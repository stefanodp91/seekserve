// A SOCKS5 proxy for tests that connects only to 127.0.0.1: an engine reaches
// a seeder on this machine through it, as it reaches peers through Tor in the
// app, so nothing leaves the machine.

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace seekserve {
namespace test {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

inline bool send_all(int fd, const unsigned char* data, std::size_t size) {
    while (size > 0) {
        auto n = ::send(fd, data, size, kSendFlags);
        if (n <= 0) return false;
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

inline bool recv_all(int fd, unsigned char* data, std::size_t size) {
    while (size > 0) {
        auto n = ::recv(fd, data, size, 0);
        if (n <= 0) return false;
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

// CONNECT with an IPv4 address, no authentication; anything else is refused,
// UDP ASSOCIATE included.
class LoopbackSocks5 {
public:
    LoopbackSocks5() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof(addr);
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), len) == 0
            && ::listen(listen_fd_, 16) == 0
            && ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
            port_ = ntohs(addr.sin_port);
        }
        accept_thread_ = std::thread([this] { accept_loop(); });
    }

    ~LoopbackSocks5() {
        stopping_ = true;
        accept_thread_.join();
        {
            std::lock_guard lock(mu_);
            for (int fd : fds_) ::shutdown(fd, SHUT_RDWR);
        }
        for (auto& t : threads_) t.join();
        for (int fd : fds_) ::close(fd);
        ::close(listen_fd_);
    }

    int port() const { return port_; }

    // Slows down what each connection carries back to the one that opened
    // it (the pieces a seeder sends), so a test can act while a download is
    // under way; 0, the default, is no limit.
    void limit_download(int bytes_per_second) { download_limit_ = bytes_per_second; }

private:
    void track(int fd) {
        std::lock_guard lock(mu_);
        fds_.push_back(fd);
    }

    void accept_loop() {
        while (!stopping_) {
            pollfd p{listen_fd_, POLLIN, 0};
            if (::poll(&p, 1, 100) <= 0) continue;
            int client = ::accept(listen_fd_, nullptr, nullptr);
            if (client < 0) continue;
            track(client);
            std::lock_guard lock(mu_);
            threads_.emplace_back([this, client] { serve(client); });
        }
    }

    static void reply(int fd, unsigned char code) {
        const unsigned char r[] = {5, code, 0, 1, 0, 0, 0, 0, 0, 0};
        send_all(fd, r, sizeof(r));
    }

    // Both legs are shut down when either ends: a half-open leg would keep
    // the seeder's connection from 127.0.0.1 alive, and it would refuse the
    // engine's next one as a duplicate peer.
    void serve(int client) {
        int upstream = -1;
        relay(client, upstream);
        ::shutdown(client, SHUT_RDWR);
        if (upstream >= 0) ::shutdown(upstream, SHUT_RDWR);
    }

    void relay(int client, int& upstream) {
        unsigned char buf[512];
        if (!recv_all(client, buf, 2) || buf[0] != 5 || !recv_all(client, buf + 2, buf[1])) return;
        const unsigned char no_auth[] = {5, 0};
        if (!send_all(client, no_auth, sizeof(no_auth))) return;

        // VER CMD RSV ATYP, then an IPv4 address and a port.
        if (!recv_all(client, buf, 4)) return;
        if (buf[1] != 1) return reply(client, 7);  // command not supported
        if (buf[3] != 1) return reply(client, 8);  // address type not supported
        if (!recv_all(client, buf + 4, 6)) return;
        sockaddr_in target{};
        target.sin_family = AF_INET;
        std::memcpy(&target.sin_addr, buf + 4, 4);
        std::memcpy(&target.sin_port, buf + 8, 2);
        if (target.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) return reply(client, 2);  // not allowed

        upstream = ::socket(AF_INET, SOCK_STREAM, 0);
        track(upstream);
        if (::connect(upstream, reinterpret_cast<sockaddr*>(&target), sizeof(target)) != 0) {
            return reply(client, 5);  // connection refused
        }
        reply(client, 0);

        pollfd p[2] = {{client, POLLIN, 0}, {upstream, POLLIN, 0}};
        while (!stopping_) {
            if (::poll(p, 2, 100) <= 0) continue;
            for (int i = 0; i < 2; ++i) {
                if (!(p[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                auto n = ::recv(p[i].fd, buf, sizeof(buf), 0);
                if (n <= 0 || !send_all(p[1 - i].fd, buf, static_cast<std::size_t>(n))) return;
                const int limit = download_limit_;
                if (i == 1 && limit > 0) {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(1'000'000LL * n / limit));
                }
            }
        }
    }

    int listen_fd_ = -1;
    int port_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<int> download_limit_{0};
    std::thread accept_thread_;
    std::mutex mu_;
    std::vector<int> fds_;
    std::vector<std::thread> threads_;
};

}  // namespace test
}  // namespace seekserve
