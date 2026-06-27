#include "myrpc_provider.h"
#include "myrpc_config.h"
#include "zookeeper_util.h"
#include "spdlog/spdlog.h"
#include "myrpc_channel.h"
#include "rpc_header.pb.h"

using myrpc::RpcHeader;
namespace {
constexpr uint32_t kMaxRpcFrameSize = 16 * 1024 * 1024;
constexpr uint32_t kMaxRpcHeaderSize = 64 * 1024;
/// @brief protobuf Closure回调类实现
class MyRpcClosure : public google::protobuf::Closure {
  private:
    std::function<void()> cb_;

  public:
    MyRpcClosure(std::function<void()> callback) : cb_(std::move(callback)) {};
    void Run() override {
        cb_();
        delete this; // 自我删除，释放资源
    };
};

} // namespace

MyRpcProvider::~MyRpcProvider() {
    spdlog::debug("~MyRpcProvider()");
    for (auto& si : services_map_) {
        if (si.second.service) {
            delete si.second.service;
            si.second.service = nullptr;
        }
    }
}

void MyRpcProvider::NotifyService(google::protobuf::Service* service) {
    auto* service_descriptor = service->GetDescriptor();
    std::string service_name = service_descriptor->name();
    int method_count = service_descriptor->method_count();
    ServiceInfo service_info;
    service_info.service = service;
    for (int i = 0; i < method_count; ++i) {
        auto* method_descriptor = service_descriptor->method(i);
        std::string method_name = method_descriptor->name();
        service_info.method_map.emplace(method_name, method_descriptor);
    }
    services_map_.emplace(service_name, service_info);
    spdlog::info("successfully notifyservice {}", service_name);
}

void MyRpcProvider::Run() {
    /**创建TCP 服务器处理tcp网络请求 */
    std::string server_ip = MyRpcConfig::GetInstance().Load("rpcserverip");
    uint16_t server_port = std::stoi(MyRpcConfig::GetInstance().Load("rpcserverport"));
    muduo::net::InetAddress address(server_ip, server_port);
    muduo::net::TcpServer tcp_server(&event_loop_, address, "MyRpcProvider");
    tcp_server.setConnectionCallback([this](const muduo::net::TcpConnectionPtr& conn) { this->OnConnection(conn); });
    tcp_server.setMessageCallback([this](const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buffer,
                                         muduo::Timestamp receive_time) { OnMessage(conn, buffer, receive_time); });
    tcp_server.setThreadNum(4); // 4各IO线程
    thread_pool_.start(100);    // 线程池处理业务

    /**zookeeper服务注册 */
    ZkClient zk_client;
    zk_client.Start();
    std::string ip_port = server_ip + ":" + std::to_string(server_port);
    for (const auto& pair : services_map_) {
        std::string service_path = "/" + pair.first;
        zk_client.Create(service_path, {}); // 永久节点

        std::string instance_path = service_path + "/" + ip_port;
        zk_client.Create(instance_path, ip_port, ZOO_EPHEMERAL); // 临时节点
    }

    spdlog::info("MyRpcProvider start at ip:{0},port:{1}", server_ip, server_port);

    /**启动tcp server */
    tcp_server.start();
    event_loop_.loop();
}

void MyRpcProvider::OnConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (!conn->connected()) {
        conn->shutdown(); // 关闭无用连接
    }
}

void MyRpcProvider::OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buffer,
                              muduo::Timestamp receive_time) {
    while (buffer->readableBytes() >= kFixHeaderSize) // 处理粘包
    {
        /**解析请求数据 */
        uint32_t total_len;
        std::memcpy(&total_len, buffer->peek(), kFixHeaderSize);
        total_len = ntohl(total_len); // 改变字节序
        if (total_len < kFixHeaderSize || total_len > kMaxRpcFrameSize) {
            spdlog::warn("invalid rpc frame length: {}", total_len);
            conn->shutdown();
            return;
        }
        // 数据不够一帧 → 退出，等下次数据到达
        if (buffer->readableBytes() < kFixHeaderSize + total_len) {
            break;
        }

        buffer->retrieve(kFixHeaderSize);
        uint32_t header_len;
        std::memcpy(&header_len, buffer->peek(), kFixHeaderSize);
        header_len = ntohl(header_len);
        buffer->retrieve(kFixHeaderSize);
        if (header_len == 0 || header_len > kMaxRpcHeaderSize || header_len > total_len - kFixHeaderSize) {
            spdlog::warn("invalid rpc header length: {}, total_len: {}", header_len, total_len);
            conn->shutdown();
            return;
        }

        uint32_t args_len = total_len - kFixHeaderSize - header_len;
        std::string header_str(buffer->peek(), header_len);
        buffer->retrieve(header_len);

        std::string args_str(buffer->peek(), args_len);
        buffer->retrieve(args_len); // 一帧数据接收完毕

        /**数据反序列化 */
        RpcHeader rpc_header;
        if (!rpc_header.ParseFromString(header_str)) {
            spdlog::error("failed parse RpcHeader from string!");
            return;
        }
        /**服务查找和反射创建对象 */
        std::string service_name = rpc_header.service_name();
        std::string method_name = rpc_header.method_name();
        if (rpc_header.args_size() != args_len) {
            spdlog::warn("rpc args size mismatch, header args_size: {}, actual args_len: {}", rpc_header.args_size(),
                         args_len);
            conn->shutdown();
            return;
        }
        auto service_it = services_map_.find(service_name);
        if (service_it == services_map_.end()) {
            spdlog::warn("service {} is not exist!", service_name);
            return;
        }
        auto* service = service_it->second.service;
        auto method_it = service_it->second.method_map.find(method_name);
        if (method_it == service_it->second.method_map.end()) {
            spdlog::warn("service {0} don`t have method {1}", service_name, method_name);
            return;
        }
        // 反射创建对应方法的请求和回复对象
        auto* methdo_descriptor = method_it->second;
        auto* request = service->GetRequestPrototype(methdo_descriptor).New();
        auto* response = service->GetResponsePrototype(methdo_descriptor).New();
        if (!request->ParseFromString(args_str)) {
            spdlog::error("Failed to parse request from string!");
            delete request;
            delete response;
            return;
        }
        /**提交业务到线程池中 */
        auto* done = new MyRpcClosure([this, conn, request, response]() {
            // request和response对象在SendRpcResponse函数内部进行释放
            SendRpcResponse(conn, response, request);
        });

        thread_pool_.run([service, methdo_descriptor, request, response, done]() {
            // 具体提供服务的service不使用controller,controller一般在channel中使用
            service->CallMethod(methdo_descriptor, nullptr, request, response, done);
        });
    }
}

/// @brief 发送rpc回复
/// @param conn
/// @param response
/// @param request
void MyRpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr& conn, google::protobuf::Message* response,
                                    google::protobuf::Message* request) {
    std::string response_str;
    if (!response->SerializeToString(&response_str)) {
        spdlog::error("Failed to Serialize response to string");
        delete request;
        delete response;
        return;
    }
    std::string send_str;
    send_str.reserve(kFixHeaderSize + response_str.size());
    uint32_t res_len = htonl(response_str.size());
    send_str.append(reinterpret_cast<char*>(&res_len), kFixHeaderSize);
    send_str.append(response_str.c_str(), response_str.size());
    conn->send(send_str.c_str(), send_str.size());
    delete request;
    delete response;
}
