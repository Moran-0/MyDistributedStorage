#include "myrpc_config.h"
#include <iostream>
#include <fstream>
#include "spdlog/spdlog.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

MyRpcConfig& MyRpcConfig::GetInstance() {
    static MyRpcConfig rpc_config;
    return rpc_config;
}

void MyRpcConfig::LoadConfigFile(std::string const& file_name) {
    std::ifstream config_file(file_name);
    if (!config_file.is_open()) {
        spdlog::error("Failed to open file \"{}\"", file_name);
        return;
    }
    json j;
    try {
        j = json::parse(config_file);
    } catch (const json::parse_error& e) {
        spdlog::error("JSON 解析错误: {}", e.what());
        return;
    }
    for (const auto& [key, value] : j.items()) {
        config_map_[key] = value.get<std::string>();
    }
}

std::string MyRpcConfig::Load(std::string const& key) {
    auto res = config_map_.find(key);
    if (res != config_map_.end()) {
        return res->second;
    }
    return {};
}
