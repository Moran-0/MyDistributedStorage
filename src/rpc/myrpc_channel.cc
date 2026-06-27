#include "myrpc_channel.h"
#include <arpa/inet.h>
#include <thread>
#include <mutex>

#include "service_discovery.h"
#include "myrpc_connect_pool.h"
#include "rpc_header.pb.h"
#include "spdlog/fmt/fmt.h"

using myrpc::RpcHeader;
namespace {
/// @brief 精确接收指定字节数
/// @param fd
/// @param buf
/// @param len
/// @return
ssize_t RecvExac(int fd, char* buf, size_t len) {
    ssize_t total_recv = 0;
    while (total_recv < len) {
        ssize_t ret = recv(fd, buf + total_recv, len - total_recv, 0);
        if (ret == 0) { // 对方关闭连接
            return total_recv;
        }
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total_recv += ret;
    }
    return total_recv;
}
} // namespace

void MyRpcChannel::CallMethod(const MethodDescriptor* method, RpcController* controller, const Message* request,
                              Message* response, Closure* done) {
    /** 延迟初始化，全局请求id */
    static std::once_flag init_flag;
    std::call_once(init_flag, []() { ServiceDiscovery::GetInstance().Init(); });
    static std::atomic<uint64_t> request_id{0};
    uint64_t req_id = request_id.fetch_add(1, std::memory_order_relaxed);

    /**服务发现：获取服务地址 */
    std::string service_name = method->service()->name();
    std::string method_name = method->name();
    // 请求ID做路由key,负载更均匀分布
    std::string ip_port = ServiceDiscovery::GetInstance().GetTargetNode(service_name, std::to_string(req_id));
    auto pos = ip_port.find(':');
    if (ip_port.empty() || pos == std::string::npos || pos == 0 || pos + 1 >= ip_port.size()) {
        controller->SetFailed(
            fmt::format("service discovery failed: invalid endpoint for {0}:{1}", service_name, ip_port));
        return;
    }
    std::string ip_str = ip_port.substr(0, pos);
    int port = 0;
    try {
        port = std::stoi(ip_port.substr(pos + 1));
        if (port < 0 || port > 65535) {
            throw std::runtime_error("invalid port!");
        }
    } catch (const std::exception& e) {
        controller->SetFailed(
            fmt::format("service discovery failed: invalid port in endpoint:{0};{1}", ip_port, e.what()));
        return;
    }

    /**从连接池借用 TCP 连接 */
    int client_fd = RpcConnectPool::GetInstance().BorrowConnection(ip_str, port);
    if (client_fd == -1) {
        controller->SetFailed("create tcp connection failed!");
        return;
    }
    /**
     * 构造协议包并发送
     * ┌────────────────┬────────────────┬──────────────────┬────────────────┐
     * │   total_len    │   header_len   │   RpcHeader      │   args (body)  │
     * │   (4 bytes)    │   (4 bytes)    │   (Protobuf)     │   (Protobuf)   │
     * │   网络字节序     │   网络字节序    │   序列化后         │   序列化后      │
     * └────────────────┴────────────────┴──────────────────┴────────────────┘
     */

    std::string args_str;
    if (!request->SerializeToString(&args_str)) {
        controller->SetFailed("Failed to serialize request to string!");
        RpcConnectPool::GetInstance().ReturnConnection(ip_str, port, client_fd, true);
        return;
    }
    RpcHeader rpc_header;
    rpc_header.set_service_name(service_name);
    rpc_header.set_method_name(method_name);
    rpc_header.set_args_size(args_str.size());
    std::string header_str;
    if (!rpc_header.SerializeToString(&header_str)) {
        controller->SetFailed("Failed to serialize rpc_header to string!");
        RpcConnectPool::GetInstance().ReturnConnection(ip_str, port, client_fd, true);
        return;
    }
    // 网络字节序总长度
    uint32_t net_total_len = htonl(kFixHeaderSize + header_str.size() + args_str.size());
    // RpcHeader长度
    uint32_t net_header_len = htonl(header_str.size());
    // 组织发送报文字符串
    std::string send_rpc_str;
    send_rpc_str.reserve(kFixHeaderSize + kFixHeaderSize + header_str.size() + args_str.size()); // 预分配空间
    send_rpc_str.append(reinterpret_cast<char*>(&net_total_len), kFixHeaderSize);
    send_rpc_str.append(reinterpret_cast<char*>(&net_header_len), kFixHeaderSize);
    send_rpc_str.append(header_str);
    send_rpc_str.append(args_str);
    size_t send_size = send_rpc_str.size();
    size_t send_left = send_rpc_str.size();
    // 确保数据全部发送出去了
    while (send_left > 0) {
        ssize_t send_bytes = send(client_fd, send_rpc_str.c_str() + send_size - send_left, send_left, MSG_NOSIGNAL);
        if (send_bytes > 0) {
            send_left -= static_cast<size_t>(send_bytes);
            continue;
        }
        if (send_bytes == -1 && errno == EINTR) {
            continue;
        }
        controller->SetFailed("send rpc message failed!");
        RpcConnectPool::GetInstance().ReturnConnection(ip_str, port, client_fd, true);
        return;
    }

    /**
     * 接收响应并解码
     * ┌────────────────┬─────────────────┐
     * │   total_len    │    args (body)  │
     * │   (4 bytes)    │    (Protobuf)   │
     * │   网络字节序     │    序列化后      │
     * └────────────────┴─────────────────┘
     */
    uint32_t response_len;
    if (RecvExac(client_fd, reinterpret_cast<char*>(&response_len), kFixHeaderSize) != kFixHeaderSize) {
        controller->SetFailed("recv rpc response failed!");
        RpcConnectPool::GetInstance().ReturnConnection(ip_str, port, client_fd, true);
        return;
    }
    response_len = ntohl(response_len);
    std::vector<char> response_buf(response_len);
    if (RecvExac(client_fd, response_buf.data(), response_len) != response_len) {
        controller->SetFailed("recv rpc response content failed!");
        RpcConnectPool::GetInstance().ReturnConnection(ip_str, port, client_fd, true);
        return;
    }
    if (!response->ParseFromArray(response_buf.data(), response_buf.size())) {
        controller->SetFailed("parse response  failed!");
        RpcConnectPool::GetInstance().ReturnConnection(ip_str, port, client_fd, true);
        return;
    }

    RpcConnectPool::GetInstance().ReturnConnection(ip_str, port, client_fd, false);
}
