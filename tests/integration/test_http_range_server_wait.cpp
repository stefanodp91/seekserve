// Responses that have to wait for pieces (app BUG-83): the body must not end
// short while the client waits, the wait must end when the client leaves or
// the server stops, and the scheduler must follow the newest reader.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/byte_source.hpp"
#include "seekserve/config.hpp"
#include "seekserve/http_range_server.hpp"
#include "seekserve/piece_availability.hpp"
#include "seekserve/types.hpp"

namespace seekserve {
namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;
using namespace std::chrono_literals;

// Pieces as large as the server's read chunk, so each chunk is one piece.
static constexpr int kPieceLength = 65536;
static constexpr int kNumPieces = 5;
static constexpr int kFileSize = kPieceLength * kNumPieces;
static const std::string kTarget = "/stream/abc123/0?token=testtoken";

static std::uint8_t test_byte(std::int64_t pos) {
    return static_cast<std::uint8_t>((pos * 7 + 13) % 256);
}

// Polls `done` until it holds or `limit` passes.
template <typename Pred>
static bool eventually(Pred done, std::chrono::milliseconds limit = 3000ms) {
    auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        if (done()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return done();
}

// Shuts the socket down if the test is still reading after `limit`, so a
// server that regresses fails the test instead of hanging it.
class ReadDeadline {
public:
    ReadDeadline(tcp::socket& socket, std::chrono::seconds limit)
        : fd_(socket.native_handle()) {
        thread_ = std::thread([this, limit] {
            std::unique_lock lock(mu_);
            if (!cv_.wait_for(lock, limit, [this] { return done_; })) {
                ::shutdown(fd_, SHUT_RDWR);
            }
        });
    }
    ~ReadDeadline() {
        {
            std::lock_guard lock(mu_);
            done_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

private:
    int fd_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_ = false;
    std::thread thread_;
};

class HttpRangeServerWaitTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path() / "seekserve_test_http_wait";
        std::filesystem::create_directories(tmp_dir_);
        file_path_ = (tmp_dir_ / "test.bin").string();

        std::ofstream out(file_path_, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        for (int i = 0; i < kFileSize; ++i) {
            auto b = test_byte(i);
            out.write(reinterpret_cast<const char*>(&b), 1);
        }
        out.close();

        fs_.set_piece_length(kPieceLength);
        fs_.add_file("test.bin", kFileSize);
        fs_.set_num_pieces(kNumPieces);

        mapper_ = std::make_unique<ByteRangeMapper>(fs_, 0);
        avail_ = std::make_unique<PieceAvailabilityIndex>(kNumPieces, kPieceLength, kPieceLength);
    }

    // The source's own timeout is short on purpose: the server must keep
    // waiting past it.
    void start_server(ServerConfig config = {}) {
        source_ = std::make_shared<ByteSource>(
            lt::torrent_handle{}, 0, file_path_, *mapper_, *avail_, 100ms);

        server_ = std::make_shared<HttpRangeServer>(ioc_, config);
        server_->set_byte_source(source_, "abc123", 0);
        server_->set_auth_token("testtoken");
        server_->set_range_callback(
            [this](const ByteRange& range, const TorrentId&, FileIndex) {
                {
                    std::lock_guard lock(calls_mu_);
                    range_starts_.push_back(range.start);
                }
                if (after_report_) after_report_(range.start);
            });

        auto result = server_->start(0);
        ASSERT_TRUE(result.ok()) << result.error().message();
        port_ = result.value();
        io_thread_ = std::thread([this] { ioc_.run(); });
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
            // Connection threads own sockets of ioc_.
            eventually([&] { return server_->active_connections() == 0; });
        }
        ioc_.stop();
        if (io_thread_.joinable()) io_thread_.join();
        std::filesystem::remove_all(tmp_dir_);
    }

    void complete_pieces(int first, int last) {
        for (int p = first; p <= last; ++p) avail_->mark_complete(p);
        if (source_) source_->notify_piece_complete();
    }

    std::vector<std::int64_t> range_starts() {
        std::lock_guard lock(calls_mu_);
        return range_starts_;
    }

    // Opens a connection and sends a GET for `range` ("" = whole file).
    tcp::socket send_get(net::io_context& ioc, const std::string& range) {
        tcp::socket socket(ioc);
        socket.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), port_));
        std::string req = "GET " + kTarget + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
        if (!range.empty()) req += "Range: " + range + "\r\n";
        req += "Connection: close\r\n\r\n";
        net::write(socket, net::buffer(req));
        return socket;
    }

    // Reads the response to the end; `ec` tells whether the body was whole.
    http::response<http::string_body> read_response(tcp::socket& socket,
                                                    boost::system::error_code& ec) {
        ReadDeadline deadline(socket, 10s);
        beast::flat_buffer buffer;
        http::response_parser<http::string_body> parser;
        parser.body_limit(kFileSize + 1);
        http::read(socket, buffer, parser, ec);
        return parser.release();
    }

    // Reads the status line and headers; returns the body bytes read with them.
    std::string read_headers(tcp::socket& socket) {
        ReadDeadline deadline(socket, 10s);
        net::streambuf buf;
        auto n = net::read_until(socket, buf, "\r\n\r\n");
        std::string all(net::buffers_begin(buf.data()), net::buffers_end(buf.data()));
        return all.substr(n);
    }

    std::filesystem::path tmp_dir_;
    std::string file_path_;
    lt::file_storage fs_;
    std::unique_ptr<ByteRangeMapper> mapper_;
    std::unique_ptr<PieceAvailabilityIndex> avail_;
    std::shared_ptr<ByteSource> source_;

    net::io_context ioc_;
    std::shared_ptr<HttpRangeServer> server_;
    std::uint16_t port_ = 0;
    std::thread io_thread_;

    std::mutex calls_mu_;
    std::vector<std::int64_t> range_starts_;
    // Runs after each report is recorded, on the reporting thread.
    std::function<void(std::int64_t)> after_report_;
};

// Before BUG-83 the body ended after the source's timeout (30 s on the
// phone) and the player took the short body for the end of the video.
TEST_F(HttpRangeServerWaitTest, KeepsWaitingForPiecesWhileTheClientStays) {
    complete_pieces(0, 1);
    start_server();

    std::thread late_pieces([this] {
        std::this_thread::sleep_for(1500ms);  // three wait slices, 15 source timeouts
        complete_pieces(2, 4);
    });

    net::io_context client;
    auto socket = send_get(client, "bytes=0-");
    boost::system::error_code ec;
    auto res = read_response(socket, ec);
    late_pieces.join();

    ASSERT_FALSE(ec) << ec.message();
    EXPECT_EQ(res.result(), http::status::partial_content);
    ASSERT_EQ(static_cast<int>(res.body().size()), kFileSize);
    for (int i = 0; i < kFileSize; ++i) {
        ASSERT_EQ(static_cast<std::uint8_t>(res.body()[i]), test_byte(i)) << "byte " << i;
    }
}

TEST_F(HttpRangeServerWaitTest, StopsWaitingWhenTheClientLeaves) {
    start_server();

    net::io_context client;
    auto socket = send_get(client, "bytes=131072-");
    read_headers(socket);
    ASSERT_TRUE(eventually([&] { return server_->active_connections() == 1; }));

    socket.close();

    EXPECT_TRUE(eventually([&] { return server_->active_connections() == 0; }))
        << "the connection thread kept waiting for a client that left";
}

TEST_F(HttpRangeServerWaitTest, StopEndsAWaitingResponse) {
    start_server();

    net::io_context client;
    auto socket = send_get(client, "bytes=131072-");
    read_headers(socket);
    ASSERT_TRUE(eventually([&] { return server_->active_connections() == 1; }));

    server_->stop();

    EXPECT_TRUE(eventually([&] { return server_->active_connections() == 0; }));
}

TEST_F(HttpRangeServerWaitTest, StallTimeoutEndsTheResponseWhenSet) {
    ServerConfig config;
    config.stall_timeout = 1s;
    start_server(config);

    auto started = std::chrono::steady_clock::now();
    net::io_context client;
    auto socket = send_get(client, "bytes=131072-");
    boost::system::error_code ec;
    auto res = read_response(socket, ec);
    auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_EQ(ec, http::error::partial_message);
    EXPECT_TRUE(res.body().empty());
    EXPECT_GE(elapsed, 1s);
    EXPECT_LT(elapsed, 5s);
}

// A single request can run to the end of the file: the scheduler has to hear
// where its reader is, not only where it started.
TEST_F(HttpRangeServerWaitTest, ReaderProgressMovesTheScheduler) {
    complete_pieces(0, kNumPieces - 1);
    start_server();

    net::io_context client;
    auto socket = send_get(client, "");
    boost::system::error_code ec;
    auto res = read_response(socket, ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_EQ(static_cast<int>(res.body().size()), kFileSize);

    std::vector<std::int64_t> expected{0};  // the request itself
    for (int p = 1; p < kNumPieces; ++p) expected.push_back(std::int64_t{p} * kPieceLength);
    EXPECT_EQ(range_starts(), expected);
}

// After a seek the player opens a new request; the old one, still waiting,
// must not pull the scheduler back when its piece arrives.
TEST_F(HttpRangeServerWaitTest, OnlyTheNewestReaderMovesTheScheduler) {
    complete_pieces(1, 1);
    start_server();

    net::io_context client;
    auto old_reader = send_get(client, "bytes=65536-");
    read_headers(old_reader);
    // It sends piece 1, reports piece 2 and waits for it.
    ASSERT_TRUE(eventually([&] { return range_starts().size() == 2; }));

    auto new_reader = send_get(client, "bytes=262144-");
    read_headers(new_reader);
    ASSERT_TRUE(eventually([&] { return range_starts().size() == 3; }));

    // Piece 2 arrives: the old reader sends it and moves on to piece 3
    // without reporting, since the new reader owns the position.
    complete_pieces(2, 2);
    std::this_thread::sleep_for(700ms);

    EXPECT_EQ(range_starts(), (std::vector<std::int64_t>{65536, 131072, 262144}));

    old_reader.close();
    new_reader.close();
    EXPECT_TRUE(eventually([&] { return server_->active_connections() == 0; }));
}

// The old reader crosses a piece while the new request's first report is
// still being applied: its report must not land after the new one and pull
// the scheduler back to a connection the player has left.
TEST_F(HttpRangeServerWaitTest, OldReaderCannotReportAfterTheNewReadersFirstReport) {
    complete_pieces(0, 0);
    after_report_ = [](std::int64_t start) {
        if (start == 262144) std::this_thread::sleep_for(400ms);  // a slow engine lock
    };
    start_server();

    net::io_context client;
    auto old_reader = send_get(client, "bytes=0-");
    read_headers(old_reader);
    // It sends piece 0, reports piece 1 and waits for it.
    ASSERT_TRUE(eventually([&] { return range_starts().size() == 2; }));

    std::thread piece_arrives([this] {
        std::this_thread::sleep_for(100ms);  // during the new reader's report
        complete_pieces(1, 1);
    });
    auto new_reader = send_get(client, "bytes=262144-");
    read_headers(new_reader);
    piece_arrives.join();
    std::this_thread::sleep_for(700ms);  // the old reader has moved to piece 2

    auto starts = range_starts();
    ASSERT_FALSE(starts.empty());
    EXPECT_EQ(starts.back(), 262144);
    EXPECT_EQ(std::count(starts.begin(), starts.end(), 131072), 0);

    old_reader.close();
    new_reader.close();
}

// A waiting reader keeps its position with the scheduler: anything that moved
// it meanwhile (a finished reader, another request) is undone.
TEST_F(HttpRangeServerWaitTest, WaitingOwnerReportsItsPositionAgain) {
    start_server();

    net::io_context client;
    auto reader = send_get(client, "bytes=65536-");
    read_headers(reader);
    std::this_thread::sleep_for(2500ms);

    auto starts = range_starts();
    EXPECT_GE(std::count(starts.begin(), starts.end(), 65536), 2);
    reader.close();
}

TEST_F(HttpRangeServerWaitTest, HeadRequestLeavesTheSchedulerAlone) {
    start_server();

    net::io_context client;
    tcp::socket socket(client);
    socket.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), port_));
    std::string req = "HEAD " + kTarget + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                      "Range: bytes=131072-\r\nConnection: close\r\n\r\n";
    net::write(socket, net::buffer(req));
    read_headers(socket);

    EXPECT_TRUE(range_starts().empty());
}

// A range that starts late in a downloaded piece gets that piece's bytes
// before the wait for the next one.
TEST_F(HttpRangeServerWaitTest, SendsWhatIsOnDiskBeforeWaitingForTheNextPiece) {
    complete_pieces(0, 0);
    start_server();

    net::io_context client;
    auto socket = send_get(client, "bytes=100-");
    std::string body = read_headers(socket);
    {
        ReadDeadline deadline(socket, 3s);
        std::vector<char> rest(static_cast<std::size_t>(kPieceLength - 100) - body.size());
        boost::system::error_code ec;
        net::read(socket, net::buffer(rest), ec);
        ASSERT_FALSE(ec) << ec.message();
        body.append(rest.begin(), rest.end());
    }
    ASSERT_EQ(static_cast<int>(body.size()), kPieceLength - 100);
    EXPECT_EQ(static_cast<std::uint8_t>(body[0]), test_byte(100));
    socket.close();
}

// Bytes the client sent after its request must not hide its FIN.
TEST_F(HttpRangeServerWaitTest, NoticesAClientThatSentMoreBytesAndLeft) {
    start_server();

    net::io_context client;
    auto socket = send_get(client, "bytes=131072-");
    read_headers(socket);
    net::write(socket, net::buffer(std::string("unexpected bytes after the request")));
    ASSERT_TRUE(eventually([&] { return server_->active_connections() == 1; }));

    socket.close();

    EXPECT_TRUE(eventually([&] { return server_->active_connections() == 0; }));
}

// A client that connected and sent nothing holds a thread in the request
// read; stop() must still end it.
TEST_F(HttpRangeServerWaitTest, StopEndsAnIdleConnection) {
    start_server();

    net::io_context client;
    tcp::socket socket(client);
    socket.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), port_));
    ASSERT_TRUE(eventually([&] { return server_->active_connections() == 1; }));

    server_->stop();

    EXPECT_TRUE(eventually([&] { return server_->active_connections() == 0; }));
}

}  // namespace
}  // namespace seekserve
