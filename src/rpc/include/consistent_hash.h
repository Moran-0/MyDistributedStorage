#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include <shared_mutex>
#include <thread>
#include <memory>
#include <mutex>

/**
 * 一致性哈希算法实现-负责服务负载均衡
 */

struct HashConfig {
    int replicas;             // 每个节点的初始虚拟节点数
    int min_replicas;         // 最小虚拟节点数
    int max_replicas;         // 最大虚拟节点数
    double balance_threshold; // 负载不均阈值
};

/// @brief 一致性哈希
class ConsistentHash {
  private:
    HashConfig config_;
    std::vector<uint32_t> keys_;                         // 排序的节点哈希值数组，用于二分查找
    std::unordered_map<uint32_t, std::string> ring_;     // 节点哈希值->物理节点名称的映射
    std::unordered_map<std::string, int> node_replicas_; // 物理节点->虚拟节点数量
    std::unordered_map<std::string, std::shared_ptr<std::atomic<unsigned long long>>> node_counts_; // 每个节点的请求数
    std::atomic<unsigned long long> total_requests_;                                                // 总请求数

    mutable std::shared_timed_mutex rw_mtx_; // 超时读写锁
    std::thread balance_thread_;             // 后台监控负载均衡
    std::atomic<bool> stop_balance_;         // 负载均衡停止标志

    void AddNodesInternal(std::string const& node, int replicas); // 添加内部虚拟节点
    void StartBalancer();                                         // 开启负载均衡
    void CheckAndRebalance();                                     // 检查是否需要重均衡
    void Rebalance();                                             // 重均衡

  public:
    ConsistentHash(HashConfig cfg = {10, 5, 200, 0.25});
    ~ConsistentHash() = default;
    void AddNodes(const std::vector<std::string>& nodes);
    void RemoveNode(std::string const& key);
    std::string GetTargetNode(std::string const& key);
    void UpdateNodes(std::vector<std::string>& nodes);
};
