#include <iostream>
#include <memory>

#include "myrpc_config.h"
#include "myrpc_provider.h"
#include "rpc_demo.pb.h"

class EchoServiceImpl final : public rpcdemo::EchoService {
 public:
  void Echo(::google::protobuf::RpcController* controller,
            const ::rpcdemo::EchoRequest* request,
            ::rpcdemo::EchoResponse* response,
            ::google::protobuf::Closure* done) override {
    (void)controller;
    response->set_msg("echo: " + request->msg());
    if (done) {
      done->Run();
    }
  }
};

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: echo_server <config.json>\n";
    return 1;
  }

  MyRpcConfig::GetInstance().LoadConfigFile(argv[1]);

  auto* service = new EchoServiceImpl();
  MyRpcProvider provider;
  provider.NotifyService(service);
  provider.Run();
  return 0;
}
