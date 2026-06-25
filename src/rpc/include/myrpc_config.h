#pragma once
#include <string>
#include <unordered_map>

class MyRpcConfig {
  public:
    static MyRpcConfig& GetInstance();
    void LoadConfigFile(std::string const& file_name);
    std::string Load(std::string const& key);

  private:
    MyRpcConfig() = default;
    ~MyRpcConfig() = default;
    MyRpcConfig(const MyRpcConfig&) = delete;
    MyRpcConfig(MyRpcConfig&&) = delete;
    MyRpcConfig& operator=(const MyRpcConfig&) = delete;
    MyRpcConfig& operator=(MyRpcConfig&&) = delete;
    std::unordered_map<std::string, std::string> config_map_;
};