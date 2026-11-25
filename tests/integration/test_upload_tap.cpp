#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
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

}  // namespace
}  // namespace seekserve
