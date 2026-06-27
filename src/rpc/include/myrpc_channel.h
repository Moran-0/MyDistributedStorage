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
  public:
    MyRpcChannel() {};
    virtual ~MyRpcChannel() override {}
    void CallMethod(const MethodDescriptor* method, RpcController* controller, const Message* request,
                    Message* response, Closure* done) override;
};