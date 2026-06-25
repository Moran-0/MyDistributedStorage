#include "consistent_hash.h"
#include "boost/crc.hpp"
#include <cmath>

void ConsistentHash::AddNodesInternal(std::string const& node, int replicas) {
    boost::crc_32_type crc32_computer;
    for (int i = 0; i < replicas; ++i) {
        std::string virtual_node = node + "#" + std::to_string(i);
        crc32_computer.reset(); // 重置状态
        crc32_computer.process_bytes(virtual_node.c_str(), virtual_node.size());
        uint32_t hash_val = crc32_computer.checksum();
        keys_.push_back(hash_val); // 存储哈希值
        ring_[hash_val] = node;    // 添加映射
    }
    node_replicas_[node] = replicas;
    if (node_counts_.find(node) == node_counts_.end()) {
        node_counts_[node] = std::make_shared<std::atomic<unsigned long long>>(0);
    }
}
/// @brief 开启负载均衡后台线程
void ConsistentHash::StartBalancer() {
    balance_thread_ = std::thread([this]() {
        while (!stop_balance_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!stop_balance_) {
                // 防止休眠后状态变化
                CheckAndRebalance();
            }
        }
    }); // std::thread 的构造函数自动启动线程
}

void ConsistentHash::CheckAndRebalance() {
    if (total_requests_.load(std::memory_order_relaxed) <= 500) {
        // 请求数太少，无需负载均衡
        return;
    }
    std::unique_lock lock(rw_mtx_); // 写锁
    if (node_counts_.empty()) {
        return;
    }
    double avg =
        static_cast<double>(total_requests_.load(std::memory_order_relaxed)) / static_cast<double>(node_counts_.size());
    double max_load = 0.0;
    for (const auto& pair : node_counts_) {
        double load = (static_cast<double>(pair.second->load()) - avg) / avg;
        max_load = std::max(load, max_load);
    }
    // 超过阈值，执行重均衡
    if (max_load > config_.balance_threshold) {
        Rebalance();
    }
}

/// @brief 重均衡
void ConsistentHash::Rebalance() {
    double avg =
        static_cast<double>(total_requests_.load(std::memory_order_relaxed)) / static_cast<double>(node_counts_.size());
    std::unordered_map<std::string, int> old_replicas = node_replicas_;
    for (const auto& pair : node_counts_) {
        const std::string& node = pair.first;
        auto count = pair.second->load();
        double ratio = static_cast<double>(count) / avg;
        int old_replicas = node_replicas_[node];
        int new_replicas;
        // 重新分配虚拟节点-不对称控制策略
        if (ratio > 1.0) {
            new_replicas = static_cast<int>(std::round(old_replicas / ratio)); // 热节点快速收缩
        } else {
            new_replicas = static_cast<int>(std::round(old_replicas * (2.0 - ratio))); // 冷节点缓慢扩张
        }
        RemoveNode(node);
        AddNodesInternal(node, new_replicas);
    }
    total_requests_.store(0, std::memory_order_relaxed);
    std::sort(keys_.begin(), keys_.end());
}

ConsistentHash::ConsistentHash(HashConfig cfg) : config_(cfg) {}

/// @brief 添加物理节点
/// @param nodes
void ConsistentHash::AddNodes(const std::vector<std::string>& nodes) {
    std::unique_lock lock(rw_mtx_); // 写锁
    for (const auto& key : nodes) {
        if (key.empty()) {
            continue;
        }
        AddNodesInternal(key, config_.replicas);
    }
    std::sort(keys_.begin(), keys_.end());
}

/// @brief 删除物理节点
/// @param key
void ConsistentHash::RemoveNode(std::string const& key) {
    if (key.empty()) {
        return;
    }
    auto node = node_replicas_.find(key);
    if (node == node_replicas_.end()) {
        return;
    }
    boost::crc_32_type crc32_computer; // crc32哈希函数
    for (int i = 0; i < node->second; ++i) {
        std::string virtual_node = key + "#" + std::to_string(i);
        crc32_computer.reset(); // 重置状态
        crc32_computer.process_bytes(virtual_node.c_str(), virtual_node.size());
        uint32_t hash_val = crc32_computer.checksum();
        auto it_vec = std::remove(keys_.begin(), keys_.end(), hash_val);
        keys_.erase(it_vec);
        ring_.erase(hash_val);
    }
    // 删除节点后无需重新排序

    node_counts_.erase(key);
    node_replicas_.erase(key);
}

/// @brief 获取物理节点名称
/// @param key
/// @return
std::string ConsistentHash::GetTargetNode(std::string const& key) {
    if (key.empty()) {
        return {};
    }
    std::string node;
    std::shared_ptr<std::atomic<unsigned long long>> counter_ptr = nullptr;
    {
        std::shared_lock lock(rw_mtx_); // 读锁
        if (keys_.empty()) {
            return {};
        }
        boost::crc_32_type crc32_computer;
        crc32_computer.process_bytes(key.c_str(), key.size());
        uint32_t hash_val = crc32_computer.checksum();
        auto it = std::lower_bound(keys_.begin(), keys_.end(), hash_val); // 二分查找第一个不小于hash_val的节点
        if (it == keys_.end()) {
            it = keys_.end(); // 超出环尾 → 回绕到环头（顺时针首节点）
        }
        node = ring_[*it];
        counter_ptr = node_counts_[node]; // 取出原子计数指针
    }
    thread_local int sample_count = 0;
    if (++sample_count >= 100) {
        if (counter_ptr) {
            counter_ptr->fetch_add(100, std::memory_order_relaxed);
        }
        total_requests_.fetch_add(100, std::memory_order_relaxed);
        sample_count = 0;
    }
    return node;
}

void ConsistentHash::UpdateNodes(std::vector<std::string>& nodes) {
    std::unique_lock lock(rw_mtx_);
    keys_.clear();
    ring_.clear();
    node_replicas_.clear();
    node_counts_.clear();
    total_requests_.store(0, std::memory_order_relaxed);
    for (const auto& node : nodes) {
        if (!node.empty()) {
            AddNodesInternal(node, config_.replicas);
        }
    }
    std::sort(keys_.begin(), keys_.end());
}
