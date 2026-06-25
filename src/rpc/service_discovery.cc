#include "service_discovery.h"

void ServiceDiscovery::WatcherCallback(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx) {
    if (type != ZOO_CHANGED_EVENT) {
        return;
    }
    auto* sd = static_cast<ServiceDiscovery*>(watcherCtx);
    std::string service_name(path);
    service_name = service_name.substr(1); // 去除开头的 “/”
    // 回调函数注册是一次性的，每次执行回调函数时需要重新注册
    auto update_nodes = sd->zk_client_->GetChildren(path, WatcherCallback, sd);
    std::unique_lock lock(sd->rw_mtx_);
    if (sd->nodes_hash_.find(service_name) != sd->nodes_hash_.end()) {
        sd->nodes_hash_[service_name]->UpdateNodes(update_nodes);
    }
}

ServiceDiscovery& ServiceDiscovery::GetInstance() {
    static ServiceDiscovery service_discovery;
    return service_discovery;
}

void ServiceDiscovery::Init() {
    zk_client_ = new ZkClient(); // 创建zookeeper客户端
}
/// @brief 获取物理节点名称
/// @param service_name
/// @param key
/// @return
std::string ServiceDiscovery::GetTargetNode(std::string const& service_name, std::string const& key) {
    // 快速路由，查看当前是否已有缓存
    {
        std::shared_lock lock(rw_mtx_); // 读锁
        auto it = nodes_hash_.find(service_name);
        if (it != nodes_hash_.end()) {
            return it->second->GetTargetNode(key);
        }
    }
    // 如果没有缓存，那么去zookeeper路由中心拉取
    std::string service_path = std::string("/") + service_name;
    std::vector<std::string> update_nodes = zk_client_->GetChildren(service_path, WatcherCallback, this);
    {
        std::unique_lock lock(rw_mtx_); // 写锁
        // 二次检查，可能在zookeeper拉取服务信息时已经更新
        if (nodes_hash_.find(service_name) == nodes_hash_.end()) {
            nodes_hash_[service_name] = std::make_unique<ConsistentHash>();
            nodes_hash_[service_name]->AddNodes(update_nodes);
            // nodes_cache_[service_name] = update_nodes;
        }
        return nodes_hash_[service_name]->GetTargetNode(key);
    }
}
