#include "raft.h"
/// @brief 获取最后一条日志的索引
/// @return
MY_NODISCARD int Raft::GetLastLogIndex() {
    int index = -1;
    int _ = -1;
    GetLastLogTermIndex(_, index);
    return index;
}

/// @brief 获取最后一条日志的任期
/// @return
MY_NODISCARD int Raft::GetLastLogTerm() {
    int term = -1;
    int _ = -1;
    GetLastLogTermIndex(term, _);
    return term;
}

/// @brief 获取最后一条日志的任期和索引
/// @param term
/// @param index
void Raft::GetLastLogTermIndex(int& term, int& index) {
    // todo: 后续考虑是否加读锁保护
    if (logs_.empty()) {
        term = last_snapshot_term_;
        index = last_snapshot_index_;
        return;
    }
    term = logs_.back().log_term();
    index = logs_.back().log_index();
}

/// @brief 从日志的逻辑index查找在logs_数组中的index
/// @param log_index 日志的逻辑index
/// @return
MY_NODISCARD int Raft::GetSliceIndexInLogs(int log_index) {
    MY_ASSERT(log_index > last_snapshot_index_);
    int last_log_index = GetLastLogIndex();
    MY_ASSERT(log_index <= last_log_index);
    return log_index - last_snapshot_index_ - 1;
}

/// @brief 对给定的日志的逻辑index查找对应的任期号
/// @param log_index
/// @return
MY_NODISCARD int Raft::GetLogTerm(int log_index) {
    MY_ASSERT(log_index >= last_snapshot_index_);
    if (log_index == last_snapshot_index_) {
        return last_snapshot_term_;
    }
    int last_log_index = GetLastLogIndex();
    MY_ASSERT(log_index <= last_log_index);
    int slice_index = GetSliceIndexInLogs(log_index);
    return logs_[slice_index].log_term();
}

/// @brief 判断term和index是否比当前节点的日志更新
/// @param term
/// @param index
/// @return
bool Raft::UpToDate(int term, int index) {
    int self_last_index = -1;
    int self_last_term = -1;
    GetLastLogTermIndex(self_last_term, self_last_index);
    return term > self_last_term || (term == self_last_term && index >= self_last_index);
}

/// @brief 发起选举
void Raft::InitiateElection() {
    std::lock_guard lock(mtx_);
    if (status_ == LEADER) {
        return;
    }
    status_ = CANDIDATE;
    current_term_ += 1;    // 任期自增
    voted_for_ = self_id_; // 投票给自己
    // todo: 持久化
    int last_log_index = -1;
    int last_log_term = -1;
    GetLastLogTermIndex(last_log_term, last_log_index);
    std::shared_ptr<int> voted_num = std::make_shared<int>(1);
    last_reset_election_time_ = system_clock::now();
    // 遍历其他raft节点
    for (const auto& pair : raft_peers_) {
        int raft_id = pair.first;
        auto vote_request_args = std::make_shared<raftrpc::RequestVoteArgs>();
        vote_request_args->set_term(current_term_);
        vote_request_args->set_candidate_id(self_id_);
        vote_request_args->set_last_log_index(last_log_index);
        vote_request_args->set_last_log_term(last_log_term);
        auto vote_request_reply = std::make_shared<raftrpc::RequestVoteReply>();
        // 后台线程发送投票请求，避免持锁执行耗时操作
        std::thread t([this, vote_request_args, vote_request_reply, raft_id, voted_num]() {
            SendRequestVote(raft_id, vote_request_args, vote_request_reply, voted_num);
        });
        t.detach();
    }
}

/// @brief 发送投票请求
/// @param raft_id raft节点id
/// @param request_args
/// @param request_reply
/// @param voted_num 投票数量
bool Raft::SendRequestVote(int raft_id, std::shared_ptr<raftrpc::RequestVoteArgs> request_args,
                           std::shared_ptr<raftrpc::RequestVoteReply> request_reply, std::shared_ptr<int> voted_num) {
    spdlog::info("SendRequestVote by RaftRpcUtil start!");
    auto rpc_util = raft_peers_.find(raft_id);
    if (rpc_util == raft_peers_.end()) {
        spdlog::error("Invalid raft_id!");
        return false;
    }
    bool send_success = rpc_util->second.rpc->RequestVote(request_args.get(), request_reply.get());
    if (!send_success) {
        spdlog::error("error while RequestVote!");
        return false;
    }
    spdlog::info("SendRequestVote by RaftRpcUtil end!");

    std::lock_guard lock(mtx_);
    // 判断当前是否还是本任期的CANDIDATE
    if (request_args->term() != current_term_ || status_ != CANDIDATE) {
        return true;
    }
    // 过期回复
    if (request_reply->term() < current_term_) {
        return true;
    }
    // 存在比自己任期高的raft节点，转变为follower
    if (request_reply->term() > current_term_) {
        status_ = FOLLOWER;
        voted_for_ = -1;
        current_term_ = request_reply->term();
        // todo: 持久化
        return true;
    }
    MY_ASSERT(request_reply->term() == current_term_);
    // 未投票给自己
    if (!request_reply->vote_granted()) {
        return true;
    }
    ++*voted_num;
    // 判断当前票数是否过半
    if (*voted_num > (raft_peers_.size() + 1) / 2 + 1) {
        *voted_num = 0;
        voted_for_ = -1;
        status_ = LEADER;
        int last_log_index = GetLastLogIndex();
        // 更新其他raft节点的状态
        for (auto& pair : raft_peers_) {
            pair.second.next_index = last_log_index + 1; // 假设follower已拥有自己全部日志
            pair.second.match_index = 0;
        }

        // 立刻发送心跳消息，通知follower自己的leader身份
        std::thread t([this]() { DoHeartBeat(); });
        t.detach();
        // todo: 持久化
    }
    return true;
}

/// @brief 处理竞选者的投票请求
/// @param vote_args
/// @param vote_reply
void Raft::ProcessVote(const raftrpc::RequestVoteArgs* vote_args, raftrpc::RequestVoteReply* vote_reply) {
    if (vote_args == nullptr || vote_reply == nullptr) {
        throw std::invalid_argument("[Raft::ProcessVote] nullptr function args!");
    }
    std::lock_guard lock(mtx_);

    /**提取请求参数数据 */

    int candidate_log_index = vote_args->last_log_index();
    int candidate_log_term = vote_args->last_log_term();
    int candidate_term = vote_args->term();
    int candidate_id = vote_args->candidate_id();

    /**比较竞选者与当前节点的任期 */

    if (candidate_term < current_term_) {
        // 竞选者任期小于当前节点
        vote_reply->set_term(current_term_);
        vote_reply->set_vote_granted(false);
        vote_reply->set_vote_state(kExpire);
        return;
    }
    if (candidate_term > current_term_) {
        // 竞选者的任期严格大于当前节点，当前节点状态直接设定为follower
        current_term_ = candidate_term;
        voted_for_ = -1;
        status_ = FOLLOWER;
    }
    MY_ASSERT(candidate_term == current_term_);

    /**判断竞选者持有的日志是否比当前节点更新 */
    if (!UpToDate(candidate_log_term, candidate_log_index)) {
        // 竞选者持有日志落后于当前节点
        vote_reply->set_term(current_term_);
        vote_reply->set_vote_granted(false);
        vote_reply->set_vote_state(kNormal);
        return;
    }
    // 判断是否已经投票给别的节点
    if (voted_for_ != -1 && voted_for_ != candidate_id) {
        vote_reply->set_term(current_term_);
        vote_reply->set_vote_granted(false);
        vote_reply->set_vote_state(kHaveVoted);
        return;
    }
    /**符合当选leader条件，投票给该竞选者 */
    last_reset_election_time_ = system_clock::now(); // 选举时间更新
    status_ = FOLLOWER;
    voted_for_ = candidate_id;
    current_term_ = candidate_term;
    vote_reply->set_term(current_term_);
    vote_reply->set_vote_granted(true);
    vote_reply->set_vote_state(kNormal);
}
