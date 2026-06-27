#include "myrpc_connect_pool.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include "spdlog/spdlog.h"

static constexpr int kMaxConnSize = 1024;
constexpr int kWarmUpSize = 16;

RpcConnectPool& RpcConnectPool::GetInstance() {
    static RpcConnectPool rpc_connect_pool;
    return rpc_connect_pool;
}

/// @brief 借用连接
/// @param ip
/// @param port
/// @return
int RpcConnectPool::BorrowConnection(const std::string& ip, uint16_t port) {
    std::string key = ip + ":" + std::to_string(port);
    ConnectionBucket* conn_bucket = nullptr;
    {
        std::scoped_lock lock(mtx_);
        auto pair = pools_.find(key);
        if (pair != pools_.end()) {
            conn_bucket = pair->second.get();
        }
    }
    if (!conn_bucket) {
        WarmUp(ip, port, kWarmUpSize);
        std::scoped_lock lock(mtx_);
        conn_bucket = pools_[key].get();
    }
    int fd = -1;
    if (conn_bucket->free_fds->Pop(fd)) {
        return fd;
    }
    WarmUp(ip, port, kWarmUpSize); // 队列为空，预热连接一次
    if (conn_bucket->free_fds->Pop(fd)) {
        return fd;
    }
    // 预热连接之后依旧失败，直接返回-1，借用连接失败
    spdlog::warn("BorrowConnection from {0}:{1} failed!", ip, port);
    return -1;
}

/// @brief 归还连接
/// @param ip
/// @param port
/// @param fd
/// @param is_bad
void RpcConnectPool::ReturnConnection(const std::string& ip, uint16_t port, int fd, bool is_bad) {
    std::string key = ip + ":" + std::to_string(port);
    ConnectionBucket* conn_bucket = nullptr;
    {
        std::scoped_lock lock(mtx_);
        auto pair = pools_.find(key);
        if (pair != pools_.end()) {
            conn_bucket = pair->second.get();
        }
    }
    if (!conn_bucket) {
        close(fd);
        spdlog::warn("ReturnConnection fd {0} from {1}:{2} is not exist,just close socket!", fd, ip, port);
        return;
    }
    if (is_bad) {
        close(fd);
        conn_bucket->active_count.fetch_sub(1);
        return;
    }
    if (!conn_bucket->free_fds->Push(fd)) {
        close(fd);
        conn_bucket->active_count.fetch_sub(1);
    }
}

RpcConnectPool::RpcConnectPool() : max_conn_per_(kMaxConnSize) {}

RpcConnectPool::~RpcConnectPool() {
    for (auto& ip_conn_pair : pools_) {
        int fd;
        while (ip_conn_pair.second->free_fds->Pop(fd)) {
            close(fd);
        }
    }
}

/// @brief 预热连接
/// @param ip
/// @param port
/// @param count
void RpcConnectPool::WarmUp(const std::string& ip, uint16_t port, int count) {
    std::string key = ip + ":" + std::to_string(port);
    ConnectionBucket* conn_bucket = nullptr;
    // 判断ConnectionBucket是否存在
    {
        std::scoped_lock lock(mtx_); // pools_互斥访问
        auto res = pools_.find(key);
        if (res == pools_.end()) {
            auto [it, inserted] = pools_.try_emplace(key, std::make_unique<ConnectionBucket>(kMaxConnSize));
            conn_bucket = it->second.get();
        } else {
            conn_bucket = res->second.get();
        }
    }
    int success_add = 0;
    // 往connBucket中加入连接
    for (int i = 0; i < count; ++i) {
        if (conn_bucket->active_count.load() > max_conn_per_) {
            break;
        }
        int fd = socket(AF_INET, SOCK_STREAM, 0); // 建立tcp ipv4连接
        if (fd == -1) {
            continue;
        }
        // 设置tcp连接非阻塞，nodelay
        int opt = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());

        if (connect(fd, (sockaddr*)&addr, sizeof(sockaddr)) == 0) {
            conn_bucket->active_count.fetch_add(1);
            if (!conn_bucket->free_fds->Push(fd)) {
                conn_bucket->active_count.fetch_sub(1);
                close(fd);
            } else {
                ++success_add;
            }
        } else {
            close(fd);
        }
    }
}
