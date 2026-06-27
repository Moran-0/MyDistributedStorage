#pragma once
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "zookeeper_util.h"
#include "consistent_hash.h"

/**
 * 服务发现中枢设计
 */

/// @brief 服务发现
class ServiceDiscovery {
  private:
    ServiceDiscovery() = default;
    ~ServiceDiscovery() = default;
    std::shared_timed_mutex rw_mtx_;                                              // 读写锁
    ZkClient zk_client_;                                                          // zookeeper客户端
    std::unordered_map<std::string, std::unique_ptr<ConsistentHash>> nodes_hash_; // 服务名 → 哈希环
    // std::unordered_map<std::string, std::vector<std::string>> nodes_cache_;       // 服务名 → 原始节点列表

    static void WatcherCallback(zhandle_t* zh, int type, int state, const char* path,
                                void* watcherCtx); // 回调函数：ZK 推送 → 自动更新

  public:
    ServiceDiscovery(const ServiceDiscovery&) = delete;
    ServiceDiscovery& operator=(const ServiceDiscovery&) = delete;
    static ServiceDiscovery& GetInstance();
    void Init();
    std::string GetTargetNode(std::string const& service_name, std::string const& key);
};
