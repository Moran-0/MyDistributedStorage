#include "zookeeper_util.h"
#include "myrpc_config.h"
#include "spdlog/spdlog.h"

namespace {
constexpr int kZkTimeoutMs = 6000;
constexpr int kPathBufLen = 512;
constexpr int kDataBufLen = 512;
constexpr int kWaitTimeOut = 2;
} // namespace

void ZkClient::SessionWatcher(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx) {
    (void)zh;
    (void)type;
    (void)path;
    auto* client = static_cast<ZkClient*>(watcherCtx);
    if (client != nullptr) {
        client->OnSessionEvent(state);
        client->cv_.notify_all(); // 通知zhandle句柄创建状态
    }
}

void ZkClient::OnSessionEvent(int state) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (state == ZOO_CONNECTED_STATE) {
        is_connected_ = true;
        return;
    }
    if (state == ZOO_EXPIRED_SESSION_STATE || state == ZOO_AUTH_FAILED_STATE) {
        is_connected_ = false;
    }
}

bool ZkClient::EnsureConnected() {
    std::lock_guard<std::mutex> lock(mtx_);
    return zhandle_ != nullptr && is_connected_;
}

/// @brief 创建一个节点前确保该节点的父节点存在
/// @param path
/// @return
bool ZkClient::EnsureParentPath(const std::string& path) {
    std::vector<std::string> parents;
    {
        size_t pos = 0;
        while ((pos = path.find('/', pos + 1)) != std::string::npos) {
            parents.push_back(path.substr(0, pos));
        }
    }
    for (const auto& parent : parents) {
        // 不使用zoo_exists判断是否存在节点，直接进行创建
        int rc = zoo_create(zhandle_, parent.c_str(), nullptr, 0, &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
        if (rc != ZOK && rc != ZNODEEXISTS) {
            spdlog::error("create parent path failed: {0}, rc:{1}", parent, rc);
            return false;
        }
    }
    return true;
}
ZkClient::ZkClient() : is_connected_(false), is_closing_(false), zhandle_(nullptr) {}

ZkClient::~ZkClient() {
    std::scoped_lock lock(mtx_);
    is_closing_ = true;
    if (zhandle_ != nullptr) {
        zookeeper_close(zhandle_);
        zhandle_ = nullptr;
    }
}

/// @brief 创建zhandle_t句柄
void ZkClient::Start() {
    {
        std::scoped_lock lock(mtx_);
        if (zhandle_ != nullptr && is_connected_) {
            return; // 已经建立连接
        }
    }
    std::string ip = MyRpcConfig::GetInstance().Load("zookeeperip");
    std::string port = MyRpcConfig::GetInstance().Load("zookeeperport");
    connstr_ = ip + ":" + port;
    zhandle_ = zookeeper_init(connstr_.c_str(), SessionWatcher, kZkTimeoutMs, nullptr, this, 0);
    if (zhandle_ == nullptr) {
        spdlog::error("zookeeper_init error");
        return;
    }
    std::unique_lock lock(mtx_);
    bool state = cv_.wait_for(lock, std::chrono::seconds(kWaitTimeOut), [this]() { return is_connected_; });
    if (state) {
        spdlog::info("zookeeper_init success!");
    } else {
        spdlog::error("zookeeper_init error");
    }
}

/// @brief 获取节点数据 ip:port字符串
/// @param path
/// @return
std::string ZkClient::GetData(std::string const& path) {
    if (!EnsureConnected() || path.empty()) {
        spdlog::warn("GetDate while unconnected or with empty path");
        return "";
    }
    char buf[kDataBufLen] = {0};
    int buf_len = sizeof(buf);
    Stat stat;
    int rc = zoo_get(zhandle_, path.c_str(), 0, buf, &buf_len, &stat);
    if (rc != ZOK) {
        spdlog::error("zoo_get failed with path {0} rc {1}", path, rc);
        return "";
    }
    return std::string(buf, buf_len);
}

/// @brief 获取该服务的子节点数据 (提供该服务的物理节点ip:port数据)
/// @param path
/// @param fn
/// @param cbContext
/// @return
std::vector<std::string> ZkClient::GetChildren(std::string const& path, watcher_fn fn, void* cbContext) {
    std::vector<std::string> res;
    if (!EnsureConnected() || path.empty()) {
        return res;
    }
    String_vector nodes;
    std::memset(&nodes, 0, sizeof(nodes));
    int rc = zoo_wget_children(zhandle_, path.c_str(), fn, cbContext, &nodes);
    if (rc != ZOK) {
        spdlog::error("zoo_wget_children failed with path {0}, rc {1}", path, rc);
        return res;
    }
    for (int i = 0; i < nodes.count; ++i) {
        std::string child_path = path;
        if (!child_path.empty() && child_path.back() != '/') {
            child_path.push_back('/');
        }
        child_path.append(nodes.data[i]);
        std::string data = GetData(child_path);
        if (!data.empty()) {
            res.push_back(data);
        }
    }
    deallocate_String_vector(&nodes);
    return res;
}

bool ZkClient::IsConnected() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return zhandle_ != nullptr && is_connected_;
}

/// @brief 创建指定路径的znode节点
/// @param path
/// @param data
/// @param state
void ZkClient::Create(std::string const& path, std::string const& data, int state) {
    if (!EnsureConnected()) {
        spdlog::error("zookeeper not connected");
        return;
    }
    if (path.empty()) {
        spdlog::warn("empty znode path!");
        return;
    }
    if (!EnsureParentPath(path)) {
        return;
    }
    char create_path[kPathBufLen] = {0};
    int rc = zoo_create(zhandle_, path.c_str(), data.c_str(), data.length(), &ZOO_OPEN_ACL_UNSAFE, state, create_path,
                        sizeof(create_path));
    if (rc != ZOK && rc != ZNODEEXISTS) {
        spdlog::error("create znode of path {0} failed! rc is {1}", path, rc);
        return;
    }
    spdlog::info("create znode of path [{}] success!", path);
}
