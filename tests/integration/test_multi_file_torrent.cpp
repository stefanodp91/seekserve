// A torrent with several files, as a season pack is: what selecting one of
// them does (app WORK-29, phase P4, step P4-01), what the status says of
// each wanted file (step P4-02), and the two files a torrent may want at
// once, the one a player reads and the one that downloads behind it (step
// P4-03, app DEC-42).
// Offline: a second session seeds the torrent on 127.0.0.1 and the engine
// reaches it only through LoopbackSocks5, with no DHT and no trackers.

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <nlohmann/json.hpp>

#include "seekserve/engine.hpp"
#include "seekserve/offline_cache.hpp"

#include "loopback_socks5.hpp"

namespace seekserve {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;
using test::LoopbackSocks5;

constexpr int kPiece = 16 * 1024;
// Until a file is selected every file of a new torrent is wanted, and on
// loopback a small one would be complete before the test selects another:
// the tests keep the proxy this slow (bytes per second) until they select.
constexpr int kTrickle = 64 * 1024;
const char* const kFolder = "Show.S01";
const char* const kE01 = "Show.S01E01.mkv";
const char* const kE01Subtitles = "Show.S01E01.srt";
const char* const kE02 = "Show.S01E02.mkv";
const char* const kE03 = "Show.S01E03.mkv";

struct PackFile {
    const char* name;
    int size;
};

// In the order of the torrent. No size is a multiple of the piece size: the
// files of a v1 torrent follow one another, so each of them shares its last
// piece with the next one.
const PackFile kPack[] = {
    {kE01, 512 * 1024 + 4321},
    {kE01Subtitles, 700},
    {kE02, 4 * 1024 * 1024 + 1234},
    {kE03, 256 * 1024 + 99},
};

template <typename Pred>
bool wait_for(Pred done, std::chrono::seconds limit = std::chrono::seconds(20)) {
    auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return done();
}

// Bytes that do not repeat from one piece to the next: a piece read in the
// place of another must not pass for the right one.
std::string content_of(int size, std::uint32_t seed) {
    std::string data(static_cast<std::size_t>(size), '\0');
    std::uint32_t x = seed * 2654435761u + 1;
    for (auto& c : data) {
        x = x * 1664525u + 1013904223u;
        c = static_cast<char>(x >> 24);
    }
    return data;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path.string(), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// GET with a Range on a stream URL (http://127.0.0.1:PORT/...); the whole
// answer, status line and headers first.
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

std::string http_get_body(const std::string& url, const std::string& range) {
    auto out = http_get(url, range);
    auto body = out.find("\r\n\r\n");
    return body == std::string::npos ? "" : out.substr(body + 4);
}

// The status code of the answer ("HTTP/1.1 206 ..."), or 0.
int http_status(const std::string& url, const std::string& range) {
    auto out = http_get(url, range);
    return out.size() >= 12 ? std::atoi(out.c_str() + 9) : 0;
}

// The files the engine said complete, in the order it said so.
class CompletedFiles {
public:
    explicit CompletedFiles(SeekServeEngine& engine) : engine_(engine) {
        engine_.set_event_callback([this](const std::string& event) {
            auto e = json::parse(event);
            if (e["type"] != "file_completed") return;
            std::lock_guard lock(mu_);
            files_.push_back(e["data"]["file_index"].get<FileIndex>());
        });
    }

    // set_event_callback waits for a callback in progress.
    ~CompletedFiles() { engine_.set_event_callback(nullptr); }

    bool has(FileIndex fi) const {
        std::lock_guard lock(mu_);
        return std::find(files_.begin(), files_.end(), fi) != files_.end();
    }

private:
    SeekServeEngine& engine_;
    mutable std::mutex mu_;
    std::vector<FileIndex> files_;
};

json status_of(SeekServeEngine& engine, const TorrentId& id) {
    return json::parse(engine.get_status_json(id));
}

// libtorrent's state for a torrent with every wanted piece and files it does
// not want: finished, not seeding.
constexpr int kFinished = static_cast<int>(lt::torrent_status::finished);

bool finished(SeekServeEngine& engine, const TorrentId& id) {
    auto status = status_of(engine, id);
    return status.value("state", 0) == kFinished && status.value("progress", 0.0) >= 1.0;
}

int peers_of(SeekServeEngine& engine, const TorrentId& id) {
    return status_of(engine, id).value("num_peers", -1);
}

// What the status says of file `fi` among the files the torrent wants; null
// when the file is not among them.
json wanted_file(const json& status, FileIndex fi) {
    if (!status.contains("files")) return nullptr;
    for (const auto& file : status["files"]) {
        if (file.value("index", -1) == fi) return file;
    }
    return nullptr;
}

std::int64_t bytes_of(const json& file) {
    return file.is_object() ? file.value("downloaded", std::int64_t{-1}) : -1;
}

bool whole(const json& file) {
    return file.is_object() && file.value("complete", false);
}

// What a wanted file is wanted for: "reader", "background", or nothing for
// the subtitles and for a file no call has named.
std::string role_of(const json& file) {
    return file.is_object() ? file.value("role", std::string()) : std::string();
}

int size_of(const std::string& name) {
    for (const auto& file : kPack) {
        if (name == file.name) return file.size;
    }
    return -1;
}

class MultiFileTorrent : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("seekserve_multi_file_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir_ / "seed" / kFolder);
        fs::create_directories(dir_ / "engine");

        std::uint32_t seed = 1;
        for (const auto& file : kPack) {
            auto data = content_of(file.size, seed++);
            std::ofstream((dir_ / "seed" / kFolder / file.name).string(), std::ios::binary)
                .write(data.data(), static_cast<std::streamsize>(data.size()));
        }

        // v1 only: a v2 torrent pads every file to a piece boundary, and no
        // piece would belong to two files. Sorted, so the order is kPack's.
        auto files = lt::list_files((dir_ / "seed" / kFolder).string());
        std::sort(files.begin(), files.end(),
                  [](const auto& a, const auto& b) { return a.filename < b.filename; });
        lt::create_torrent ct(std::move(files), kPiece, lt::create_torrent::v1_only);
        lt::set_piece_hashes(ct, (dir_ / "seed").string());
        torrent_file_ = ct.generate_buf();
        ti_ = lt::load_torrent_buffer(torrent_file_).ti;
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    SeekServeEngine::Config engine_config(int proxy_port) const {
        SeekServeEngine::Config config;
        config.session.save_path = (dir_ / "engine").string();
        config.session.enable_webtorrent = false;
        config.session.proxy.enabled = true;
        config.session.proxy.hostname = "127.0.0.1";
        config.session.proxy.port = proxy_port;
        config.cache.db_path = (dir_ / "cache.db").string();
        config.auth_token = "tok";
        return config;
    }

    std::unique_ptr<lt::session> start_seeder() const {
        lt::settings_pack sp;
        sp.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
        sp.set_bool(lt::settings_pack::enable_dht, false);
        sp.set_bool(lt::settings_pack::enable_lsd, false);
        sp.set_bool(lt::settings_pack::enable_upnp, false);
        sp.set_bool(lt::settings_pack::enable_natpmp, false);
        auto seeder = std::make_unique<lt::session>(lt::session_params{sp});

        lt::add_torrent_params atp;
        atp.ti = ti_;
        atp.save_path = (dir_ / "seed").string();
        // Started at once: a paused torrent refuses the engine's connection,
        // and libtorrent retries a refused peer only after a minute.
        atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
        auto handle = seeder->add_torrent(std::move(atp));
        wait_for([&] { return handle.status().state == lt::torrent_status::seeding; });
        return seeder;
    }

    // The seeder as the one peer, no trackers.
    std::string magnet(const lt::session& seeder) const {
        auto const& v1 = ti_->info_hashes().v1;
        return "magnet:?xt=urn:btih:"
            + lt::aux::to_hex({v1.data(), static_cast<std::ptrdiff_t>(v1.size())})
            + "&dn=" + kFolder + "&x.pe=127.0.0.1:" + std::to_string(seeder.listen_port());
    }

    // Adds the torrent and waits for its files.
    TorrentId add(SeekServeEngine& engine, const lt::session& seeder) const {
        auto added = engine.add_torrent(magnet(seeder));
        EXPECT_TRUE(added);
        if (!added) return {};
        const TorrentId id = added.value();
        EXPECT_TRUE(wait_for([&] { return status_of(engine, id).value("has_metadata", false); }))
            << "no metadata from the seeder";
        return id;
    }

    // The index the engine lists `name` under, or -1.
    static FileIndex index_of(SeekServeEngine& engine, const TorrentId& id,
                              const std::string& name) {
        auto files = engine.list_files(id);
        if (!files) return -1;
        for (const auto& file : files.value()) {
            if (fs::path(file.path).filename() == name) return file.index;
        }
        return -1;
    }

    std::string seeded(const std::string& name) const {
        return read_file(dir_ / "seed" / kFolder / name);
    }

    // What the engine has of `name` on disk; empty if it has no such file.
    std::string downloaded(const std::string& name) const {
        return read_file(dir_ / "engine" / kFolder / name);
    }

    // How many of the first `pieces` piece-long stretches of `name` the
    // engine has on disk as the seeder has them.
    int stretches_there(const std::string& name, int pieces) const {
        const auto mine = downloaded(name);
        const auto theirs = seeded(name);
        int there = 0;
        for (int i = 0; i < pieces; ++i) {
            const auto from = static_cast<std::size_t>(i) * kPiece;
            if (from + kPiece > mine.size() || from + kPiece > theirs.size()) break;
            if (mine.compare(from, kPiece, theirs, from, kPiece) == 0) ++there;
        }
        return there;
    }

    fs::path dir_;
    std::shared_ptr<lt::torrent_info const> ti_;
    // The same torrent as a .torrent file would hold it.
    std::vector<char> torrent_file_;
};

TEST_F(MultiFileTorrent, EachFileSharesAPieceWithTheNext) {
    const auto& layout = ti_->layout();
    ASSERT_EQ(layout.num_files(), static_cast<int>(std::size(kPack)));
    for (lt::file_index_t i{0}; static_cast<int>(i) < layout.num_files(); ++i) {
        auto const& expected = kPack[static_cast<int>(i)];
        EXPECT_EQ(fs::path(layout.file_path(i)).filename(), expected.name);
        EXPECT_EQ(layout.file_size(i), expected.size);
        if (static_cast<int>(i) > 0) {
            EXPECT_NE(layout.file_offset(i) % kPiece, 0) << expected.name;
        }
    }
}

// The file selected downloads, with the subtitles next to it, and nothing
// else.
TEST_F(MultiFileTorrent, ASelectedFileDownloadsAloneWithTheSubtitles) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex subtitles = index_of(engine, id, kE01Subtitles);
    const FileIndex e02 = index_of(engine, id, kE02);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e01, 0);
    ASSERT_GE(subtitles, 0);
    ASSERT_GE(e02, 0);
    ASSERT_GE(e03, 0);

    ASSERT_TRUE(engine.select_file(id, e01));
    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e01) && completed.has(subtitles); }))
        << engine.get_status_json(id);
    ASSERT_TRUE(wait_for([&] { return finished(engine, id); })) << engine.get_status_json(id);

    const auto status = status_of(engine, id);
    EXPECT_EQ(status["selected_file"], e01);
    EXPECT_EQ(status["offline_ready"], true);
    EXPECT_EQ(downloaded(kE01), seeded(kE01));
    EXPECT_EQ(downloaded(kE01Subtitles), seeded(kE01Subtitles));
    // The others are not wanted: neither completes.
    EXPECT_FALSE(completed.has(e02));
    EXPECT_FALSE(completed.has(e03));
    EXPECT_NE(downloaded(kE02), seeded(kE02));
    EXPECT_NE(downloaded(kE03), seeded(kE03));
}

// The status says, for each file the torrent wants, how big it is, how much
// of it is there and whether it is whole (app WORK-29, phase P4, step P4-02).
// `progress` counts the wanted pieces of the whole torrent: with two wanted
// files, as P4 will have, it is no file's own number.
TEST_F(MultiFileTorrent, TheStatusSaysWhatEachWantedFileHas) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex subtitles = index_of(engine, id, kE01Subtitles);
    const FileIndex e02 = index_of(engine, id, kE02);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e01, 0);
    ASSERT_GE(subtitles, 0);
    ASSERT_GE(e02, 0);
    ASSERT_GE(e03, 0);

    // Until a file is selected every file is wanted: the four of them.
    {
        const auto status = status_of(engine, id);
        ASSERT_TRUE(status.contains("files")) << status.dump();
        EXPECT_EQ(status["files"].size(), std::size(kPack)) << status.dump();
        for (const auto& file : kPack) {
            const auto listed = wanted_file(status, index_of(engine, id, file.name));
            ASSERT_TRUE(listed.is_object()) << file.name;
            EXPECT_EQ(listed["size"], file.size) << file.name;
        }
    }

    ASSERT_TRUE(engine.select_file(id, e02));
    ASSERT_TRUE(wait_for([&] { return bytes_of(wanted_file(status_of(engine, id), e02)) > 0; }))
        << engine.get_status_json(id);
    {
        // Still slow: the file is far from whole.
        const auto status = status_of(engine, id);
        const auto video = wanted_file(status, e02);
        EXPECT_LT(bytes_of(video), size_of(kE02)) << status.dump();
        EXPECT_FALSE(whole(video)) << status.dump();
        // The selected file and the subtitles, no other.
        EXPECT_EQ(status["files"].size(), 2u) << status.dump();
        EXPECT_TRUE(wanted_file(status, subtitles).is_object()) << status.dump();
        EXPECT_TRUE(wanted_file(status, e01).is_null()) << status.dump();
        EXPECT_TRUE(wanted_file(status, e03).is_null()) << status.dump();
    }

    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e02) && completed.has(subtitles); }))
        << engine.get_status_json(id);
    const auto status = status_of(engine, id);
    const auto video = wanted_file(status, e02);
    EXPECT_EQ(bytes_of(video), size_of(kE02)) << status.dump();
    EXPECT_TRUE(whole(video)) << status.dump();
    // The subtitles' only piece is also the last of the video before them
    // and the first of the one after: each file is told its own bytes of it.
    const auto text = wanted_file(status, subtitles);
    EXPECT_EQ(bytes_of(text), size_of(kE01Subtitles)) << status.dump();
    EXPECT_TRUE(whole(text)) << status.dump();
    EXPECT_EQ(status["files"].size(), 2u) << status.dump();
}

// With every wanted piece there, libtorrent closes its connections to the
// seeds (close_redundant_connections, on by default) and takes a peer back
// only a minute after it let it go (min_reconnect_time). So the next file of
// a pack, selected when the one before is complete, does not start at once:
// with one seeder, as here, it waits for that minute, which this test does
// not wait for. In a swarm the peers that are not seeds stay and the
// trackers may give others; how long the app's queue waits between two files
// of a pack was never measured on a phone (app BUG-198).
// To be looked at in P4, where the next file of a pack may start while a
// player holds the torrent (app DEC-246).
TEST_F(MultiFileTorrent, WithEveryWantedPieceThereTheSeederIsLetGo) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e01, 0);
    ASSERT_GE(e03, 0);

    ASSERT_TRUE(engine.select_file(id, e01));
    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e01) && finished(engine, id); }))
        << engine.get_status_json(id);
    ASSERT_TRUE(wait_for([&] { return peers_of(engine, id) == 0; }))
        << engine.get_status_json(id);

    ASSERT_TRUE(engine.select_file(id, e03));
    std::this_thread::sleep_for(std::chrono::seconds(3));
    EXPECT_EQ(peers_of(engine, id), 0) << engine.get_status_json(id);
    EXPECT_FALSE(completed.has(e03));
    EXPECT_EQ(status_of(engine, id)["selected_file"], e03);
    // What the file before had stays as it was.
    EXPECT_EQ(downloaded(kE01), seeded(kE01));
}

// The same pack with the app's configuration (app BUG-198, phase P5, step
// P5-03): the seeder is kept after the file is complete, so the next file of
// the pack starts at once. The 20 seconds are the limit of this test.
TEST_F(MultiFileTorrent, SecondFileOfAPackStartsWithinSecondsWithTheAppSettings) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port());
    config.session.close_redundant_connections = false;
    SeekServeEngine engine(config);
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e01, 0);
    ASSERT_GE(e03, 0);

    ASSERT_TRUE(engine.select_file(id, e01));
    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e01) && finished(engine, id); }))
        << engine.get_status_json(id);

    ASSERT_TRUE(engine.select_file(id, e03));
    const auto before = bytes_of(wanted_file(status_of(engine, id), e03));
    ASSERT_LT(before, size_of(kE03)) << "the next file was whole before it was selected";
    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(wait_for([&] { return bytes_of(wanted_file(status_of(engine, id), e03)) > before; },
                         std::chrono::seconds(60)))
        << engine.get_status_json(id);
    const auto waited = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started);
    EXPECT_LT(waited.count(), 20) << "the next file of the pack waited " << waited.count()
                                  << " s for its first byte";
}

// A player reads the selected file from the stream server while it comes,
// its last piece too, which is also the first of the next file.
TEST_F(MultiFileTorrent, TheReaderGetsTheBytesOfTheSelectedFile) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    ASSERT_TRUE(engine.start_server());

    const TorrentId id = add(engine, *seeder);
    const FileIndex e02 = index_of(engine, id, kE02);
    ASSERT_GE(e02, 0);
    ASSERT_TRUE(engine.select_file(id, e02));
    proxy.limit_download(0);
    auto url = engine.get_stream_url(id, e02);
    ASSERT_TRUE(url);

    const std::string expected = seeded(kE02);
    const auto size = expected.size();
    EXPECT_EQ(http_get_body(url.value(), "bytes=0-"), expected);
    EXPECT_EQ(http_get_body(url.value(), "bytes=" + std::to_string(size - 100) + "-"),
              expected.substr(size - 100));
}

// The start of a stream with priority (app BUG-228, DEC-656): with it on,
// the player reads its file near the start and from a point far in and gets
// the right bytes, and the file then downloads to its last byte: the
// priorities come back after the start, and none is zero in between.
TEST_F(MultiFileTorrent, AStartWithPriorityGivesTheRightBytesAndTheWholeFile) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port());
    config.scheduler.startup_focus_bytes = 8 * kPiece;
    config.scheduler.startup_focus_timeout = std::chrono::seconds(2);
    SeekServeEngine engine(config);
    ASSERT_TRUE(engine.start_server());
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e02 = index_of(engine, id, kE02);
    ASSERT_GE(e02, 0);
    ASSERT_TRUE(engine.select_file(id, e02));
    auto url = engine.get_stream_url(id, e02);
    ASSERT_TRUE(url);

    const std::string expected = seeded(kE02);
    const std::size_t length = 4 * kPiece;
    // From the file's second piece: its first one is shared with the file
    // before, which is not wanted, and read while it downloads its part of
    // that piece comes back as zeros, with or without this start (a
    // separate problem, app BUG-236).
    const std::size_t start = static_cast<std::size_t>(
        kPiece - (ti_->files().file_offset(lt::file_index_t{e02}) % kPiece));
    const std::size_t far = expected.size() / 2;
    EXPECT_EQ(http_get_body(url.value(), "bytes=" + std::to_string(start) + "-" +
                                             std::to_string(start + length - 1)),
              expected.substr(start, length));
    EXPECT_EQ(http_get_body(url.value(), "bytes=" + std::to_string(far) + "-" +
                                             std::to_string(far + length - 1)),
              expected.substr(far, length));

    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e02); })) << engine.get_status_json(id);
    EXPECT_TRUE(downloaded(kE02) == seeded(kE02));
}

// select_file is one selection: selecting another file takes the first one
// out, and what it had stays where it was. A torrent that wants two files,
// one for the player and one that downloads behind it, asks for them with
// set_wanted_files (below; app DEC-42, DEC-246).
TEST_F(MultiFileTorrent, SelectingAnotherFileStopsTheFirst) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    // Slow enough to act while the big file is still far from its end.
    proxy.limit_download(128 * 1024);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex e02 = index_of(engine, id, kE02);
    ASSERT_GE(e01, 0);
    ASSERT_GE(e02, 0);

    ASSERT_TRUE(engine.select_file(id, e02));
    ASSERT_TRUE(wait_for([&] { return status_of(engine, id).value("progress", 0.0) >= 0.05; }))
        << engine.get_status_json(id);
    ASSERT_LT(status_of(engine, id).value("progress", 0.0), 0.5) << "too fast to act in time";

    ASSERT_TRUE(engine.select_file(id, e01));
    ASSERT_TRUE(wait_for([&] { return completed.has(e01); }, std::chrono::seconds(40)))
        << engine.get_status_json(id);
    // Every wanted piece is there: the big file is not among the wanted.
    ASSERT_TRUE(wait_for([&] { return finished(engine, id); })) << engine.get_status_json(id);
    EXPECT_EQ(status_of(engine, id)["selected_file"], e01);
    EXPECT_EQ(downloaded(kE01), seeded(kE01));

    // And it stays so: nothing of it comes any more.
    std::this_thread::sleep_for(std::chrono::seconds(2));
    EXPECT_TRUE(finished(engine, id)) << engine.get_status_json(id);
    EXPECT_FALSE(completed.has(e02));
    EXPECT_NE(downloaded(kE02), seeded(kE02));
}

// What a restart keeps (resume data, app WORK-29 wave P2b): libtorrent saves
// the priorities of the files, so the torrent comes back wanting the file
// selected before and no other; the engine's own selection is not kept.
TEST_F(MultiFileTorrent, ResumeDataKeepsTheFilePrioritiesNotTheSelection) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port());
    config.session.resume_data = true;

    TorrentId id;
    FileIndex e01 = -1;
    FileIndex subtitles = -1;
    FileIndex e02 = -1;
    FileIndex e03 = -1;
    {
        SeekServeEngine engine(config);
        CompletedFiles completed(engine);
        id = add(engine, *seeder);
        e01 = index_of(engine, id, kE01);
        subtitles = index_of(engine, id, kE01Subtitles);
        e02 = index_of(engine, id, kE02);
        e03 = index_of(engine, id, kE03);
        ASSERT_GE(e01, 0);
        ASSERT_TRUE(engine.select_file(id, e01));
        proxy.limit_download(0);
        ASSERT_TRUE(wait_for([&] { return completed.has(e01) && completed.has(subtitles); }))
            << engine.get_status_json(id);
        // The engine's destructor writes the last resume data.
    }

    std::vector<char> stored;
    {
        OfflineCacheManager reader(config.cache);
        stored = reader.load_resume_data(id);
    }
    ASSERT_FALSE(stored.empty());
    lt::error_code ec;
    auto params = lt::read_resume_data(stored, ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_EQ(params.file_priorities.size(), std::size(kPack));
    auto priority = [&](FileIndex fi) {
        return params.file_priorities[static_cast<std::size_t>(fi)];
    };
    EXPECT_EQ(priority(e01), lt::default_priority);
    EXPECT_EQ(priority(subtitles), lt::default_priority);
    EXPECT_EQ(priority(e02), lt::dont_download);
    EXPECT_EQ(priority(e03), lt::dont_download);

    SeekServeEngine restored(config);
    ASSERT_TRUE(wait_for([&] { return status_of(restored, id).value("has_metadata", false); }))
        << restored.get_status_json(id);
    auto status = status_of(restored, id);
    EXPECT_EQ(status.value("paused", false), true);
    EXPECT_TRUE(status["selected_file"].is_null()) << status.dump();

    // The files of the status go by those priorities, not by the selection:
    // paused and with no file selected, it still says what the torrent has.
    ASSERT_TRUE(wait_for([&] { return whole(wanted_file(status_of(restored, id), e01)); }))
        << restored.get_status_json(id);
    status = status_of(restored, id);
    EXPECT_EQ(bytes_of(wanted_file(status, e01)), size_of(kE01)) << status.dump();
    EXPECT_TRUE(whole(wanted_file(status, subtitles))) << status.dump();
    EXPECT_TRUE(wanted_file(status, e02).is_null()) << status.dump();
    EXPECT_TRUE(wanted_file(status, e03).is_null()) << status.dump();
}

// A file deleted behind the engine (the app frees space) is not whole any
// more, and the status says so: "complete" goes by the pieces libtorrent has
// now. "offline_ready", the older flag of the selected file, is the cache's
// note that the file completed once: nothing takes it back, and it still
// says ready.
TEST_F(MultiFileTorrent, AFileThatWentIsNotWholeAnyMore) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port());
    config.session.resume_data = true;

    TorrentId id;
    FileIndex e01 = -1;
    {
        SeekServeEngine engine(config);
        CompletedFiles completed(engine);
        id = add(engine, *seeder);
        e01 = index_of(engine, id, kE01);
        ASSERT_GE(e01, 0);
        ASSERT_TRUE(engine.select_file(id, e01));
        proxy.limit_download(0);
        ASSERT_TRUE(wait_for([&] { return completed.has(e01); })) << engine.get_status_json(id);
        ASSERT_TRUE(whole(wanted_file(status_of(engine, id), e01))) << engine.get_status_json(id);
    }
    // Nobody to take it from again.
    seeder.reset();
    fs::remove(dir_ / "engine" / kFolder / kE01);

    SeekServeEngine restored(config);
    ASSERT_TRUE(wait_for([&] { return status_of(restored, id).value("has_metadata", false); }))
        << restored.get_status_json(id);
    ASSERT_TRUE(restored.select_file(id, e01));
    ASSERT_TRUE(restored.start_torrent(id));
    // libtorrent sees the file is gone and checks what is there.
    ASSERT_TRUE(wait_for([&] {
        const auto status = status_of(restored, id);
        const int state = status.value("state", 0);
        return state != static_cast<int>(lt::torrent_status::checking_files) &&
               state != static_cast<int>(lt::torrent_status::checking_resume_data) &&
               status.value("progress", 1.0) < 1.0;
    })) << restored.get_status_json(id);

    const auto status = status_of(restored, id);
    const auto video = wanted_file(status, e01);
    ASSERT_TRUE(video.is_object()) << status.dump();
    EXPECT_EQ(video["size"], size_of(kE01)) << status.dump();
    EXPECT_EQ(bytes_of(video), 0) << status.dump();
    EXPECT_FALSE(whole(video)) << status.dump();
    EXPECT_EQ(status["offline_ready"], true) << status.dump();
}

// App BUG-202: a file deleted while the engine has its torrent stays whole
// for libtorrent, which keeps the pieces it had in memory, and the app took
// the empty file for a finished download. Asked to check the torrent again,
// the engine finds the file gone and downloads it again. The check lets every
// peer go, and libtorrent takes one back only after min_reconnect_time: here
// a second, with the app's other choice (the seeder kept when a file is
// whole, app BUG-198), so the test waits for the check and not for that.
TEST_F(MultiFileTorrent, AFileDeletedWhileTheEngineRunsComesAgainAfterARecheck) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port());
    config.session.close_redundant_connections = false;
    config.session.min_reconnect_time = std::chrono::seconds(1);
    SeekServeEngine engine(config);
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e01, 0);
    ASSERT_GE(e03, 0);
    ASSERT_TRUE(engine.select_file(id, e03));
    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e03); })) << engine.get_status_json(id);

    // The queue moves on to another file of the pack, and the user deletes
    // the one that came: libtorrent still says it whole.
    proxy.limit_download(kTrickle);
    ASSERT_TRUE(engine.select_file(id, e01));
    fs::remove(dir_ / "engine" / kFolder / kE03);
    ASSERT_TRUE(engine.select_file(id, e03));
    ASSERT_TRUE(wait_for([&] { return whole(wanted_file(status_of(engine, id), e03)); }))
        << engine.get_status_json(id);

    ASSERT_TRUE(engine.recheck_torrent(id));
    ASSERT_TRUE(wait_for([&] {
        const auto status = status_of(engine, id);
        const int state = status.value("state", 0);
        return state != static_cast<int>(lt::torrent_status::checking_files) &&
               state != static_cast<int>(lt::torrent_status::checking_resume_data) &&
               !whole(wanted_file(status, e03));
    })) << engine.get_status_json(id);

    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return downloaded(kE03) == seeded(kE03); }))
        << engine.get_status_json(id);
    EXPECT_TRUE(wait_for([&] { return whole(wanted_file(status_of(engine, id), e03)); }))
        << engine.get_status_json(id);
}

// App BUG-202, the order of the owner's phone test (app P6-04): a file whose
// part of a shared piece went to its own file is deleted while the queue
// downloads another file, and checked again; that file comes, then the deleted
// one is wanted again. The check moves the deleted file to the part file,
// which holds only the other file's part of the shared piece: libtorrent's
// copy out of it reached the end of the part file, and the torrent went into
// error, paused, with no peer (seekserve's patch to libtorrent,
// patches/libtorrent). Not every run made that slot the last one, so the test
// failed about one run in two before the patch.
TEST_F(MultiFileTorrent, AfterARecheckTheDeletedFileComesAgainAfterTheNext) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port());
    config.session.close_redundant_connections = false;
    config.session.min_reconnect_time = std::chrono::seconds(1);
    SeekServeEngine engine(config);
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e01, 0);
    ASSERT_GE(e03, 0);
    ASSERT_TRUE(engine.select_file(id, e03));
    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e03); })) << engine.get_status_json(id);

    proxy.limit_download(kTrickle);
    ASSERT_TRUE(engine.select_file(id, e01));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    fs::remove(dir_ / "engine" / kFolder / kE03);
    ASSERT_TRUE(engine.recheck_torrent(id));
    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e01); })) << engine.get_status_json(id);

    ASSERT_TRUE(engine.select_file(id, e03));
    EXPECT_TRUE(wait_for([&] { return downloaded(kE03) == seeded(kE03); }))
        << engine.get_status_json(id);
    EXPECT_TRUE(wait_for([&] { return whole(wanted_file(status_of(engine, id), e03)); }))
        << engine.get_status_json(id);
}

// libtorrent puts a torrent into error, and pauses it, when it cannot write a
// file: here a folder takes the file's place. Nothing said so, and the app saw
// a download with no data (app BUG-202, after the phone test P6-04; app
// DEC-323). The status says it under its own key: "error" already means a
// torrent the engine does not have.
TEST_F(MultiFileTorrent, TheStatusSaysLibtorrentsError) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));

    fs::create_directories(dir_ / "engine" / kFolder / kE03);
    const TorrentId id = add(engine, *seeder);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e03, 0);
    ASSERT_TRUE(engine.select_file(id, e03));
    ASSERT_TRUE(wait_for([&] { return status_of(engine, id).contains("torrent_error"); }))
        << engine.get_status_json(id);

    const auto status = status_of(engine, id);
    const auto& error = status["torrent_error"];
    EXPECT_FALSE(error.value("message", std::string()).empty()) << status.dump();
    EXPECT_EQ(error.value("file", -1), e03) << status.dump();
    EXPECT_TRUE(status.value("paused", false)) << status.dump();
    EXPECT_FALSE(status.contains("error")) << status.dump();
}

// A torrent in error stays in error through a plain resume: libtorrent wants
// clear_error first. The app's queue starts a download again after a stall,
// so starting it is trying again (app DEC-323).
TEST_F(MultiFileTorrent, StartingATorrentInErrorTriesAgain) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    // The recheck lets the seeder go; with the app's settings it comes back
    // after min_reconnect_time, a second here.
    auto config = engine_config(proxy.port());
    config.session.close_redundant_connections = false;
    config.session.min_reconnect_time = std::chrono::seconds(1);
    SeekServeEngine engine(config);

    fs::create_directories(dir_ / "engine" / kFolder / kE03);
    const TorrentId id = add(engine, *seeder);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e03, 0);
    ASSERT_TRUE(engine.select_file(id, e03));
    ASSERT_TRUE(wait_for([&] { return status_of(engine, id).contains("torrent_error"); }))
        << engine.get_status_json(id);

    fs::remove(dir_ / "engine" / kFolder / kE03);
    ASSERT_FALSE(fs::exists(dir_ / "engine" / kFolder / kE03));
    ASSERT_TRUE(engine.start_torrent(id));
    EXPECT_TRUE(wait_for([&] { return downloaded(kE03) == seeded(kE03); }))
        << engine.get_status_json(id);
    EXPECT_FALSE(status_of(engine, id).contains("torrent_error")) << engine.get_status_json(id);
}

// The same through the queue's own way in (resume_torrent, auto managed).
TEST_F(MultiFileTorrent, ResumingATorrentInErrorTriesAgain) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    // The recheck lets the seeder go; with the app's settings it comes back
    // after min_reconnect_time, a second here.
    auto config = engine_config(proxy.port());
    config.session.close_redundant_connections = false;
    config.session.min_reconnect_time = std::chrono::seconds(1);
    SeekServeEngine engine(config);

    fs::create_directories(dir_ / "engine" / kFolder / kE03);
    const TorrentId id = add(engine, *seeder);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e03, 0);
    ASSERT_TRUE(engine.select_file(id, e03));
    ASSERT_TRUE(wait_for([&] { return status_of(engine, id).contains("torrent_error"); }))
        << engine.get_status_json(id);

    fs::remove(dir_ / "engine" / kFolder / kE03);
    ASSERT_FALSE(fs::exists(dir_ / "engine" / kFolder / kE03));
    ASSERT_TRUE(engine.resume_torrent(id));
    EXPECT_TRUE(wait_for([&] { return downloaded(kE03) == seeded(kE03); }))
        << engine.get_status_json(id);
    EXPECT_FALSE(status_of(engine, id).contains("torrent_error")) << engine.get_status_json(id);
}

// A pack may want two files at once (app WORK-29, phase P4, step P4-03; app
// DEC-42): the one a player reads and the one that downloads behind it. Both
// come, each whole, the piece they share too, and nothing else does.
TEST_F(MultiFileTorrent, TheFileBehindThePlayerDownloadsToo) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex subtitles = index_of(engine, id, kE01Subtitles);
    const FileIndex e02 = index_of(engine, id, kE02);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e01, 0);
    ASSERT_GE(subtitles, 0);
    ASSERT_GE(e02, 0);
    ASSERT_GE(e03, 0);

    ASSERT_TRUE(engine.set_wanted_files(id, e02, e03));
    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e02) && completed.has(e03); }))
        << engine.get_status_json(id);
    // Finished, not seeding: the first episode is not wanted.
    ASSERT_TRUE(wait_for([&] { return finished(engine, id); })) << engine.get_status_json(id);

    const auto status = status_of(engine, id);
    // The file a player reads is the selected one, as select_file makes it.
    EXPECT_EQ(status["selected_file"], e02) << status.dump();
    EXPECT_EQ(role_of(wanted_file(status, e02)), "reader") << status.dump();
    EXPECT_EQ(role_of(wanted_file(status, e03)), "background") << status.dump();
    EXPECT_EQ(role_of(wanted_file(status, subtitles)), "") << status.dump();
    EXPECT_TRUE(whole(wanted_file(status, e02))) << status.dump();
    EXPECT_TRUE(whole(wanted_file(status, e03))) << status.dump();
    EXPECT_TRUE(wanted_file(status, e01).is_null()) << status.dump();
    EXPECT_TRUE(downloaded(kE02) == seeded(kE02));
    EXPECT_TRUE(downloaded(kE03) == seeded(kE03));
    EXPECT_FALSE(completed.has(e01));
    EXPECT_FALSE(downloaded(kE01) == seeded(kE01));
}

// The video first (app DEC-246): while the file a player reads is not whole,
// the one behind it gets what is left, which with one seeder is next to
// nothing; once the reader's file is whole it goes on by itself, with no
// other call.
TEST_F(MultiFileTorrent, ThePlayersFileComesFirst) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(256 * 1024);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex e02 = index_of(engine, id, kE02);
    ASSERT_GE(e01, 0);
    ASSERT_GE(e02, 0);

    // The small one in the player, the big one behind it.
    ASSERT_TRUE(engine.set_wanted_files(id, e01, e02));
    ASSERT_TRUE(wait_for([&] { return completed.has(e01); })) << engine.get_status_json(id);
    {
        const auto status = status_of(engine, id);
        EXPECT_LT(bytes_of(wanted_file(status, e02)), size_of(kE02) / 4) << status.dump();
        EXPECT_FALSE(whole(wanted_file(status, e02))) << status.dump();
        EXPECT_LT(status.value("progress", 1.0), 1.0) << status.dump();
    }

    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e02); })) << engine.get_status_json(id);
    ASSERT_TRUE(wait_for([&] { return finished(engine, id); })) << engine.get_status_json(id);
    EXPECT_TRUE(downloaded(kE02) == seeded(kE02));
}

// The player is not disturbed when the queue moves on: the file behind the
// reader changes, and the stream the player holds goes on to its last byte.
TEST_F(MultiFileTorrent, TheReaderKeepsItsStreamWhenTheFileBehindItChanges) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(256 * 1024);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    ASSERT_TRUE(engine.start_server());
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex e02 = index_of(engine, id, kE02);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e01, 0);
    ASSERT_GE(e02, 0);
    ASSERT_GE(e03, 0);

    ASSERT_TRUE(engine.set_wanted_files(id, e02, e01));
    auto url = engine.get_stream_url(id, e02);
    ASSERT_TRUE(url);

    std::string body;
    std::thread player([&] { body = http_get_body(url.value(), "bytes=0-"); });
    // Under way, and far from its end.
    const bool under_way = wait_for([&] {
        return bytes_of(wanted_file(status_of(engine, id), e02)) > size_of(kE02) / 10;
    });
    const bool early = bytes_of(wanted_file(status_of(engine, id), e02)) < size_of(kE02) / 2;
    const bool changed = static_cast<bool>(engine.set_wanted_files(id, e02, e03));
    proxy.limit_download(0);
    player.join();
    ASSERT_TRUE(under_way) << engine.get_status_json(id);
    ASSERT_TRUE(early) << "too fast to act in time";
    ASSERT_TRUE(changed);

    const std::string expected = seeded(kE02);
    EXPECT_EQ(body.size(), expected.size());
    EXPECT_TRUE(body == expected);

    ASSERT_TRUE(wait_for([&] { return completed.has(e03); })) << engine.get_status_json(id);
    const auto status = status_of(engine, id);
    EXPECT_EQ(status["selected_file"], e02) << status.dump();
    EXPECT_EQ(role_of(wanted_file(status, e03)), "background") << status.dump();
    EXPECT_TRUE(wanted_file(status, e01).is_null()) << status.dump();
    EXPECT_FALSE(completed.has(e01));
}

// The player closes: its file is not wanted any more, not even the pieces
// its deadlines were waiting for, nobody serves it, and the file behind it
// goes on to its end, the piece it shares with the reader's file included.
TEST_F(MultiFileTorrent, WhenThePlayerLeavesItsFileStops) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port());
    // Deadlines far ahead of what the trickle brings: a hundred pieces wait
    // for theirs when the player leaves.
    config.scheduler.lookahead_pieces = 100;
    config.scheduler.deadline_budget = 120;
    SeekServeEngine engine(config);
    ASSERT_TRUE(engine.start_server());
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e02 = index_of(engine, id, kE02);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e02, 0);
    ASSERT_GE(e03, 0);

    ASSERT_TRUE(engine.set_wanted_files(id, e02, e03));
    auto url = engine.get_stream_url(id, e02);
    ASSERT_TRUE(url);
    ASSERT_TRUE(wait_for([&] { return status_of(engine, id).value("active_deadlines", 0) >= 90; }))
        << engine.get_status_json(id);

    ASSERT_TRUE(engine.set_wanted_files(id, -1, e03));
    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e03); })) << engine.get_status_json(id);
    ASSERT_TRUE(wait_for([&] { return finished(engine, id); })) << engine.get_status_json(id);
    // And it stays so: nothing of the reader's file comes any more.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    const auto status = status_of(engine, id);
    EXPECT_TRUE(finished(engine, id)) << status.dump();
    EXPECT_EQ(status["selected_file"], e03) << status.dump();
    EXPECT_EQ(role_of(wanted_file(status, e03)), "background") << status.dump();
    EXPECT_TRUE(wanted_file(status, e02).is_null()) << status.dump();
    EXPECT_FALSE(status.contains("active_deadlines")) << status.dump();
    EXPECT_TRUE(downloaded(kE03) == seeded(kE03));
    EXPECT_FALSE(completed.has(e02));
    // Of the hundred pieces with a deadline, only what was on its way.
    EXPECT_LT(stretches_there(kE02, 100), 60);
    EXPECT_EQ(http_status(url.value(), "bytes=0-99"), 404);
}

// When the player leaves, what it asked for and has not come yet is
// cancelled (app BUG-201, DEC-330): libtorrent keeps the requests a deadline
// made when the deadline goes or the file's priority drops, and on the phone
// the file left went on coming for 5 to 10 seconds after the close (app
// P5-06, P5-09). Counted by the cancels the seeder gets for the pieces of the
// file left, not by the bytes that still come: the bytes already on their
// way come anyway, and how many depends on the speed.
TEST_F(MultiFileTorrent, WhenThePlayerLeavesWhatItAskedForIsCancelled) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    lt::settings_pack log;
    log.set_int(lt::settings_pack::alert_mask, lt::alert_category::peer_log);
    seeder->apply_settings(log);
    auto config = engine_config(proxy.port());
    config.session.close_redundant_connections = false;
    // Deadlines far ahead of what the trickle brings, as in the test above.
    config.scheduler.lookahead_pieces = 100;
    config.scheduler.deadline_budget = 120;
    SeekServeEngine engine(config);
    ASSERT_TRUE(engine.start_server());

    const TorrentId id = add(engine, *seeder);
    const FileIndex e02 = index_of(engine, id, kE02);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e02, 0);
    ASSERT_GE(e03, 0);
    // The pieces of the reader's file alone, none shared with another file.
    auto const& files = ti_->layout();
    const int first = static_cast<int>(
        files.map_file(lt::file_index_t{e02}, 0, 1).piece) + 1;
    const int last = static_cast<int>(
        files.map_file(lt::file_index_t{e02}, files.file_size(lt::file_index_t{e02}) - 1, 1).piece) - 1;
    ASSERT_LT(first, last);

    ASSERT_TRUE(engine.set_wanted_files(id, e02, e03));
    auto url = engine.get_stream_url(id, e02);
    ASSERT_TRUE(url);
    ASSERT_TRUE(wait_for([&] { return status_of(engine, id).value("active_deadlines", 0) >= 90; }))
        << engine.get_status_json(id);

    // The cancels of the pieces of the reader's file the seeder got since
    // the last call.
    std::vector<lt::alert*> alerts;
    auto cancels = [&] {
        int count = 0;
        seeder->pop_alerts(&alerts);
        for (lt::alert* a : alerts) {
            auto* entry = lt::alert_cast<lt::peer_log_alert>(a);
            if (!entry || entry->direction != lt::peer_log_alert::incoming_message ||
                entry->event_type != lt::peer_log_alert::cancel) {
                continue;
            }
            int piece = -1;
            if (std::sscanf(entry->log_message(), "piece: %d", &piece) == 1 &&
                piece >= first && piece <= last) {
                ++count;
            }
        }
        return count;
    };
    // A first deadline cancels what was asked before it, while the player
    // starts: a whole second without a cancel first, so none of those is
    // counted as the leave's.
    cancels();
    ASSERT_TRUE(wait_for([&] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return cancels() == 0;
    })) << "the seeder kept getting cancels while the player read";

    ASSERT_TRUE(engine.set_wanted_files(id, -1, e03));

    int cancelled = 0;
    wait_for([&] {
        cancelled += cancels();
        return cancelled > 0;
    }, std::chrono::seconds(3));
    EXPECT_GT(cancelled, 0) << engine.get_status_json(id);
}

// The player closes on the file the queue is downloading: the file goes on
// as a download, without the stream and without the deadlines. A piece that
// keeps its deadline is asked for before any other, so the file would still
// come in the order the player wanted; without it, the pieces that had one
// are the last (libtorrent leaves them at the lowest priority).
TEST_F(MultiFileTorrent, TheReadersFileGoesOnAsADownload) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port());
    // Deadlines far ahead of what the trickle brings.
    config.scheduler.lookahead_pieces = 100;
    config.scheduler.deadline_budget = 120;
    SeekServeEngine engine(config);
    ASSERT_TRUE(engine.start_server());
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e02 = index_of(engine, id, kE02);
    ASSERT_GE(e02, 0);

    ASSERT_TRUE(engine.set_wanted_files(id, e02, -1));
    auto url = engine.get_stream_url(id, e02);
    ASSERT_TRUE(url);
    ASSERT_TRUE(wait_for([&] { return status_of(engine, id).value("active_deadlines", 0) >= 90; }))
        << engine.get_status_json(id);
    EXPECT_EQ(role_of(wanted_file(status_of(engine, id), e02)), "reader");

    ASSERT_TRUE(engine.set_wanted_files(id, -1, e02));
    {
        const auto status = status_of(engine, id);
        EXPECT_EQ(status["selected_file"], e02) << status.dump();
        EXPECT_EQ(role_of(wanted_file(status, e02)), "background") << status.dump();
        EXPECT_FALSE(status.contains("active_deadlines")) << status.dump();
        EXPECT_EQ(http_status(url.value(), "bytes=0-99"), 404);
    }

    // Half of the file later, the hundred pieces that had a deadline are
    // not the ones that came.
    proxy.limit_download(512 * 1024);
    ASSERT_TRUE(wait_for([&] {
        return bytes_of(wanted_file(status_of(engine, id), e02)) >= size_of(kE02) / 2;
    })) << engine.get_status_json(id);
    EXPECT_LT(stretches_there(kE02, 100), 60) << engine.get_status_json(id);

    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e02); })) << engine.get_status_json(id);
    EXPECT_TRUE(downloaded(kE02) == seeded(kE02));
}

// select_file still means this file and no other: after it, neither of the
// two files wanted before is.
TEST_F(MultiFileTorrent, SelectingAFileStillMeansOnlyThatFile) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    CompletedFiles completed(engine);

    const TorrentId id = add(engine, *seeder);
    const FileIndex e01 = index_of(engine, id, kE01);
    const FileIndex e02 = index_of(engine, id, kE02);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e01, 0);
    ASSERT_GE(e02, 0);
    ASSERT_GE(e03, 0);

    ASSERT_TRUE(engine.set_wanted_files(id, e02, e03));
    ASSERT_TRUE(wait_for([&] {
        const auto status = status_of(engine, id);
        return role_of(wanted_file(status, e02)) == "reader" &&
               role_of(wanted_file(status, e03)) == "background";
    })) << engine.get_status_json(id);

    ASSERT_TRUE(engine.select_file(id, e01));
    proxy.limit_download(0);
    ASSERT_TRUE(wait_for([&] { return completed.has(e01); })) << engine.get_status_json(id);
    ASSERT_TRUE(wait_for([&] { return finished(engine, id); })) << engine.get_status_json(id);

    const auto status = status_of(engine, id);
    EXPECT_EQ(status["selected_file"], e01) << status.dump();
    EXPECT_EQ(role_of(wanted_file(status, e01)), "reader") << status.dump();
    EXPECT_EQ(status["files"].size(), 2u) << status.dump();
    EXPECT_TRUE(wanted_file(status, e02).is_null()) << status.dump();
    EXPECT_TRUE(wanted_file(status, e03).is_null()) << status.dump();
    EXPECT_FALSE(completed.has(e02));
    EXPECT_FALSE(completed.has(e03));
}

// A file that is not there, or a torrent that is not, or one whose files are
// not known yet, is an error that changes nothing: what was wanted stays
// wanted and the player keeps its stream.
TEST_F(MultiFileTorrent, WantedFilesThatAreNotThereChangeNothing) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    ASSERT_TRUE(engine.start_server());

    const TorrentId id = add(engine, *seeder);
    const FileIndex e02 = index_of(engine, id, kE02);
    const FileIndex e03 = index_of(engine, id, kE03);
    ASSERT_GE(e02, 0);
    ASSERT_GE(e03, 0);
    ASSERT_TRUE(engine.set_wanted_files(id, e02, e03));
    auto url = engine.get_stream_url(id, e02);
    ASSERT_TRUE(url);

    auto no_such_torrent = engine.set_wanted_files(std::string(40, 'f'), 0, -1);
    ASSERT_FALSE(no_such_torrent);
    EXPECT_EQ(no_such_torrent.error(), make_error_code(errc::torrent_not_found));

    // A magnet nobody answers: its files are never known.
    auto bare = engine.add_torrent("magnet:?xt=urn:btih:" + std::string(40, 'a'));
    ASSERT_TRUE(bare);
    auto no_files_yet = engine.set_wanted_files(bare.value(), 0, -1);
    ASSERT_FALSE(no_files_yet);
    EXPECT_EQ(no_files_yet.error(), make_error_code(errc::metadata_not_ready));

    for (const auto& [reader, background] :
         {std::pair<FileIndex, FileIndex>{e02, 99}, {99, e03}, {-2, e03}, {e02, -2}}) {
        auto no_such_file = engine.set_wanted_files(id, reader, background);
        ASSERT_FALSE(no_such_file) << reader << ", " << background;
        EXPECT_EQ(no_such_file.error(), make_error_code(errc::file_not_found));
    }

    const auto status = status_of(engine, id);
    EXPECT_EQ(status["selected_file"], e02) << status.dump();
    ASSERT_TRUE(wait_for([&] {
        const auto now = status_of(engine, id);
        return role_of(wanted_file(now, e02)) == "reader" &&
               role_of(wanted_file(now, e03)) == "background";
    })) << engine.get_status_json(id);
    proxy.limit_download(0);
    EXPECT_EQ(http_status(url.value(), "bytes=0-99"), 206);
}

// No player survives a restart (resume data, app WORK-29 wave P2b): the file
// one was reading ahead of a download is not wanted when the torrent comes
// back, so it cannot download by itself when the queue starts the torrent
// again; the file that downloaded behind it still is.
TEST_F(MultiFileTorrent, ARestartLeavesThePlayersFileOff) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(kTrickle);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port());
    config.session.resume_data = true;

    TorrentId id;
    FileIndex e01 = -1;
    FileIndex subtitles = -1;
    FileIndex e02 = -1;
    FileIndex e03 = -1;
    {
        SeekServeEngine engine(config);
        id = add(engine, *seeder);
        e01 = index_of(engine, id, kE01);
        subtitles = index_of(engine, id, kE01Subtitles);
        e02 = index_of(engine, id, kE02);
        e03 = index_of(engine, id, kE03);
        ASSERT_GE(e02, 0);
        ASSERT_GE(e03, 0);
        ASSERT_TRUE(engine.set_wanted_files(id, e02, e03));
        // Known to libtorrent, which is what the resume data keeps.
        ASSERT_TRUE(wait_for([&] {
            const auto status = status_of(engine, id);
            return status.contains("files") && status["files"].size() == 3 &&
                   role_of(wanted_file(status, e02)) == "reader";
        })) << engine.get_status_json(id);
        // The engine's destructor writes the last resume data.
    }

    std::vector<char> stored;
    {
        OfflineCacheManager reader(config.cache);
        stored = reader.load_resume_data(id);
    }
    ASSERT_FALSE(stored.empty());
    lt::error_code ec;
    auto params = lt::read_resume_data(stored, ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_EQ(params.file_priorities.size(), std::size(kPack));
    // On disk the reader's file is ahead of the other.
    EXPECT_EQ(params.file_priorities[static_cast<std::size_t>(e02)], lt::top_priority);
    EXPECT_EQ(params.file_priorities[static_cast<std::size_t>(e03)], lt::default_priority);

    SeekServeEngine restored(config);
    ASSERT_TRUE(wait_for([&] { return status_of(restored, id).value("has_metadata", false); }))
        << restored.get_status_json(id);
    const auto status = status_of(restored, id);
    EXPECT_EQ(status.value("paused", false), true) << status.dump();
    EXPECT_TRUE(status["selected_file"].is_null()) << status.dump();
    ASSERT_TRUE(status.contains("files")) << status.dump();
    EXPECT_TRUE(wanted_file(status, e03).is_object()) << status.dump();
    EXPECT_TRUE(wanted_file(status, subtitles).is_object()) << status.dump();
    EXPECT_TRUE(wanted_file(status, e02).is_null()) << status.dump();
    EXPECT_TRUE(wanted_file(status, e01).is_null()) << status.dump();
    // Whose file it was is the engine's own note, gone with the restart.
    EXPECT_EQ(role_of(wanted_file(status, e03)), "") << status.dump();
}

// Asking for the status of a torrent at the moment it is removed is an
// answer, never an exception (app BUG-199): the status call checked the
// handle and then asked libtorrent, which throws once the torrent is gone,
// and nothing between there and the app's service catches it. The moment is
// short: this goes round many times, with several askers.
TEST_F(MultiFileTorrent, TheStatusOfATorrentBeingRemovedIsAnAnswer) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    SeekServeEngine engine(engine_config(proxy.port()));
    // Nobody to ask for it: the torrent only has to be there.
    const auto& v1 = ti_->info_hashes().v1;
    const TorrentId id = lt::aux::to_hex({v1.data(), static_cast<std::ptrdiff_t>(v1.size())});
    const std::string uri = "magnet:?xt=urn:btih:" + id;

    std::atomic<bool> stop{false};
    std::atomic<int> thrown{0};
    std::atomic<long> asked{0};
    std::vector<std::thread> askers;
    for (int i = 0; i < 4; ++i) {
        askers.emplace_back([&] {
            while (!stop) {
                try {
                    engine.get_status_json(id);
                } catch (const std::exception&) {
                    ++thrown;
                }
                ++asked;
            }
        });
    }

    int added = 0;
    for (int round = 0; round < 400; ++round) {
        auto result = engine.add_torrent(uri);
        if (!result || result.value() != id) break;
        ++added;
        engine.remove_torrent(id, false);
    }
    stop = true;
    for (auto& asker : askers) asker.join();

    EXPECT_EQ(added, 400);
    EXPECT_GT(asked.load(), 400);
    EXPECT_EQ(thrown.load(), 0);
}

// The same for the files of a torrent that is being removed: choosing them
// asks libtorrent several things, any of which throws once the torrent is
// gone. An error then, as for a torrent that is not there.
TEST_F(MultiFileTorrent, ChoosingTheFilesOfATorrentBeingRemovedIsAnError) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    SeekServeEngine engine(engine_config(proxy.port()));
    // From a .torrent file: its files are known as soon as it is added, and
    // nobody is asked for anything.
    const auto path = (dir_ / "pack.torrent").string();
    std::ofstream(path, std::ios::binary)
        .write(torrent_file_.data(), static_cast<std::streamsize>(torrent_file_.size()));
    const auto& v1 = ti_->info_hashes().v1;
    const TorrentId id = lt::aux::to_hex({v1.data(), static_cast<std::ptrdiff_t>(v1.size())});

    std::atomic<bool> stop{false};
    std::atomic<int> thrown{0};
    std::atomic<long> chosen{0};
    std::vector<std::thread> choosers;
    for (int i = 0; i < 4; ++i) {
        choosers.emplace_back([&, i] {
            while (!stop) {
                try {
                    if (i % 2 == 0) {
                        if (engine.set_wanted_files(id, 2, 0)) ++chosen;
                    } else {
                        if (engine.select_file(id, 0)) ++chosen;
                    }
                } catch (const std::exception&) {
                    ++thrown;
                }
            }
        });
    }

    int rounds = 0;
    for (; rounds < 150; ++rounds) {
        auto result = engine.add_torrent(path);
        if (!result || result.value() != id) break;
        // Its files are there and somebody chose among them.
        const long before = chosen;
        if (!wait_for([&] { return chosen > before; })) break;
        engine.remove_torrent(id, false);
    }
    stop = true;
    for (auto& chooser : choosers) chooser.join();

    EXPECT_EQ(rounds, 150);
    EXPECT_EQ(thrown.load(), 0);
}

}  // namespace
}  // namespace seekserve
