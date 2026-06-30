#pragma once

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/stubs/callback.h>

using google::protobuf::Closure;
using google::protobuf::Message;
using google::protobuf::MethodDescriptor;
using google::protobuf::RpcController;

constexpr int kFixHeaderSize = 4;

class MyRpcChannel : public google::protobuf::RpcChannel {
  private:
    std::string target_ip_;
    uint16_t target_port_;
    bool have_target_;

  public:
    MyRpcChannel() : target_port_(0), have_target_(false) {}
    MyRpcChannel(std::string const& ip, uint16_t port) : target_ip_(ip), target_port_(port), have_target_(true) {}
    virtual ~MyRpcChannel() override {}
    void SetTarget(std::string const& ip, uint16_t port);
    void CallMethod(const MethodDescriptor* method, RpcController* controller, const Message* request,
                    Message* response, Closure* done) override;
};