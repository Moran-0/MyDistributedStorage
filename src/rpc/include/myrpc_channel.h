#pragma once

#include <google/protobuf/service.h>
using google::protobuf::Closure;
using google::protobuf::Message;
using google::protobuf::MethodDescriptor;
using google::protobuf::RpcController;
class MyRpcChannel : public google::protobuf::RpcChannel {
  public:
    MyRpcChannel();
    virtual ~MyRpcChannel();
    void CallMethod(const MethodDescriptor* method, RpcController* controller, const Message* request,
                    Message* response, Closure* done) override;
};