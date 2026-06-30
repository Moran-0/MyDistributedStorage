#pragma once
#include "raft_rpc.pb.h"
#include "myrpc_channel.h"
#include "util.h"
/**
 * 用于Raft共识层和服务层进行RPC消息通信
 */

class RaftRpcUtil {
  private:
    std::unique_ptr<raftrpc::RaftRpcService_Stub> raft_service_stub_;

  public:
    MY_NODISCARD bool AppendEntries(const raftrpc::AppendEntriesArgs* request, raftrpc::AppendEntriesReply* response);
    MY_NODISCARD bool RequestVote(const raftrpc::RequestVoteArgs* request, raftrpc::RequestVoteReply* response);
    MY_NODISCARD bool InstallSnapshot(const raftrpc::InstallSnapshotArgs* request,
                                      raftrpc::InstallSnapshotReply* response);
    void SetTarget(std::string const& ip, uint16_t port);
    RaftRpcUtil();
    RaftRpcUtil(std::string const& ip, uint16_t port);
    ~RaftRpcUtil() = default;
};