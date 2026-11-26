#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <nlohmann/json.hpp>

#include "seekserve/engine.hpp"
#include "seekserve/session_manager.hpp"

#include "loopback_socks5.hpp"

// The tap of a finished torrent (app DEC-995, DEC-996): a pack of two files
// seeded by a second session on 127.0.0.1, the engine behind LoopbackSocks5
// with no DHT, the seeder as its one peer.
namespace seekserve {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;
using test::LoopbackSocks5;

constexpr int kFileSize = 64 * 1024;

template <typename Pred>
bool wait_for(Pred pred, std::chrono::seconds timeout = std::chrono::seconds(20)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return pred();
}

class UploadTap : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("seekserve_tap_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir_ / "seed" / "pack");
        fs::create_directories(dir_ / "engine");
        for (int f = 0; f < 2; ++f) {
            std::vector<char> data(kFileSize);
            for (std::size_t i = 0; i < data.size(); ++i) {
                data[i] = static_cast<char>(i * 31 + 7 + f);
            }
            std::ofstream((dir_ / "seed" / "pack" / ("e" + std::to_string(f) + ".mkv")).string(),
                          std::ios::binary)
                .write(data.data(), static_cast<std::streamsize>(data.size()));
        }
        lt::create_torrent ct(lt::list_files((dir_ / "seed" / "pack").string()), 16 * 1024,
                              lt::create_torrent::v1_only);
        lt::set_piece_hashes(ct, (dir_ / "seed").string());
        auto torrent = ct.generate_buf();
        ti_ = lt::load_torrent_buffer(torrent).ti;
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    SeekServeEngine::Config engine_config(int proxy_port, bool share_when_finished) const {
        SeekServeEngine::Config config;
        config.session.save_path = (dir_ / "engine").string();
        config.session.enable_webtorrent = false;
        config.session.proxy.enabled = true;
        config.session.proxy.hostname = "127.0.0.1";
        config.session.proxy.port = proxy_port;
        config.session.share_when_finished = share_when_finished;
        config.session.peer_share_interval = std::chrono::seconds(1);
        config.cache.db_path = (dir_ / "cache.db").string();
        return config;
    }

    std::unique_ptr<lt::session> start_seeder() const {
        lt::settings_pack sp;
        sp.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
        sp.set_bool(lt::settings_pack::enable_dht, false);
        sp.set_bool(lt::settings_pack::enable_lsd, false);
        sp.set_bool(lt::settings_pack::enable_upnp, false);
        sp.set_bool(lt::settings_pack::enable_natpmp, false);
        // Every connection of the engine comes from 127.0.0.1 through the
        // proxy: one closing is not a duplicate of the next.
        sp.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
        auto seeder = std::make_unique<lt::session>(lt::session_params{sp});
        lt::add_torrent_params atp;
        atp.ti = ti_;
        atp.save_path = (dir_ / "seed").string();
        atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
        auto handle = seeder->add_torrent(std::move(atp));
        wait_for([&] { return handle.status().state == lt::torrent_status::seeding; });
        return seeder;
    }

    std::string magnet(const lt::session& seeder) const {
        auto const& v1 = ti_->info_hashes().v1;
        return "magnet:?xt=urn:btih:" +
               lt::aux::to_hex({v1.data(), static_cast<std::ptrdiff_t>(v1.size())}) +
               "&dn=pack&x.pe=127.0.0.1:" + std::to_string(seeder.listen_port());
    }

    static json status(SeekServeEngine& engine, const std::string& id) {
        return json::parse(engine.get_status_json(id));
    }

    static bool file_complete(SeekServeEngine& engine, const std::string& id, int index) {
        const auto s = status(engine, id);
        if (!s.contains("files")) return false;
        for (const auto& f : s["files"]) {
            if (f["index"] == index) return f["complete"].get<bool>();
        }
        return false;
    }

    fs::path dir_;
    std::shared_ptr<lt::torrent_info const> ti_;
};

// The first file whole, nothing else wanted: the tap closes. The second file
// wanted: the tap is open before the engine asks for it, and it comes.
TEST_F(UploadTap, AFinishedTorrentHasItsTapClosedAndOpenedWhenItWantsMore) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port(), /*share_when_finished=*/false));
    // The engine's tick, which keeps the taps, runs with its servers.
    ASSERT_TRUE(engine.start_server());

    auto added = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(added);
    const std::string id = added.value();
    ASSERT_TRUE(wait_for([&] { return static_cast<bool>(engine.select_file(id, 0)); }))
        << "no metadata from the seeder";
    ASSERT_TRUE(wait_for([&] { return file_complete(engine, id, 0); }))
        << engine.get_status_json(id);

    EXPECT_TRUE(wait_for([&] { return status(engine, id)["upload_tap_closed"] == true; },
                         std::chrono::seconds(5)))
        << engine.get_status_json(id);

    ASSERT_TRUE(engine.select_file(id, 1));
    // Open at once, before a tick: the requests do not wait behind the tap.
    EXPECT_EQ(status(engine, id)["upload_tap_closed"], false);
    EXPECT_TRUE(wait_for([&] { return file_complete(engine, id, 1); }))
        << engine.get_status_json(id);
    EXPECT_TRUE(wait_for([&] { return status(engine, id)["upload_tap_closed"] == true; },
                         std::chrono::seconds(5)))
        << engine.get_status_json(id);
}

// Sharing a finished torrent on, as before: the tap never closes.
TEST_F(UploadTap, WithSharingWhenFinishedTheTapStaysOpen) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port(), /*share_when_finished=*/true));
    ASSERT_TRUE(engine.start_server());

    auto added = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(added);
    const std::string id = added.value();
    ASSERT_TRUE(wait_for([&] { return static_cast<bool>(engine.select_file(id, 0)); }));
    ASSERT_TRUE(wait_for([&] { return file_complete(engine, id, 0); }));

    std::this_thread::sleep_for(std::chrono::seconds(3));
    EXPECT_EQ(status(engine, id)["upload_tap_closed"], false);
}

// What each kind of peer gave and got adds up to the torrent's own counts
// (app BUG-792): the seeder's connection closes when the first file is whole
// (close_redundant_connections) and opens again for the second, whose stream
// replaces the first. Nothing a connection gave between two readings is lost,
// and a new stream does not start the sum again.
TEST_F(UploadTap, WhatEachKindOfPeerGaveAddsUpToTheTorrentsCounts) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port(), /*share_when_finished=*/false));
    ASSERT_TRUE(engine.start_server());

    auto added = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(added);
    const std::string id = added.value();
    ASSERT_TRUE(wait_for([&] { return static_cast<bool>(engine.select_file(id, 0)); }));
    ASSERT_TRUE(wait_for([&] { return file_complete(engine, id, 0); }));
    ASSERT_TRUE(engine.select_file(id, 1));
    ASSERT_TRUE(wait_for([&] { return file_complete(engine, id, 1); }));

    auto sums = [&] {
        const auto s = status(engine, id);
        std::int64_t received = 0;
        std::int64_t sent = 0;
        if (s.contains("peer_share")) {
            for (const auto& [group, share] : s["peer_share"].items()) {
                received += share["received"].get<std::int64_t>();
                sent += share["sent"].get<std::int64_t>();
            }
        }
        return std::tuple{received, sent, s["all_time_download"].get<std::int64_t>(),
                          s["all_time_upload"].get<std::int64_t>()};
    };
    EXPECT_TRUE(wait_for(
        [&] {
            auto [received, sent, downloaded, uploaded] = sums();
            return downloaded >= 2 * kFileSize && received == downloaded && sent == uploaded;
        },
        std::chrono::seconds(10)))
        << engine.get_status_json(id);
}

// The status asks the readers (app BUG-792): during a slow download, with
// readings an hour apart, what the open connection gave shows within a tick
// of the status, not at the next reading or when the connection closes.
TEST_F(UploadTap, AStatusShowsWhatAnOpenConnectionGave) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    proxy.limit_download(16 * 1024);
    auto seeder = start_seeder();
    auto config = engine_config(proxy.port(), /*share_when_finished=*/true);
    config.session.peer_share_interval = std::chrono::seconds(3600);
    SeekServeEngine engine(config);
    ASSERT_TRUE(engine.start_server());

    auto added = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(added);
    const std::string id = added.value();
    ASSERT_TRUE(wait_for([&] { return static_cast<bool>(engine.select_file(id, 0)); }));

    // Half a file in, still connected: the groups hold what came, but for the
    // last tick (a second of the proxy's 16 KB/s, and a block on its way).
    bool seen = false;
    std::string last;
    EXPECT_TRUE(wait_for([&] {
        const auto s = status(engine, id);
        last = s.dump();
        const auto downloaded = s["all_time_download"].get<std::int64_t>();
        if (s["num_peers"] != 1 || downloaded < kFileSize / 2 || !s.contains("peer_share")) return false;
        std::int64_t sum = 0;
        for (const auto& [group, share] : s["peer_share"].items()) {
            sum += share["received"].get<std::int64_t>();
        }
        seen = sum >= downloaded - 32 * 1024;
        return seen;
    })) << last;
}

// libtorrent starts a connection to a peer it was connected to before at that
// peer's earlier totals (torrent_peer::prev_amount_download, app BUG-792): the
// session lets go of the seeder after it gave bytes, then connects to it
// again. What the seeder gave counts once. On the session manager, where the
// books are, with the seeder given back by hand: no tracker here gives it.
TEST_F(UploadTap, APeerConnectedAgainCountsWhatItGaveOnce) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    // Slow enough that the second file still comes after the pause.
    proxy.limit_download(32 * 1024);
    auto seeder = start_seeder();
    SessionConfig config;
    config.save_path = (dir_ / "engine").string();
    config.enable_webtorrent = false;
    config.proxy.enabled = true;
    config.proxy.port = proxy.port();
    config.peer_share_interval = std::chrono::seconds(1);
    // The seeder let go is taken back at once.
    config.min_reconnect_time = std::chrono::seconds(1);
    TorrentSessionManager sessions(config);

    // The first file only, from the start (the magnet's select-only).
    auto added = sessions.add_torrent({magnet(*seeder) + "&so=0"});
    ASSERT_TRUE(added);
    const std::string id = added.value();
    auto handle = sessions.get_handle(id);
    ASSERT_TRUE(wait_for([&] { return handle.status().is_finished; }));
    // Both files wanted, then the seeder let go while the second comes and
    // given back.
    handle.prioritize_files({lt::default_priority, lt::default_priority});
    ASSERT_TRUE(wait_for([&] { return !handle.status().is_finished; }));
    std::this_thread::sleep_for(std::chrono::seconds(2));
    handle.pause();
    ASSERT_TRUE(wait_for([&] { return handle.status().num_peers == 0; }));
    handle.resume();
    ASSERT_TRUE(wait_for([&] { return !(handle.flags() & lt::torrent_flags::paused); }));
    handle.connect_peer(lt::tcp::endpoint(lt::make_address_v4("127.0.0.1"), seeder->listen_port()));
    ASSERT_TRUE(wait_for([&] { return handle.status().is_seeding; }))
        << "peers " << handle.status().num_peers << ", progress " << handle.status().progress;
    ASSERT_TRUE(wait_for([&] { return sessions.peer_share(id)->readers().opened >= 2; }));

    std::this_thread::sleep_for(std::chrono::seconds(2));
    const auto shares = sessions.peer_share(id)->shares();
    std::int64_t received = 0;
    for (const auto& share : shares) received += share.received;
    // libtorrent drops from its own count what comes in the second of a
    // pause: at most a second of the proxy's 32 KB/s.
    const auto downloaded = handle.status().all_time_download;
    EXPECT_GE(received, downloaded);
    EXPECT_LE(received, downloaded + 32 * 1024)
        << "readers " << sessions.peer_share(id)->readers().opened;
}

}  // namespace
}  // namespace seekserve
