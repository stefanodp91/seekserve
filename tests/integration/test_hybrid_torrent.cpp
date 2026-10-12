#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
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

namespace seekserve {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr int kFileSize = 64 * 1024;

using test::LoopbackSocks5;

template <typename Pred>
bool wait_for(Pred pred, std::chrono::seconds timeout = std::chrono::seconds(20)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return pred();
}

// A hybrid torrent (v1 and v2 hashes) seeded by a second session on
// 127.0.0.1, and the engine fetching it from a magnet with only its v1 hash
// and the seeder as its one peer (x.pe). The engine has no DHT (proxy on) and
// connects only through LoopbackSocks5; the seeder has no DHT, LSD, UPnP or
// NAT-PMP; the magnet has no trackers.
class HybridTorrent : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("seekserve_hybrid_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir_ / "seed");
        fs::create_directories(dir_ / "engine");

        std::vector<char> data(kFileSize);
        for (std::size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<char>(i * 31 + 7);
        }
        std::ofstream((dir_ / "seed" / "video.mkv").string(), std::ios::binary)
            .write(data.data(), static_cast<std::streamsize>(data.size()));

        // Neither v1_only nor v2_only: libtorrent creates a hybrid torrent.
        lt::create_torrent ct(lt::list_files((dir_ / "seed" / "video.mkv").string()),
                              16 * 1024);
        lt::set_piece_hashes(ct, (dir_ / "seed").string());
        auto torrent = ct.generate_buf();
        std::ofstream((dir_ / "hybrid.torrent").string(), std::ios::binary)
            .write(torrent.data(), static_cast<std::streamsize>(torrent.size()));
        ti_ = lt::load_torrent_buffer(torrent).ti;
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
        // Not seed_mode: a seeder in seed mode does not serve the v2 hashes
        // the engine asks for, and the download stalls. Started at once: a
        // paused torrent refuses the engine's connection, and libtorrent
        // retries a refused peer only after a minute.
        atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
        auto handle = seeder->add_torrent(std::move(atp));
        wait_for([&] { return handle.status().state == lt::torrent_status::seeding; });
        return seeder;
    }

    // Only the v1 hash, and the seeder as the one peer.
    std::string magnet(const lt::session& seeder) const {
        return "magnet:?xt=urn:btih:" + v1_hex() + "&dn=video.mkv&x.pe=127.0.0.1:"
            + std::to_string(seeder.listen_port());
    }

    std::string v1_hex() const {
        auto const& v1 = ti_->info_hashes().v1;
        return lt::aux::to_hex({v1.data(), static_cast<std::ptrdiff_t>(v1.size())});
    }

    std::string v2_hex() const {
        auto const& v2 = ti_->info_hashes().v2;
        return lt::aux::to_hex({v2.data(), static_cast<std::ptrdiff_t>(v2.size())});
    }

    fs::path dir_;
    std::shared_ptr<lt::torrent_info const> ti_;
};

TEST_F(HybridTorrent, IsHybrid) {
    EXPECT_TRUE(ti_->info_hashes().has_v1());
    EXPECT_TRUE(ti_->info_hashes().has_v2());
}

// The engine's events of one type, by torrent id.
class EventLog {
public:
    explicit EventLog(SeekServeEngine& engine) : engine_(engine) {
        engine_.set_event_callback([this](const std::string& event) {
            std::lock_guard lock(mu_);
            events_.push_back(json::parse(event));
        });
    }

    // set_event_callback waits for a callback in progress.
    ~EventLog() { engine_.set_event_callback(nullptr); }

    std::vector<std::string> ids(const std::string& type) const {
        std::lock_guard lock(mu_);
        std::vector<std::string> ids;
        for (const auto& e : events_) {
            if (e["type"] == type) ids.push_back(e["data"]["torrent_id"]);
        }
        return ids;
    }

private:
    SeekServeEngine& engine_;
    mutable std::mutex mu_;
    std::vector<json> events_;
};

bool has_metadata(SeekServeEngine& engine, const TorrentId& id) {
    return json::parse(engine.get_status_json(id)).value("has_metadata", false);
}

// App BUG-71: when the metadata arrives libtorrent adds the v2 hash to the
// torrent, and the alert handlers used to derive the id from it, so the
// catalog, the events and the streaming state went under the v2 id while the
// app and every call use the v1 id add_torrent returned.
TEST_F(HybridTorrent, V1MagnetKeepsItsIdWhenTheMetadataArrives) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    EventLog events(engine);

    auto added = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(added);
    const std::string id = added.value();
    ASSERT_EQ(id, v1_hex());

    // Before the fix the metadata arrived, but under the v2 id.
    ASSERT_TRUE(wait_for([&] { return !events.ids("metadata_received").empty(); }))
        << "no metadata from the seeder";
    EXPECT_EQ(events.ids("metadata_received"), std::vector<std::string>{id})
        << "v2 id: " << v2_hex();
    EXPECT_TRUE(has_metadata(engine, id));

    auto files = engine.list_files(id);
    ASSERT_TRUE(files);
    ASSERT_EQ(files.value().size(), 1u);
    EXPECT_EQ(files.value()[0].size, kFileSize);

    ASSERT_TRUE(engine.select_file(id, 0));
    ASSERT_TRUE(wait_for([&] { return !events.ids("file_completed").empty(); }))
        << "the file did not complete " << engine.get_status_json(id);
    EXPECT_EQ(events.ids("file_completed"), std::vector<std::string>{id});

    auto status = json::parse(engine.get_status_json(id));
    EXPECT_EQ(status["torrent_id"], id);
    EXPECT_EQ(status["selected_file"], 0);
    EXPECT_EQ(status["offline_ready"], true);
}

// App BUG-68 with a hybrid torrent: removed and added again from the same
// magnet, it gets its metadata again under the same id.
TEST_F(HybridTorrent, V1MagnetAddedAgainAfterRemovalGetsItsMetadata) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));

    auto first = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(first);
    ASSERT_TRUE(wait_for([&] { return has_metadata(engine, first.value()); }));

    ASSERT_TRUE(engine.remove_torrent(first.value(), false));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto again = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(again);
    EXPECT_EQ(again.value(), first.value());
    EXPECT_TRUE(wait_for([&] { return has_metadata(engine, again.value()); }));
    EXPECT_TRUE(engine.list_files(again.value()));
}

// The same magnet added again once the metadata is in (a second tap in the
// app's search): libtorrent returns the torrent it has, which now knows its
// v2 hash too, and the id must stay the v1 one of the first add.
TEST_F(HybridTorrent, V1MagnetAddedTwiceKeepsItsId) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));
    EventLog events(engine);

    auto first = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(first);
    ASSERT_TRUE(wait_for([&] { return has_metadata(engine, first.value()); }));

    auto second = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(second);
    EXPECT_EQ(second.value(), first.value());
    EXPECT_EQ(engine.list_torrents(), std::vector<TorrentId>{first.value()});

    ASSERT_TRUE(engine.select_file(first.value(), 0));
    ASSERT_TRUE(wait_for([&] { return !events.ids("file_completed").empty(); }))
        << "the file did not complete " << engine.get_status_json(first.value());
    EXPECT_EQ(events.ids("file_completed"), std::vector<TorrentId>{first.value()});
}

// Removed after its v1 magnet, the torrent comes back from its .torrent file,
// so under its v2 id: the alerts of the new add must not be taken for late
// alerts of the removed one.
TEST_F(HybridTorrent, TorrentFileAfterV1MagnetRemovalGetsItsMetadata) {
    LoopbackSocks5 proxy;
    ASSERT_NE(proxy.port(), 0);
    auto seeder = start_seeder();
    SeekServeEngine engine(engine_config(proxy.port()));

    auto from_magnet = engine.add_torrent(magnet(*seeder));
    ASSERT_TRUE(from_magnet);
    ASSERT_TRUE(wait_for([&] { return has_metadata(engine, from_magnet.value()); }));
    ASSERT_TRUE(engine.remove_torrent(from_magnet.value(), false));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto from_file = engine.add_torrent((dir_ / "hybrid.torrent").string());
    ASSERT_TRUE(from_file);
    EXPECT_EQ(from_file.value(), v2_hex());
    EXPECT_TRUE(wait_for([&] { return has_metadata(engine, from_file.value()); },
                         std::chrono::seconds(5)));
}

}  // namespace
}  // namespace seekserve
