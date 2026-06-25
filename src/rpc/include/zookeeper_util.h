#pragma once
#include <mutex>
#include <string>
#include <vector>
#define THREADED
#include <zookeeper/zookeeper.h>

class ZkClient {
  private:
    mutable std::mutex mtx_;
    bool is_connected_;
    bool is_closing_;
    std::string connstr_;
    zhandle_t* zhandle_;

    static void SessionWatcher(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx);
    void OnSessionEvent(int state);
    bool EnsureConnected();
    bool EnsureParentPath(const std::string& path);

  public:
    ZkClient();
    ~ZkClient();
    void Start();
    void Create(std::string const& path, std::string const& data, int state = 0);
    std::string GetData(std::string const& path);
    std::vector<std::string> GetChildren(std::string const& path, watcher_fn fn, void* cbContext);

    bool IsConnected() const;
};
