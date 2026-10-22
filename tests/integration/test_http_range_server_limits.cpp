// Connections that hold a stream slot without moving (app BUG-88): a client
// that sends no request, a reader a newer request of the same stream left
// behind, a newest reader that stopped reading. Before, the server waited for
// each of them without limit, and four of them (max_concurrent_streams) kept
// every new request out: the TV of app WORK-37 holds up to four connections
// to a file while it seeks.

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
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
using tcp = net::ip::tcp;
using namespace std::chrono_literals;

// Far larger than what the socket buffers of a client that stopped reading
// can take, so the server's write blocks.
static constexpr int kPieceLength = 65536;
static constexpr int kNumPieces = 256;
static constexpr std::int64_t kFileSize = std::int64_t{kPieceLength} * kNumPieces;
static const std::string kTarget = "/stream/abc123/0?token=testtoken";

template <typename Pred>
static bool eventually(Pred done, std::chrono::milliseconds limit = 5000ms) {
    auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        if (done()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return done();
}

class HttpRangeServerLimitsTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path() / "seekserve_test_http_limits";
        std::filesystem::create_directories(tmp_dir_);
        file_path_ = (tmp_dir_ / "test.bin").string();

        std::ofstream out(file_path_, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        std::vector<char> piece(kPieceLength, 'x');
        for (int p = 0; p < kNumPieces; ++p) out.write(piece.data(), kPieceLength);
        out.close();

        fs_.set_piece_length(kPieceLength);
        fs_.add_file("test.bin", kFileSize);
        fs_.set_num_pieces(kNumPieces);

        mapper_ = std::make_unique<ByteRangeMapper>(fs_, 0);
        avail_ = std::make_unique<PieceAvailabilityIndex>(kNumPieces, kPieceLength, kPieceLength);
        for (int p = 0; p < kNumPieces; ++p) avail_->mark_complete(p);
    }

    void start_server(ServerConfig config) {
        source_ = std::make_shared<ByteSource>(
            lt::torrent_handle{}, 0, file_path_, *mapper_, *avail_, 100ms);
        server_ = std::make_shared<HttpRangeServer>(ioc_, config);
        server_->set_byte_source(source_, "abc123", 0);
        server_->set_auth_token("testtoken");
        auto result = server_->start(0);
        ASSERT_TRUE(result.ok()) << result.error().message();
        port_ = result.value();
        io_thread_ = std::thread([this] { ioc_.run(); });
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
            eventually([&] { return server_->active_connections() == 0; });
        }
        ioc_.stop();
        if (io_thread_.joinable()) io_thread_.join();
        std::filesystem::remove_all(tmp_dir_);
    }

    // A connection with a small receive buffer, so that a client that stops
    // reading soon stops the server's writes.
    tcp::socket connect() {
        tcp::socket socket(client_);
        socket.open(tcp::v4());
        socket.set_option(net::socket_base::receive_buffer_size(16 * 1024));
        socket.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), port_));
        return socket;
    }

    tcp::socket send_get(const std::string& range) {
        auto socket = connect();
        std::string req = "GET " + kTarget + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Range: " + range + "\r\nConnection: close\r\n\r\n";
        net::write(socket, net::buffer(req));
        return socket;
    }

    // The status line, or "" when the server closed the connection first.
    std::string read_status(tcp::socket& socket) {
        net::streambuf buf;
        boost::system::error_code ec;
        net::read_until(socket, buf, "\r\n", ec);
        if (ec) return "";
        std::string all(net::buffers_begin(buf.data()), net::buffers_end(buf.data()));
        return all.substr(0, all.find("\r\n"));
    }

    // True once the server has closed `socket`: what it had sent is read and
    // dropped until the end of the stream.
    static bool closed_by_server(tcp::socket& socket) {
        socket.non_blocking(true);
        std::vector<char> buf(65536);
        for (;;) {
            boost::system::error_code ec;
            socket.read_some(net::buffer(buf), ec);
            if (ec == net::error::would_block) return false;
            if (ec) return true;
        }
    }

    std::filesystem::path tmp_dir_;
    std::string file_path_;
    lt::file_storage fs_;
    std::unique_ptr<ByteRangeMapper> mapper_;
    std::unique_ptr<PieceAvailabilityIndex> avail_;
    std::shared_ptr<ByteSource> source_;

    net::io_context ioc_;
    net::io_context client_;
    std::shared_ptr<HttpRangeServer> server_;
    std::uint16_t port_ = 0;
    std::thread io_thread_;
};

TEST_F(HttpRangeServerLimitsTest, AConnectionThatSendsNothingIsClosedAfterTheReadTimeout) {
    ServerConfig config;
    config.read_timeout = 1s;
    start_server(config);

    auto socket = connect();
    ASSERT_TRUE(eventually([&] { return server_->active_connections() == 1; }));
    auto started = std::chrono::steady_clock::now();

    EXPECT_TRUE(eventually([&] { return server_->active_connections() == 0; }))
        << "a connection without a request kept its slot";
    EXPECT_GE(std::chrono::steady_clock::now() - started, 800ms);
    EXPECT_TRUE(eventually([&] { return closed_by_server(socket); }));
}

TEST_F(HttpRangeServerLimitsTest, AHalfSentRequestIsClosedAfterTheReadTimeout) {
    ServerConfig config;
    config.read_timeout = 1s;
    start_server(config);

    auto socket = connect();
    net::write(socket, net::buffer(std::string("GET /stream/abc123/0?tok")));

    EXPECT_TRUE(eventually([&] { return server_->active_connections() == 0; }))
        << "a request that never ended kept its slot";
}

// After a seek the TV leaves its old connection open without reading it.
TEST_F(HttpRangeServerLimitsTest, AReaderLeftBehindThatStopsReadingIsClosed) {
    ServerConfig config;
    config.stale_reader_timeout = 1s;
    start_server(config);

    auto old_reader = send_get("bytes=0-");
    ASSERT_EQ(read_status(old_reader), "HTTP/1.1 206 Partial Content");
    auto new_reader = send_get("bytes=8388608-");
    ASSERT_EQ(read_status(new_reader), "HTTP/1.1 206 Partial Content");
    ASSERT_TRUE(eventually([&] { return server_->active_connections() == 2; }));

    EXPECT_TRUE(eventually([&] { return server_->active_connections() == 1; }))
        << "the reader left behind kept its slot";
    EXPECT_TRUE(eventually([&] { return closed_by_server(old_reader); }));

    // The newest reader is a paused player: it keeps its connection.
    std::this_thread::sleep_for(2500ms);
    EXPECT_EQ(server_->active_connections(), 1);
    EXPECT_FALSE(closed_by_server(new_reader));
}

// Without a stale_reader_timeout a reader left behind waits as before.
TEST_F(HttpRangeServerLimitsTest, AReaderLeftBehindStaysWithoutAStaleReaderTimeout) {
    start_server(ServerConfig{});

    auto old_reader = send_get("bytes=0-");
    ASSERT_EQ(read_status(old_reader), "HTTP/1.1 206 Partial Content");
    auto new_reader = send_get("bytes=8388608-");
    ASSERT_EQ(read_status(new_reader), "HTTP/1.1 206 Partial Content");

    std::this_thread::sleep_for(2000ms);
    EXPECT_EQ(server_->active_connections(), 2);
}

TEST_F(HttpRangeServerLimitsTest, ConnectionTimeoutEndsANewestReaderThatStoppedWhenSet) {
    ServerConfig config;
    config.connection_timeout = 1s;
    start_server(config);

    auto reader = send_get("bytes=0-");
    ASSERT_EQ(read_status(reader), "HTTP/1.1 206 Partial Content");
    ASSERT_TRUE(eventually([&] { return server_->active_connections() == 1; }));

    EXPECT_TRUE(eventually([&] { return server_->active_connections() == 0; }));
}

// With every slot taken, the newest request gets in in place of a reader a
// newer request left behind, instead of being turned away.
TEST_F(HttpRangeServerLimitsTest, TheNewestRequestTakesTheSlotOfAReaderLeftBehind) {
    ServerConfig config;
    config.max_concurrent_streams = 2;
    start_server(config);

    auto first = send_get("bytes=0-");
    ASSERT_EQ(read_status(first), "HTTP/1.1 206 Partial Content");
    auto second = send_get("bytes=4194304-");
    ASSERT_EQ(read_status(second), "HTTP/1.1 206 Partial Content");
    ASSERT_TRUE(eventually([&] { return server_->active_connections() == 2; }));

    auto third = send_get("bytes=8388608-");
    EXPECT_EQ(read_status(third), "HTTP/1.1 206 Partial Content")
        << "the newest request was turned away";
    EXPECT_TRUE(eventually([&] { return closed_by_server(first); }))
        << "the oldest reader left behind should have made room";
    EXPECT_FALSE(closed_by_server(second));
}

// Connections that are nobody's reader left behind are not pushed out: a
// new one is turned away, as before.
TEST_F(HttpRangeServerLimitsTest, WithoutAReaderLeftBehindANewConnectionIsTurnedAway) {
    ServerConfig config;
    config.max_concurrent_streams = 2;
    start_server(config);

    auto idle1 = connect();
    auto idle2 = connect();
    ASSERT_TRUE(eventually([&] { return server_->active_connections() == 2; }));

    auto third = send_get("bytes=0-");
    EXPECT_EQ(read_status(third), "");
    EXPECT_EQ(server_->active_connections(), 2);
}

// A server for one device (app WORK-37): anyone else is closed at once,
// without an answer, and takes no slot.
TEST_F(HttpRangeServerLimitsTest, OnlyTheAllowedPeerIsServed) {
    ServerConfig config;
    config.allowed_peer = "192.0.2.1";
    config.max_concurrent_streams = 1;
    start_server(config);

    auto stranger = send_get("bytes=0-");
    EXPECT_EQ(read_status(stranger), "");
    EXPECT_EQ(server_->active_connections(), 0);
}

TEST_F(HttpRangeServerLimitsTest, TheAllowedPeerIsServed) {
    ServerConfig config;
    config.allowed_peer = "127.0.0.1";
    start_server(config);

    auto reader = send_get("bytes=0-");
    EXPECT_EQ(read_status(reader), "HTTP/1.1 206 Partial Content");
}

TEST_F(HttpRangeServerLimitsTest, AnIpv6StreamUrlHasItsAddressInBrackets) {
    ServerConfig config;
    config.bind_address = "::1";
    start_server(config);

    auto url = server_->stream_url("abc123", 0);
    EXPECT_EQ(url.rfind("http://[::1]:" + std::to_string(port_) + "/stream/abc123/0?token=", 0),
              0u) << url;
}

}  // namespace
}  // namespace seekserve
