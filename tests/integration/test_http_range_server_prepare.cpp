// A source that has to read part of its file before it knows its size (app
// WORK-37 step 7, a file rebuilt with subtitles): the server waits for it
// before it answers, tells the scheduler what it waits for, and reports the
// reader's position in the underlying file.

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>

#include "seekserve/config.hpp"
#include "seekserve/http_range_server.hpp"

namespace seekserve {
namespace {

using namespace std::chrono_literals;

constexpr std::int64_t kSize = 50000;
constexpr std::int64_t kShift = 1000;  // where its bytes come from in the "underlying" file

// Ready once the test says so; byte i is (i * 3) mod 256, and comes from
// byte i + kShift of the underlying file.
class SlowSource final : public StreamSource {
public:
    Result<std::vector<std::uint8_t>> read(std::int64_t offset, std::int64_t len,
                                           std::chrono::milliseconds) override {
        if (!ready) return make_error_code(errc::timeout_waiting_for_piece);
        std::vector<std::uint8_t> out;
        for (auto i = offset; i < std::min(offset + len, kSize); ++i) out.push_back(static_cast<std::uint8_t>(i * 3));
        return out;
    }
    PieceIndex piece_at(std::int64_t offset) const override { return static_cast<PieceIndex>(offset / 16384); }
    std::int64_t bytes_to_piece_end(std::int64_t) const override { return -1; }
    void cancel() override {}
    std::int64_t file_size() const override { return ready ? kSize : 0; }
    Result<void> prepare(std::chrono::milliseconds wait) override {
        ++prepares;
        if (ready) return {};
        std::this_thread::sleep_for(std::min(wait, 50ms));
        return make_error_code(errc::timeout_waiting_for_piece);
    }
    std::optional<ByteRange> preparing_range() const override { return ByteRange{900000, 999999}; }
    std::int64_t file_offset(std::int64_t offset) const override { return offset + kShift; }

    std::atomic<bool> ready{false};
    std::atomic<int> prepares{0};
};

std::string get(std::uint16_t port, const std::string& range, const char* method = "GET") {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return "";
    }
    std::string req = std::string(method) + " /stream/abc/0?token=t HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: " +
                      range + "\r\nConnection: close\r\n\r\n";
    ::send(fd, req.data(), req.size(), 0);
    std::string out;
    char buf[65536];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) out.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    return out;
}

class HttpRangeServerPrepareTest : public ::testing::Test {
protected:
    void SetUp() override {
        ServerConfig config;
        config.bind_address = "127.0.0.1";
        server_ = std::make_shared<HttpRangeServer>(ioc_, config);
        server_->set_auth_token("t");
        source_ = std::make_shared<SlowSource>();
        server_->set_byte_source(source_, "abc", 0, "film.mkv");
        server_->set_range_callback([this](const ByteRange& r, const TorrentId&, FileIndex) {
            std::lock_guard lock(mu_);
            ranges_.push_back(r);
        });
        auto port = server_->start(0);
        ASSERT_TRUE(port);
        port_ = port.value();
        io_thread_ = std::thread([this] { ioc_.run(); });
    }
    void TearDown() override {
        server_->stop();
        // Connection threads own sockets of ioc_.
        auto until = std::chrono::steady_clock::now() + 3s;
        while (server_->active_connections() > 0 && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(20ms);
        }
        ioc_.stop();
        if (io_thread_.joinable()) io_thread_.join();
    }

    std::vector<ByteRange> ranges() {
        std::lock_guard lock(mu_);
        return ranges_;
    }

    boost::asio::io_context ioc_;
    std::shared_ptr<HttpRangeServer> server_;
    std::shared_ptr<SlowSource> source_;
    std::uint16_t port_ = 0;
    std::thread io_thread_;
    std::mutex mu_;
    std::vector<ByteRange> ranges_;
};

TEST_F(HttpRangeServerPrepareTest, ItAnswersOnceTheSourceIsPreparedWithItsSize) {
    std::thread later([this] {
        std::this_thread::sleep_for(2500ms);
        source_->ready = true;
    });
    auto reply = get(port_, "bytes=100-");
    later.join();
    ASSERT_NE(reply.find("206"), std::string::npos) << reply.substr(0, 200);
    EXPECT_NE(reply.find("bytes 100-49999/50000"), std::string::npos) << reply.substr(0, 300);
    auto body = reply.substr(reply.find("\r\n\r\n") + 4);
    ASSERT_EQ(body.size(), static_cast<std::size_t>(kSize - 100));
    EXPECT_EQ(static_cast<std::uint8_t>(body[0]), static_cast<std::uint8_t>(300));
    EXPECT_GT(source_->prepares.load(), 1) << "it asked again while the source waited";

    auto seen = ranges();
    bool told_preparing = false, told_reading = false;
    for (const auto& r : seen) {
        told_preparing |= r.start == 900000 && r.end == 999999;
        told_reading |= r.start == 100 + kShift && r.end == kSize - 1 + kShift;
    }
    EXPECT_TRUE(told_preparing) << "the scheduler hears what the source waits for";
    EXPECT_TRUE(told_reading) << "and the reader's place in the underlying file";
}

TEST_F(HttpRangeServerPrepareTest, AHeadAlsoWaitsForTheSize) {
    source_->ready = true;
    auto reply = get(port_, "bytes=0-9", "HEAD");
    EXPECT_NE(reply.find("bytes 0-9/50000"), std::string::npos) << reply;
}

TEST_F(HttpRangeServerPrepareTest, AClientThatLeavesWhileItPreparesEndsTheWait) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    std::string req = "GET /stream/abc/0?token=t HTTP/1.1\r\nHost: x\r\nRange: bytes=0-\r\n\r\n";
    ::send(fd, req.data(), req.size(), 0);
    std::this_thread::sleep_for(700ms);
    ::close(fd);
    auto until = std::chrono::steady_clock::now() + 3s;
    while (server_->active_connections() > 0 && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_EQ(server_->active_connections(), 0);
}

}  // namespace
}  // namespace seekserve
