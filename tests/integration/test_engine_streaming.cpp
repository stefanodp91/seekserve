// The engine routes each stream request to its own torrent's scheduler (app
// BUG-83), and serves a file to one other device through a share (app
// WORK-37). Offline: torrents without trackers whose data is already in the
// save path, the session behind a proxy on a closed port (no DHT); shares are
// opened on 127.0.0.1.

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <libtorrent/create_torrent.hpp>
#include <nlohmann/json.hpp>

#include "seekserve/engine.hpp"

namespace seekserve {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr int kPiece = 16 * 1024;
constexpr int kPieces = 16;
constexpr int kSize = kPiece * kPieces;

template <typename Pred>
bool wait_for(Pred done, std::chrono::seconds limit = std::chrono::seconds(10)) {
    auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return done();
}

// Writes `name` into dir/engine and a .torrent for it; returns its path.
std::string make_torrent(const fs::path& dir, const std::string& name, int seed) {
    std::vector<char> data(kSize);
    for (int i = 0; i < kSize; ++i) data[i] = static_cast<char>(i * 31 + seed);
    std::ofstream((dir / "engine" / name).string(), std::ios::binary).write(data.data(), kSize);
    lt::create_torrent ct(lt::list_files((dir / "engine" / name).string()), kPiece);
    lt::set_piece_hashes(ct, (dir / "engine").string());
    auto buf = ct.generate_buf();
    auto path = (dir / (name + ".torrent")).string();
    std::ofstream(path, std::ios::binary)
        .write(buf.data(), static_cast<std::streamsize>(buf.size()));
    return path;
}

// GET with a Range on a stream URL (http://127.0.0.1:PORT/...); whole reply.
std::string http_get(const std::string& url, const std::string& range) {
    auto rest = url.substr(std::string("http://127.0.0.1:").size());
    auto slash = rest.find('/');
    int port = std::stoi(rest.substr(0, slash));
    auto path = rest.substr(slash);
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return "";
    }
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: " + range +
                      "\r\nConnection: close\r\n\r\n";
    ::send(fd, req.data(), req.size(), 0);
    std::string out;
    char buf[65536];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) out.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    return out;
}

int playhead(SeekServeEngine& engine, const std::string& id) {
    auto j = json::parse(engine.get_status_json(id));
    return j.contains("playhead_piece") ? j["playhead_piece"].get<int>() : -1;
}

class EngineStreamingTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("seekserve_test_engine_streaming_" + std::to_string(::getpid()));
        fs::remove_all(dir_);
        fs::create_directories(dir_ / "engine");

        config_.session.save_path = (dir_ / "engine").string();
        config_.session.enable_webtorrent = false;
        config_.session.proxy.enabled = true;  // DHT off; peers only via a closed port
        config_.session.proxy.hostname = "127.0.0.1";
        config_.session.proxy.port = 1;
        config_.cache.db_path = (dir_ / "cache.db").string();
        config_.auth_token = "tok";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path dir_;
    SeekServeEngine::Config config_;
};

// The callback used to capture the scheduler of the last select_file of any
// torrent: a download selected after the stream took its seeks.
TEST_F(EngineStreamingTest, RangeRequestMovesOnlyItsTorrentsScheduler) {
    auto t1 = make_torrent(dir_, "a.mkv", 7);
    auto t2 = make_torrent(dir_, "b.mkv", 11);

    SeekServeEngine engine(config_);
    ASSERT_TRUE(engine.start_server(0));

    auto id1 = engine.add_torrent(t1);
    auto id2 = engine.add_torrent(t2);
    ASSERT_TRUE(id1);
    ASSERT_TRUE(id2);
    auto complete = [&](const std::string& id) {
        auto j = json::parse(engine.get_status_json(id));
        return j.value("has_metadata", false) && j.value("progress", 0.0) >= 1.0;
    };
    ASSERT_TRUE(wait_for([&] { return complete(id1.value()) && complete(id2.value()); }))
        << engine.get_status_json(id1.value());

    ASSERT_TRUE(engine.select_file(id1.value(), 0));
    ASSERT_TRUE(engine.select_file(id2.value(), 0));
    ASSERT_EQ(playhead(engine, id1.value()), 0);
    ASSERT_EQ(playhead(engine, id2.value()), 0);

    auto url = engine.get_stream_url(id1.value(), 0);
    ASSERT_TRUE(url);
    auto reply = http_get(url.value(), "bytes=" + std::to_string(10 * kPiece) + "-");
    ASSERT_NE(reply.find("206"), std::string::npos) << reply.substr(0, 200);

    EXPECT_GE(playhead(engine, id1.value()), 10) << "the stream's own scheduler did not move";
    EXPECT_EQ(playhead(engine, id2.value()), 0) << "another torrent's scheduler took the request";

    // Selecting the file again replaces the scheduler: requests go to the new one.
    ASSERT_TRUE(engine.select_file(id1.value(), 0));
    reply = http_get(engine.get_stream_url(id1.value(), 0).value(),
                     "bytes=" + std::to_string(5 * kPiece) + "-");
    ASSERT_NE(reply.find("206"), std::string::npos);
    EXPECT_GE(playhead(engine, id1.value()), 5);

    engine.stop_server();
}

// A share for a device of the home network (app WORK-37, DEC-613): its own
// server, port and token, and a URL that does not carry the torrent's id
// (its info hash, which tells anyone what the video is).
class EngineShareTest : public EngineStreamingTest {
protected:
    // An engine with one complete torrent whose file a player reads.
    void SetUp() override {
        EngineStreamingTest::SetUp();
        auto t = make_torrent(dir_, "a.mkv", 7);
        engine_ = std::make_unique<SeekServeEngine>(config_);
        ASSERT_TRUE(engine_->start_server(0));
        auto id = engine_->add_torrent(t);
        ASSERT_TRUE(id);
        id_ = id.value();
        ASSERT_TRUE(wait_for([&] {
            auto j = json::parse(engine_->get_status_json(id_));
            return j.value("has_metadata", false) && j.value("progress", 0.0) >= 1.0;
        })) << engine_->get_status_json(id_);
        ASSERT_TRUE(engine_->select_file(id_, 0));
    }

    void TearDown() override {
        engine_.reset();
        EngineStreamingTest::TearDown();
    }

    static std::string with_token(const std::string& url, const std::string& token) {
        return url.substr(0, url.find("token=") + 6) + token;
    }

    static std::string path_of(const std::string& url) {
        auto rest = url.substr(std::string("http://127.0.0.1:").size());
        return rest.substr(rest.find('/'));
    }

    static std::string with_path(const std::string& url, const std::string& path) {
        auto rest = url.substr(std::string("http://127.0.0.1:").size());
        return "http://127.0.0.1:" + rest.substr(0, rest.find('/')) + path;
    }

    std::unique_ptr<SeekServeEngine> engine_;
    std::string id_;
};

TEST_F(EngineShareTest, AShareServesTheFileWithoutTheTorrentsId) {
    auto share = engine_->open_share(id_, 0, "127.0.0.1", "127.0.0.1");
    ASSERT_TRUE(share) << share.error().message();
    const auto& url = share.value().url;
    auto stream_url = engine_->get_stream_url(id_, 0).value();

    EXPECT_EQ(url.find(id_), std::string::npos) << url;
    EXPECT_EQ(url.rfind("http://127.0.0.1:", 0), 0u) << url;
    EXPECT_NE(url.substr(0, url.find('/', 7)), stream_url.substr(0, stream_url.find('/', 7)))
        << "the share has a server of its own";
    EXPECT_EQ(url.find("token=tok&"), std::string::npos);
    EXPECT_NE(url.substr(url.find("token=")), "token=tok");
    EXPECT_FALSE(share.value().id.empty());

    auto reply = http_get(url, "bytes=" + std::to_string(10 * kPiece) + "-");
    ASSERT_NE(reply.find("206"), std::string::npos) << reply.substr(0, 200);
    auto body = reply.substr(reply.find("\r\n\r\n") + 4);
    ASSERT_EQ(static_cast<int>(body.size()), 6 * kPiece);
    for (int i = 0; i < 6 * kPiece; ++i) {
        ASSERT_EQ(body[i], static_cast<char>((10 * kPiece + i) * 31 + 7)) << "byte " << i;
    }
    EXPECT_GE(playhead(*engine_, id_), 10) << "the share's reads did not move the scheduler";
}

TEST_F(EngineShareTest, EachServerTakesOnlyItsOwnToken) {
    auto share = engine_->open_share(id_, 0, "127.0.0.1", "127.0.0.1");
    ASSERT_TRUE(share);
    const auto& url = share.value().url;
    auto stream_url = engine_->get_stream_url(id_, 0).value();
    auto share_token = url.substr(url.find("token=") + 6);

    EXPECT_NE(http_get(with_token(url, "tok"), "bytes=0-").find("403"), std::string::npos);
    EXPECT_NE(http_get(with_token(stream_url, share_token), "bytes=0-").find("403"),
              std::string::npos);
    // The engine's own path does not lead to the file through the share.
    auto through_share = with_path(url, path_of(with_token(stream_url, share_token)));
    EXPECT_NE(http_get(through_share, "bytes=0-").find("404"), std::string::npos);
}

TEST_F(EngineShareTest, AShareServesNoOtherAddress) {
    auto share = engine_->open_share(id_, 0, "127.0.0.1", "192.0.2.1");
    ASSERT_TRUE(share);
    EXPECT_EQ(http_get(share.value().url, "bytes=0-"), "");
}

TEST_F(EngineShareTest, AClosedShareAnswersNoMore) {
    auto share = engine_->open_share(id_, 0, "127.0.0.1", "127.0.0.1");
    ASSERT_TRUE(share);
    ASSERT_NE(http_get(share.value().url, "bytes=0-").find("206"), std::string::npos);

    ASSERT_TRUE(engine_->close_share(share.value().id));
    EXPECT_EQ(http_get(share.value().url, "bytes=0-"), "");
    EXPECT_EQ(engine_->close_share(share.value().id).error(),
              make_error_code(errc::file_not_found));
}

TEST_F(EngineShareTest, StoppingTheServersClosesTheShares) {
    auto share = engine_->open_share(id_, 0, "127.0.0.1", "127.0.0.1");
    ASSERT_TRUE(share);
    engine_->stop_server();
    EXPECT_EQ(http_get(share.value().url, "bytes=0-"), "");
}

// Selecting the file again gives it a new stream: the share follows it, and
// the device keeps the URL it was given.
TEST_F(EngineShareTest, AShareFollowsItsFileWhenItIsSelectedAgain) {
    auto share = engine_->open_share(id_, 0, "127.0.0.1", "127.0.0.1");
    ASSERT_TRUE(share);
    ASSERT_TRUE(engine_->select_file(id_, 0));
    auto reply = http_get(share.value().url, "bytes=" + std::to_string(5 * kPiece) + "-");
    ASSERT_NE(reply.find("206"), std::string::npos) << reply.substr(0, 200);
    // The headers go out before the first read: a share left with the old,
    // cancelled stream would answer 206 and send no body.
    auto body = reply.substr(reply.find("\r\n\r\n") + 4);
    ASSERT_EQ(static_cast<int>(body.size()), 11 * kPiece);
    EXPECT_EQ(body[0], static_cast<char>(5 * kPiece * 31 + 7));
    EXPECT_GE(playhead(*engine_, id_), 5);
}

TEST_F(EngineShareTest, AShareNeedsAFileAPlayerReadsAndTwoAddresses) {
    EXPECT_EQ(engine_->open_share("0123456789abcdef", 0, "127.0.0.1", "127.0.0.1").error(),
              make_error_code(errc::torrent_not_found));
    EXPECT_EQ(engine_->open_share(id_, 1, "127.0.0.1", "127.0.0.1").error(),
              make_error_code(errc::file_not_found));
    // Never every network of the device at once, nor any client.
    EXPECT_EQ(engine_->open_share(id_, 0, "0.0.0.0", "127.0.0.1").error(),
              make_error_code(errc::invalid_argument));
    EXPECT_EQ(engine_->open_share(id_, 0, "::", "127.0.0.1").error(),
              make_error_code(errc::invalid_argument));
    EXPECT_EQ(engine_->open_share(id_, 0, "127.0.0.1", "0.0.0.0").error(),
              make_error_code(errc::invalid_argument));
    EXPECT_EQ(engine_->open_share(id_, 0, "not an address", "127.0.0.1").error(),
              make_error_code(errc::invalid_argument));
    EXPECT_EQ(engine_->open_share(id_, 0, "127.0.0.1", "").error(),
              make_error_code(errc::invalid_argument));

    engine_->stop_server();
    EXPECT_EQ(engine_->open_share(id_, 0, "127.0.0.1", "127.0.0.1").error(),
              make_error_code(errc::session_not_started));
}

}  // namespace
}  // namespace seekserve
