#pragma once
#include <string>
#include <unordered_map>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include "muduo/net/EventLoop.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/InetAddress.h"
#include "muduo/base/ThreadPool.h"
#include "muduo/net/TcpServer.h"

/// @brief myrpc 服务提供
class MyRpcProvider {
  private:
    muduo::ThreadPool thread_pool_;
    muduo::net::EventLoop event_loop_;
    struct ServiceInfo {
        google::protobuf::Service* service;
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> method_map;
    };
    std::unordered_map<std::string, ServiceInfo> services_map_; // 提供的服务信息

  public:
    MyRpcProvider() = default;
    ~MyRpcProvider();
    void NotifyService(google::protobuf::Service* service); // 注册服务
    void Run();

  private:
    void OnConnection(const muduo::net::TcpConnectionPtr& conn);
    void OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buffer, muduo::Timestamp receive_time);
    void SendRpcResponse(const muduo::net::TcpConnectionPtr& conn, google::protobuf::Message* response,
                         google::protobuf::Message* request);
};
