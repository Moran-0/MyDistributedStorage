#include <iostream>
#include <memory>

#include "myrpc_channel.h"
#include "myrpc_config.h"
#include "myrpc_controller.h"
#include "rpc_demo.pb.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: echo_client <config.json> <message>\n";
        return 1;
    }

    MyRpcConfig::GetInstance().LoadConfigFile(argv[1]);

    MyRpcChannel channel;
    MyRpcController controller;
    rpcdemo::EchoService_Stub stub(&channel);

    rpcdemo::EchoRequest request;
    rpcdemo::EchoResponse response;
    request.set_msg(argv[2]);

    stub.Echo(&controller, &request, &response, nullptr);

    if (controller.Failed()) {
        std::cerr << "rpc failed: " << controller.ErrorText() << "\n";
        return 1;
    }

    std::cout << response.msg() << std::endl;
    return 0;
}
