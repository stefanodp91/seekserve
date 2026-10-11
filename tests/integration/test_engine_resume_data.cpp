// The engine writes each torrent's resume data into the cache database (app
// WORK-29, wave P2b, step P2b-01) and starts again from it (step P2b-02).
// Offline: torrents without trackers whose
// data is already in the save path, the session behind a proxy on a closed
// port (no DHT).

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <nlohmann/json.hpp>

#include "seekserve/engine.hpp"
#include "seekserve/offline_cache.hpp"
#include "seekserve/session_manager.hpp"

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

// The bytes make_torrent wrote for `seed`.
std::vector<char> file_bytes(int seed) {
    std::vector<char> data(kSize);
    for (int i = 0; i < kSize; ++i) data[i] = static_cast<char>(i * 31 + seed);
    return data;
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

    static json status(SeekServeEngine& engine, const std::string& id) {
        return json::parse(engine.get_status_json(id));
    }

    // A first engine that had the whole torrent and went away; returns the
    // torrent's id. What it knew is in the cache database.
    std::string first_run(const std::string& torrent) {
        SeekServeEngine engine(config_);
        auto added = engine.add_torrent(torrent);
        EXPECT_TRUE(added);
        if (!added) return "";
        EXPECT_TRUE(wait_for([&] { return complete(engine, added.value()); }));
        return added.value();
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

// --- Starting again from the resume data (P2b-02) ---

// The point of it all: after a restart the engine knows the torrent's files
// without asking anyone, and what it has without reading the disk again.
TEST_F(EngineResumeDataTest, ARestartNeedsNeitherTheMetadataNorACheck) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    auto id = first_run(torrent);
    ASSERT_FALSE(id.empty());

    // Nothing to get the metadata from: no peers, and no .torrent file.
    fs::remove(torrent);
    // Other bytes of the same length: a check of the file would find no
    // valid piece.
    {
        std::vector<char> other(kSize, 'x');
        std::ofstream((dir_ / "engine" / "a.mkv").string(), std::ios::binary)
            .write(other.data(), kSize);
    }

    SeekServeEngine engine(config_);

    ASSERT_EQ(engine.list_torrents(), std::vector<std::string>{id});
    ASSERT_TRUE(wait_for([&] { return status(engine, id).value("has_metadata", false); }))
        << engine.get_status_json(id);
    auto files = engine.list_files(id);
    ASSERT_TRUE(files);
    ASSERT_EQ(files.value().size(), 1u);
    EXPECT_EQ(files.value()[0].path, "a.mkv");

    // Give a check the time to run, were there one.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto st = status(engine, id);
    EXPECT_NE(st.value("state", 0), 1) << "checking_files";
    EXPECT_GE(st.value("progress", 0.0), 1.0) << st.dump();
}

// What starts after a restart is the app's decision (Tor ready, the queue):
// a restored torrent never runs by itself, even if it was running before.
TEST_F(EngineResumeDataTest, ARestoredTorrentComesBackPaused) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    auto id = first_run(torrent);  // left running
    ASSERT_FALSE(id.empty());

    SeekServeEngine engine(config_);

    ASSERT_TRUE(wait_for([&] { return status(engine, id).value("has_metadata", false); }));
    EXPECT_TRUE(status(engine, id).value("paused", false)) << engine.get_status_json(id);
}

// The same holds, with the switch on, for a torrent that has no resume data
// yet (the first start after an update): it is restored from its URI, paused.
TEST_F(EngineResumeDataTest, ATorrentWithoutResumeDataComesBackPausedToo) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    std::string id;
    {
        config_.session.resume_data = false;  // an engine from before
        id = first_run(torrent);
        config_.session.resume_data = true;
    }
    ASSERT_FALSE(id.empty());
    ASSERT_TRUE(stored(id).empty());

    SeekServeEngine engine(config_);

    ASSERT_EQ(engine.list_torrents(), std::vector<std::string>{id});
    EXPECT_TRUE(status(engine, id).value("paused", false)) << engine.get_status_json(id);
}

// What is on the device can be played with nobody sharing the torrent.
TEST_F(EngineResumeDataTest, ARestoredTorrentStreamsWhatItHas) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    auto id = first_run(torrent);
    ASSERT_FALSE(id.empty());
    fs::remove(torrent);

    SeekServeEngine engine(config_);
    ASSERT_TRUE(engine.start_server(0));
    ASSERT_TRUE(wait_for([&] { return status(engine, id).value("has_metadata", false); }));
    ASSERT_TRUE(engine.start_torrent(id));
    ASSERT_TRUE(engine.select_file(id, 0));
    auto url = engine.get_stream_url(id, 0);
    ASSERT_TRUE(url);

    auto reply = http_get(url.value(), "bytes=1000-1099");

    ASSERT_NE(reply.find("206"), std::string::npos) << reply.substr(0, 200);
    auto body = reply.substr(reply.find("\r\n\r\n") + 4);
    auto expected = file_bytes(7);
    EXPECT_EQ(body, std::string(expected.begin() + 1000, expected.begin() + 1100));
}

// A file deleted behind the engine (the app frees space) is not a piece the
// engine still believes it has: libtorrent sees it is gone and checks.
TEST_F(EngineResumeDataTest, AFileThatWentIsNotBelieved) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    auto id = first_run(torrent);
    ASSERT_FALSE(id.empty());
    fs::remove(dir_ / "engine" / "a.mkv");

    SeekServeEngine engine(config_);
    ASSERT_TRUE(wait_for([&] { return status(engine, id).value("has_metadata", false); }));
    ASSERT_TRUE(engine.start_torrent(id));

    ASSERT_TRUE(wait_for([&] {
        auto st = status(engine, id);
        return st.value("state", 0) != 1 && st.value("state", 0) != 7 &&
               st.value("progress", 1.0) < 1.0;
    })) << engine.get_status_json(id);
    EXPECT_EQ(status(engine, id).value("progress", 1.0), 0.0);
}

// Damaged resume data must not cost the torrent: it comes back from its URI.
TEST_F(EngineResumeDataTest, UnreadableResumeDataFallsBackToTheUri) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    auto id = first_run(torrent);
    ASSERT_FALSE(id.empty());
    {
        OfflineCacheManager writer(config_.cache);
        writer.save_resume_data(id, {'x', 'y', 'z'});
    }

    SeekServeEngine engine(config_);

    ASSERT_EQ(engine.list_torrents(), std::vector<std::string>{id});
    ASSERT_TRUE(wait_for([&] { return status(engine, id).value("has_metadata", false); }))
        << engine.get_status_json(id);
}

// Without the switch the engine restores as before, from the URI only: here
// the .torrent file is gone, so the torrent cannot come back.
TEST_F(EngineResumeDataTest, WithoutTheSwitchResumeDataIsNotRead) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    auto id = first_run(torrent);
    ASSERT_FALSE(id.empty());
    ASSERT_FALSE(stored(id).empty());
    fs::remove(torrent);

    config_.session.resume_data = false;
    SeekServeEngine engine(config_);

    EXPECT_TRUE(engine.list_torrents().empty());
}

// Restart after restart: an engine that changed nothing leaves the resume
// data as it found it, and the next one starts from it again.
TEST_F(EngineResumeDataTest, RestartsAgainAndAgain) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    auto id = first_run(torrent);
    ASSERT_FALSE(id.empty());
    fs::remove(torrent);

    for (int run = 0; run < 3; ++run) {
        SeekServeEngine engine(config_);
        ASSERT_EQ(engine.list_torrents(), std::vector<std::string>{id}) << "run " << run;
        ASSERT_TRUE(wait_for([&] { return status(engine, id).value("has_metadata", false); }))
            << "run " << run;
        EXPECT_GE(status(engine, id).value("progress", 0.0), 1.0) << "run " << run;
    }
}

// A torrent whose metadata never arrived has resume data too (its hashes and
// trackers, as libtorrent writes them): it comes back from it as it would
// from its magnet, under the same id, paused, still without its files. Seen
// on the phone: the torrents the app restores and pauses without starting.
TEST_F(EngineResumeDataTest, ATorrentStillWithoutMetadataComesBackTheSame) {
    const std::string magnet =
        "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"
        "&dn=never.started&tr=http%3A%2F%2Ftracker.invalid%2Fannounce";
    std::string id;
    {
        SeekServeEngine engine(config_);
        auto added = engine.add_torrent(magnet);
        ASSERT_TRUE(added);
        id = added.value();
        ASSERT_TRUE(engine.pause_torrent(id));  // as the service does on restore
        ASSERT_TRUE(wait_for([&] { return !stored(id).empty(); }))
            << "no resume data for a torrent without metadata";
    }
    auto params = decode(stored(id));
    EXPECT_FALSE(params.ti);
    EXPECT_TRUE(params.peers.empty());

    SeekServeEngine engine(config_);

    ASSERT_EQ(engine.list_torrents(), std::vector<std::string>{id});
    auto st = status(engine, id);
    EXPECT_FALSE(st.value("has_metadata", true)) << st.dump();
    EXPECT_TRUE(st.value("paused", false)) << st.dump();
    EXPECT_FALSE(engine.list_files(id));
    // It can still be started and removed like any other.
    EXPECT_TRUE(engine.start_torrent(id));
    EXPECT_TRUE(engine.remove_torrent(id, false));
    EXPECT_TRUE(stored(id).empty());
}

// A torrent added again from its resume data keeps the id it was stored
// under, whatever its hashes say (a hybrid added by its v1 hash, app BUG-71).
TEST_F(EngineResumeDataTest, ARestoredTorrentKeepsTheIdItHad) {
    auto torrent = make_torrent(dir_, "a.mkv", 7);
    lt::error_code ec;
    auto params = lt::load_torrent_file(torrent, ec, lt::load_torrent_limits{});
    ASSERT_FALSE(ec) << ec.message();
    params.save_path = config_.session.save_path;

    TorrentSessionManager sessions(config_.session);
    auto id = sessions.restore_torrent("the-id-it-had", params);

    ASSERT_TRUE(id);
    EXPECT_EQ(id.value(), "the-id-it-had");
    EXPECT_TRUE(sessions.has_torrent("the-id-it-had"));
    auto handle = sessions.get_handle("the-id-it-had");
    ASSERT_TRUE(handle.is_valid());
    auto found = sessions.id_of(handle);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, "the-id-it-had");
    auto flags = handle.flags();
    EXPECT_TRUE(flags & lt::torrent_flags::paused);
    EXPECT_FALSE(flags & lt::torrent_flags::auto_managed);
}

} // namespace
} // namespace seekserve
