#pragma once
#include <memory>
#include <unordered_map>
#include <mutex>

#include "lock_free_queue.h"

// 连接桶
struct ConnectionBucket {
    std::unique_ptr<LockFreeQueue<int>> free_fds; // 空闲套接字
    std::atomic<int> active_count;                // 已建立连接数
    ConnectionBucket(size_t capacity) : free_fds(std::make_unique<LockFreeQueue<int>>(capacity)), active_count(0) {}
};

/// @brief 单例模式RPC连接池
class RpcConnectPool {
  public:
    static RpcConnectPool& GetInstance();                                                     // 获取实例
    int BorrowConnection(const std::string& ip, uint16_t port);                               // 借用连接
    void ReturnConnection(const std::string& ip, uint16_t port, int fd, bool is_bad = false); // 归还连接

  private:
    RpcConnectPool();
    ~RpcConnectPool();
    void WarmUp(const std::string& ip, uint16_t port, int count); // 预热连接

    int max_conn_per_;
    std::unordered_map<std::string, std::unique_ptr<ConnectionBucket>> pools_;
    std::mutex mtx_;
};
