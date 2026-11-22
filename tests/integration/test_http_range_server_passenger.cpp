// A passenger read (role=listener): something other than the player reads
// a stream, a subtitle listener in the app, and must never take the download
// away from the player. It gets only the bytes already here, is told at once
// when they are not, and never moves the scheduler.

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

static constexpr int kPieceLength = 16384;
static constexpr int kNumPieces = 48;
static constexpr std::int64_t kFileSize = std::int64_t{kPieceLength} * kNumPieces;
static const std::string kPlayer = "/stream/abc123/0?token=testtoken";
static const std::string kPassenger = "/stream/abc123/0?token=testtoken&role=listener";

static std::uint8_t test_byte(std::int64_t pos) {
    return static_cast<std::uint8_t>((pos * 7 + 13) % 256);
}

static std::string bytes(std::int64_t first, std::int64_t last) {
    return "bytes=" + std::to_string(first) + "-" + (last < 0 ? "" : std::to_string(last));
}

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
// server that waits fails the test instead of hanging it.
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

class HttpRangeServerPassengerTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path() / "seekserve_test_http_passenger";
        std::filesystem::create_directories(tmp_dir_);
        file_path_ = (tmp_dir_ / "test.bin").string();

        std::ofstream out(file_path_, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        for (std::int64_t i = 0; i < kFileSize; ++i) {
            auto b = test_byte(i);
            out.write(reinterpret_cast<const char*>(&b), 1);
        }
        out.close();

        fs_.set_piece_length(kPieceLength);
        fs_.add_file("test.bin", kFileSize);
        fs_.set_num_pieces(kNumPieces);

        mapper_ = std::make_unique<ByteRangeMapper>(fs_, 0);
        avail_ = std::make_unique<PieceAvailabilityIndex>(kNumPieces, kPieceLength, kPieceLength);
        source_ = std::make_shared<ByteSource>(
            lt::torrent_handle{}, 0, file_path_, *mapper_, *avail_, 100ms);

        server_ = std::make_shared<HttpRangeServer>(ioc_, ServerConfig{});
        server_->set_byte_source(source_, "abc123", 0);
        server_->set_auth_token("testtoken");
        server_->set_range_callback([this](const ByteRange& range, const TorrentId&, FileIndex) {
            std::lock_guard lock(calls_mu_);
            range_starts_.push_back(range.start);
        });
        auto result = server_->start(0);
        ASSERT_TRUE(result.ok()) << result.error().message();
        port_ = result.value();
        io_thread_ = std::thread([this] { ioc_.run(); });
    }

    void TearDown() override {
        server_->stop();
        eventually([&] { return server_->active_connections() == 0; });
        ioc_.stop();
        if (io_thread_.joinable()) io_thread_.join();
        std::filesystem::remove_all(tmp_dir_);
    }

    void complete_pieces(int first, int last) {
        for (int p = first; p <= last; ++p) avail_->mark_complete(p);
        source_->notify_piece_complete();
    }

    std::vector<std::int64_t> range_starts() {
        std::lock_guard lock(calls_mu_);
        return range_starts_;
    }

    tcp::socket send_get(const std::string& target, const std::string& range) {
        tcp::socket socket(client_);
        socket.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), port_));
        std::string req = "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
        if (!range.empty()) req += "Range: " + range + "\r\n";
        req += "Connection: close\r\n\r\n";
        net::write(socket, net::buffer(req));
        return socket;
    }

    // The whole response of a request that is expected to end by itself.
    http::response<http::string_body> get(const std::string& target, const std::string& range) {
        auto socket = send_get(target, range);
        ReadDeadline deadline(socket, 3s);
        beast::flat_buffer buffer;
        http::response_parser<http::string_body> parser;
        parser.body_limit(kFileSize + 1);
        boost::system::error_code ec;
        http::read(socket, buffer, parser, ec);
        EXPECT_FALSE(ec) << ec.message();
        return parser.release();
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

    std::mutex calls_mu_;
    std::vector<std::int64_t> range_starts_;
};

// The player waits at piece 3; a listener reads piece 40, far ahead. The
// scheduler hears only the player, before and after, and the player still
// owns the position when its piece arrives.
TEST_F(HttpRangeServerPassengerTest, passengerNeverMovesThePosition) {
    complete_pieces(2, 2);
    complete_pieces(40, 40);

    auto player = send_get(kPlayer, bytes(2 * kPieceLength, -1));
    {
        ReadDeadline deadline(player, 3s);
        net::streambuf head;
        net::read_until(player, head, "\r\n\r\n");
    }
    // It reports its start, sends piece 2, reports piece 3 and waits for it.
    ASSERT_TRUE(eventually([&] { return range_starts().size() == 2; }));

    auto res = get(kPassenger, bytes(40 * kPieceLength, 41 * kPieceLength - 1));
    EXPECT_EQ(res.result(), http::status::partial_content);
    EXPECT_EQ(static_cast<int>(res.body().size()), kPieceLength);
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(range_starts(), (std::vector<std::int64_t>{2 * kPieceLength, 3 * kPieceLength}));

    complete_pieces(3, 3);
    EXPECT_TRUE(eventually([&] {
        auto starts = range_starts();
        return starts.size() == 3 && starts.back() == 4 * kPieceLength;
    })) << "the player lost the position to the listener";
    player.close();
}

TEST_F(HttpRangeServerPassengerTest, passengerGetsWaitWhenThePieceIsMissing) {
    auto started = std::chrono::steady_clock::now();
    auto res = get(kPassenger, bytes(0, 100));
    auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_EQ(res.result(), http::status::service_unavailable);
    EXPECT_TRUE(res.body().empty());
    EXPECT_LT(elapsed, 100ms);
    EXPECT_TRUE(range_starts().empty());
}

TEST_F(HttpRangeServerPassengerTest, passengerGetsOnlyWhatArrived) {
    complete_pieces(0, 3);
    complete_pieces(5, 5);

    auto res = get(kPassenger, bytes(0, 6 * kPieceLength - 1));

    EXPECT_EQ(res.result(), http::status::partial_content);
    EXPECT_EQ(res[http::field::content_range],
              "bytes 0-" + std::to_string(4 * kPieceLength - 1) + "/" + std::to_string(kFileSize));
    ASSERT_EQ(static_cast<int>(res.body().size()), 4 * kPieceLength);
    for (int i = 0; i < 4 * kPieceLength; ++i) {
        ASSERT_EQ(static_cast<std::uint8_t>(res.body()[i]), test_byte(i)) << "byte " << i;
    }
    EXPECT_TRUE(range_starts().empty());
}

TEST_F(HttpRangeServerPassengerTest, passengerPastTheEndGets416) {
    complete_pieces(0, kNumPieces - 1);

    auto res = get(kPassenger, bytes(kFileSize, -1));

    EXPECT_EQ(res.result(), http::status::range_not_satisfiable);
    EXPECT_TRUE(range_starts().empty());
}

TEST_F(HttpRangeServerPassengerTest, passengerNeedsTheToken) {
    complete_pieces(0, kNumPieces - 1);

    auto res = get("/stream/abc123/0?role=listener", bytes(0, 100));

    EXPECT_EQ(res.result(), http::status::forbidden);
    EXPECT_TRUE(range_starts().empty());
}

} // namespace
} // namespace seekserve
