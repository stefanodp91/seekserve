// The engine routes each stream request to its own torrent's scheduler (app
// BUG-83). Offline: two torrents without trackers whose data is already in
// the save path, the session behind a proxy on a closed port (no DHT).

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

}  // namespace
}  // namespace seekserve
