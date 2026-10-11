// The engine writes each torrent's resume data into the cache database (app
// WORK-29, wave P2b, step P2b-01). Offline: torrents without trackers whose
// data is already in the save path, the session behind a proxy on a closed
// port (no DHT).

#include <gtest/gtest.h>

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/torrent_info.hpp>
#include <nlohmann/json.hpp>

#include "seekserve/engine.hpp"
#include "seekserve/offline_cache.hpp"

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

class EngineResumeDataTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("seekserve_test_engine_resume_" + std::to_string(::getpid()));
        fs::remove_all(dir_);
        fs::create_directories(dir_ / "engine");

        config_.session.save_path = (dir_ / "engine").string();
        config_.session.enable_webtorrent = false;
        config_.session.proxy.enabled = true;  // DHT off; peers only via a closed port
        config_.session.proxy.hostname = "127.0.0.1";
        config_.session.proxy.port = 1;
        config_.session.resume_data = true;
        config_.cache.db_path = (dir_ / "cache.db").string();
        config_.auth_token = "tok";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    // What the cache database holds for the torrent, read as the next engine
    // would read it: through a second connection.
    std::vector<char> stored(const std::string& id) {
        OfflineCacheManager reader(config_.cache);
        return reader.load_resume_data(id);
    }

    static lt::add_torrent_params decode(const std::vector<char>& data) {
        lt::error_code ec;
        auto p = lt::read_resume_data(data, ec);
        EXPECT_FALSE(ec) << ec.message();
        return p;
    }

    static bool complete(SeekServeEngine& engine, const std::string& id) {
        auto j = json::parse(engine.get_status_json(id));
        return j.value("has_metadata", false) && j.value("progress", 0.0) >= 1.0 &&
               j.value("state", 0) != 1;  // not checking_files any more
    }

    fs::path dir_;
    SeekServeEngine::Config config_;
};

// As soon as the files of a torrent are known, the engine keeps them: the
// next start does not have to ask peers for the metadata.
TEST_F(EngineResumeDataTest, WritesTheMetadataOnceItIsKnown) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    SeekServeEngine engine(config_);

    auto id = engine.add_torrent(torrent);
    ASSERT_TRUE(id);

    ASSERT_TRUE(wait_for([&] { return !stored(id.value()).empty(); }))
        << "no resume data in the cache database";
    auto params = decode(stored(id.value()));
    ASSERT_TRUE(params.ti) << "resume data without the info dictionary";
    EXPECT_EQ(params.ti->name(), "a.mkv");
    EXPECT_EQ(params.ti->num_pieces(), kPieces);
    EXPECT_TRUE(params.peers.empty());
    EXPECT_TRUE(params.banned_peers.empty());
}

// Without the switch the engine behaves as before: nothing is kept.
TEST_F(EngineResumeDataTest, WritesNothingWhenSwitchedOff) {
    config_.session.resume_data = false;
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    std::string id;
    {
        SeekServeEngine engine(config_);
        auto added = engine.add_torrent(torrent);
        ASSERT_TRUE(added);
        id = added.value();
        ASSERT_TRUE(wait_for([&] { return complete(engine, id); }));
        ASSERT_TRUE(engine.pause_torrent(id));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        EXPECT_TRUE(stored(id).empty());
    }
    EXPECT_TRUE(stored(id).empty()) << "written at shutdown";
}

// A pause is when the user leaves a torrent: what it has by then is kept.
TEST_F(EngineResumeDataTest, WritesThePiecesItHasOnPause) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    SeekServeEngine engine(config_);
    auto id = engine.add_torrent(torrent);
    ASSERT_TRUE(id);
    ASSERT_TRUE(wait_for([&] { return complete(engine, id.value()); }))
        << engine.get_status_json(id.value());

    ASSERT_TRUE(engine.pause_torrent(id.value()));

    ASSERT_TRUE(wait_for([&] {
        auto data = stored(id.value());
        if (data.empty()) return false;
        auto p = decode(data);
        return p.have_pieces.size() == kPieces && p.have_pieces.all_set();
    })) << "the checked pieces never reached the cache database";
}

// The engine can go away at any time (the service is stopped): what the
// torrents have is written before it does.
TEST_F(EngineResumeDataTest, WritesAtShutdown) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    std::string id;
    {
        SeekServeEngine engine(config_);
        auto added = engine.add_torrent(torrent);
        ASSERT_TRUE(added);
        id = added.value();
        ASSERT_TRUE(wait_for([&] { return complete(engine, id); }));
    }

    auto data = stored(id);
    ASSERT_FALSE(data.empty());
    auto params = decode(data);
    ASSERT_TRUE(params.ti);
    EXPECT_EQ(params.have_pieces.size(), kPieces);
    EXPECT_TRUE(params.have_pieces.all_set());
}

// With an interval, a torrent that changed is saved again by itself: a
// process killed without warning loses at most that much.
TEST_F(EngineResumeDataTest, WritesAgainEveryInterval) {
    config_.session.resume_save_interval = std::chrono::seconds(1);
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    SeekServeEngine engine(config_);
    ASSERT_TRUE(engine.start_server(0));  // the timer runs with the servers
    auto id = engine.add_torrent(torrent);
    ASSERT_TRUE(id);

    // No pause, no shutdown: only the timer can write the checked pieces.
    ASSERT_TRUE(wait_for([&] {
        auto data = stored(id.value());
        if (data.empty()) return false;
        auto p = decode(data);
        return p.have_pieces.size() == kPieces && p.have_pieces.all_set();
    })) << "the timer never wrote the checked pieces";
}

// A torrent taken out of the engine leaves nothing behind, even if a save
// was still on its way.
TEST_F(EngineResumeDataTest, ARemovedTorrentLeavesNoResumeData) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    SeekServeEngine engine(config_);
    auto id = engine.add_torrent(torrent);
    ASSERT_TRUE(id);
    ASSERT_TRUE(wait_for([&] { return !stored(id.value()).empty(); }));

    ASSERT_TRUE(engine.pause_torrent(id.value()));  // a save is on its way
    ASSERT_TRUE(engine.remove_torrent(id.value(), false));

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(stored(id.value()).empty());
    OfflineCacheManager reader(config_.cache);
    EXPECT_TRUE(reader.list_torrent_uris().empty());
}

} // namespace
} // namespace seekserve
