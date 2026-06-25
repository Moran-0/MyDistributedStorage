#include "myrpc_channel.h"

MyRpcChannel::MyRpcChannel() {}

MyRpcChannel::~MyRpcChannel() {}

void MyRpcChannel::CallMethod(const MethodDescriptor* method, RpcController* controller, const Message* request,
                              Message* response, Closure* done) {}
