#pragma once
#include <iostream>
#include <cstdlib>
#include <mutex>
#include <chrono>
#include "util.h"
#include "raft_rpc.pb.h"
#include "raft_rpc_util.h"
#include "spdlog/spdlog.h"

#ifndef NDEBUG
#define MY_ASSERT(expr)                                                                                                \
    if (!(expr)) {                                                                                                     \
        spdlog::info("Assertion failed: {0} in file {1} line {2}", #expr, __FILE__, __LINE__);                         \
        std::abort();                                                                                                  \
    }
#else
#define MY_ASSERT(expr) ((void)0)
#endif

/**节点状态（竞选、网络） */

constexpr int kKilled = 0;
constexpr int kHaveVoted = 1; // 本轮已经投过票了
constexpr int kExpire = 2;    // 投票（消息、竞选者）过期
constexpr int kNormal = 3;

using time_point = std::chrono::system_clock::time_point;
using std::chrono::system_clock;

/// @brief Raft同伴节点信息
struct RaftPeer {
    std::shared_ptr<RaftRpcUtil> rpc; // rpc通信工具
    std::string ip;
    int id;          // raft节点id
    int match_index; // 最近匹配的日志索引
    int next_index;  // 下一个要进行同步的日志的索引
    uint32_t port;
};

class Raft : public raftrpc::RaftRpcService {
  private:
    std::mutex mtx_;
    int self_id_;
    int current_term_;
    int voted_for_;
    int commited_index_;
    int last_applied;                                           // 已经汇报给状态机（上层应用）的最近一条log的index
    std::vector<raftrpc::LogEntry> logs_;                       // 日志条目数组，包含了状态机要执行的指令集
    int last_snapshot_index_;                                   // 最近一条快照的最新日志index
    int last_snapshot_term_;                                    // 最近一条快照的最新日志term
    enum Status : std::uint8_t { FOLLOWER, CANDIDATE, LEADER }; // raft节点状态
    Status status_;                                             // 当前节点状态
    time_point last_reset_election_time_;                       // 最近选举重置时间点
    time_point last_reset_heartbeat_time;                       // 最近心跳重置时间点
    std::unordered_map<int, RaftPeer> raft_peers_; // 其他raft节点 [raft节点id -> RaftPeer结构体],不包含自己

  private:
    /**索引换算函数，快照会让日志逻辑index和vector 数据下标不一致*/

    MY_NODISCARD int GetLastLogIndex();
    MY_NODISCARD int GetLastLogTerm();
    void GetLastLogTermIndex(int& term, int& index);
    MY_NODISCARD int GetSliceIndexInLogs(int log_index);
    MY_NODISCARD int GetLogTerm(int log_index);

    bool UpToDate(int term, int index);
    void InitiateElection();
    bool SendRequestVote(int raft_id, std::shared_ptr<raftrpc::RequestVoteArgs> request_args,
                         std::shared_ptr<raftrpc::RequestVoteReply> request_reply, std::shared_ptr<int> voted_num);
    void ProcessVote(const raftrpc::RequestVoteArgs* vote_args, raftrpc::RequestVoteReply* vote_reply);

    void DoHeartBeat();
};