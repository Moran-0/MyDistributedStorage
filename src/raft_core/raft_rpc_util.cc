#include "raft_rpc_util.h"
#include "myrpc_controller.h"
#include "spdlog/spdlog.h"

/// @brief 通过stub调用AppendEntries
/// @param request
/// @param response
/// @return
bool RaftRpcUtil::AppendEntries(const raftrpc::AppendEntriesArgs* request, raftrpc::AppendEntriesReply* response) {
    MyRpcController controller;
    // stub使用的channel内部不调用done回调函数，可为空
    raft_service_stub_->AppendEntries(&controller, request, response, nullptr);
    if (controller.Failed()) {
        spdlog::error("Error while process RaftRpcUtil::AppendEntries;Detail: {}", controller.ErrorText());
        return false;
    }
    return true;
}

/// @brief 通过stub调用 RequestVote
/// @param request
/// @param response
/// @return
bool RaftRpcUtil::RequestVote(const raftrpc::RequestVoteArgs* request, raftrpc::RequestVoteReply* response) {
    MyRpcController controller;
    // stub使用的channel内部不调用done回调函数，可为空
    raft_service_stub_->RequestVote(&controller, request, response, nullptr);
    if (controller.Failed()) {
        spdlog::error("Error while process RaftRpcUtil::RequestVote;Detail: {}", controller.ErrorText());
        return false;
    }
    return true;
}

/// @brief 通过stub调用 InstallSnapshot
/// @param request
/// @param response
/// @return
bool RaftRpcUtil::InstallSnapshot(const raftrpc::InstallSnapshotArgs* request,
                                  raftrpc::InstallSnapshotReply* response) {
    MyRpcController controller;
    // stub使用的channel内部不调用done回调函数，可为空
    raft_service_stub_->InstallSnapshot(&controller, request, response, nullptr);
    if (controller.Failed()) {
        spdlog::error("Error while process RaftRpcUtil::InstallSnapshot;Detail: {}", controller.ErrorText());
        return false;
    }
    return true;
}

/// @brief 设置rpc通信目标
/// @param ip
/// @param port
void RaftRpcUtil::SetTarget(std::string const& ip, uint16_t port) {
    auto* channel = static_cast<MyRpcChannel*>(raft_service_stub_->channel());
    channel->SetTarget(ip, port);
}

RaftRpcUtil::RaftRpcUtil() {
    // channel由stbu持有并负责资源释放
    raft_service_stub_ = std::make_unique<raftrpc::RaftRpcService_Stub>(new MyRpcChannel(),
                                                                        google::protobuf::Service::STUB_OWNS_CHANNEL);
}

RaftRpcUtil::RaftRpcUtil(std::string const& ip, uint16_t port) {
    // channel由stbu持有并负责资源释放
    raft_service_stub_ = std::make_unique<raftrpc::RaftRpcService_Stub>(new MyRpcChannel(ip, port),
                                                                        google::protobuf::Service::STUB_OWNS_CHANNEL);
}
