#include "myrpc_connect_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        std::exit(1);
    }
}

class LocalTcpServer {
  public:
    LocalTcpServer() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        Check(listen_fd_ >= 0, "create tcp listen socket");

        int opt = 1;
        Check(setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0, "set SO_REUSEADDR");

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        Check(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1, "parse loopback ip");
        Check(bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "bind tcp server");
        Check(listen(listen_fd_, 64) == 0, "listen tcp server");

        socklen_t addr_len = sizeof(addr);
        Check(getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0, "read tcp server port");
        port_ = ntohs(addr.sin_port);

        accept_thread_ = std::thread([this] { AcceptLoop(); });
    }

    ~LocalTcpServer() {
        stop_.store(true);
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
        close(listen_fd_);
        std::scoped_lock lock(mtx_);
        for (int fd : accepted_fds_) {
            close(fd);
        }
    }

    uint16_t Port() const {
        return port_;
    }

  private:
    void AcceptLoop() {
        while (!stop_.load()) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(listen_fd_, &read_fds);

            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 10000;
            int ready = select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
            if (ready <= 0) {
                continue;
            }

            int fd = accept(listen_fd_, nullptr, nullptr);
            if (fd >= 0) {
                std::scoped_lock lock(mtx_);
                accepted_fds_.push_back(fd);
            }
        }
    }

    int listen_fd_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread accept_thread_;
    std::mutex mtx_;
    std::vector<int> accepted_fds_;
};

bool IsTcpSocket(int fd) {
    int socket_type = 0;
    socklen_t len = sizeof(socket_type);
    return getsockopt(fd, SOL_SOCKET, SO_TYPE, &socket_type, &len) == 0 && socket_type == SOCK_STREAM;
}

void TestBorrowConnectionBuildsTcpPool() {
    LocalTcpServer server;

    int fd = RpcConnectPool::GetInstance().BorrowConnection("127.0.0.1", server.Port());
    Check(fd >= 0, "borrow connection from local tcp server");
    Check(IsTcpSocket(fd), "borrowed connection should be a TCP socket");

    RpcConnectPool::GetInstance().ReturnConnection("127.0.0.1", server.Port(), fd);
}

void TestReturnedConnectionCanBeBorrowedAgain() {
    LocalTcpServer server;
    RpcConnectPool& pool = RpcConnectPool::GetInstance();

    int fd = pool.BorrowConnection("127.0.0.1", server.Port());
    Check(fd >= 0, "borrow connection before return");
    pool.ReturnConnection("127.0.0.1", server.Port(), fd);

    bool saw_returned_fd = false;
    std::vector<int> borrowed_fds;
    for (int i = 0; i < 16; ++i) {
        int next_fd = pool.BorrowConnection("127.0.0.1", server.Port());
        Check(next_fd >= 0, "borrow returned or warmed connection");
        if (next_fd == fd) {
            saw_returned_fd = true;
        }
        borrowed_fds.push_back(next_fd);
    }

    for (int borrowed_fd : borrowed_fds) {
        pool.ReturnConnection("127.0.0.1", server.Port(), borrowed_fd);
    }
    Check(saw_returned_fd, "returned connection should become available for later borrows");
}

void TestBadConnectionIsClosedAndNotReused() {
    LocalTcpServer server;
    RpcConnectPool& pool = RpcConnectPool::GetInstance();

    int bad_fd = pool.BorrowConnection("127.0.0.1", server.Port());
    Check(bad_fd >= 0, "borrow connection marked bad later");
    pool.ReturnConnection("127.0.0.1", server.Port(), bad_fd, true);

    int error = 0;
    socklen_t len = sizeof(error);
    Check(getsockopt(bad_fd, SOL_SOCKET, SO_ERROR, &error, &len) != 0, "bad connection should be closed");
}

void TestUnknownPoolReturnClosesFd() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    Check(fd >= 0, "create fd for unknown pool return");

    RpcConnectPool::GetInstance().ReturnConnection("127.0.0.1", 1, fd);

    int error = 0;
    socklen_t len = sizeof(error);
    Check(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) != 0, "unknown pool return should close fd");
}

void TestBorrowConnectionFailureReturnsMinusOne() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    Check(listen_fd >= 0, "create temporary socket for unused port");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    Check(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1, "parse loopback ip for unused port");
    Check(bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "bind temporary socket");

    socklen_t addr_len = sizeof(addr);
    Check(getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0, "read unused port");
    uint16_t unused_port = ntohs(addr.sin_port);
    close(listen_fd);

    int fd = RpcConnectPool::GetInstance().BorrowConnection("127.0.0.1", unused_port);
    Check(fd == -1, "borrow from unused port should fail");
}

} // namespace

int main() {
    TestBorrowConnectionBuildsTcpPool();
    TestReturnedConnectionCanBeBorrowedAgain();
    TestBadConnectionIsClosedAndNotReused();
    TestUnknownPoolReturnClosesFd();
    TestBorrowConnectionFailureReturnsMinusOne();

    std::cout << "myrpc_connect_pool_test_passed" << std::endl;
    return 0;
}
