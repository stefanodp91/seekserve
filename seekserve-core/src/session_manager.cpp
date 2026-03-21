#include "seekserve/session_manager.hpp"

#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/hex.hpp>

#include <spdlog/spdlog.h>

#include <fstream>

namespace seekserve {

TorrentSessionManager::TorrentSessionManager(const SessionConfig& config)
    : config_(config)
{
    auto sp = make_settings(config);
    session_ = std::make_unique<lt::session>(lt::session_params{sp});
    dispatcher_.start(*session_);
    spdlog::info("TorrentSessionManager: session created (save_path={})", config_.save_path);
}

TorrentSessionManager::~TorrentSessionManager() {
    spdlog::info("TorrentSessionManager: shutting down");
    dispatcher_.stop();
    session_->abort();
}

lt::settings_pack TorrentSessionManager::make_settings(const SessionConfig& config) {
    lt::settings_pack sp;

    sp.set_int(lt::settings_pack::alert_mask,
        lt::alert_category::status
        | lt::alert_category::piece_progress
        | lt::alert_category::error
        | lt::alert_category::storage
        | lt::alert_category::dht);

    // Bind on all interfaces: required for BitTorrent peer connectivity.
    // Loopback-only binding breaks DHT and outbound peer connections.
    // LAN discovery protocols (UPnP/NAT-PMP/LSD) are disabled separately below.
    sp.set_str(lt::settings_pack::listen_interfaces,
        "0.0.0.0:" + std::to_string(config.listen_port_start) +
        ",[::0]:" + std::to_string(config.listen_port_start));

    // Disable LAN discovery protocols (UPnP, NAT-PMP, LSD)
    // These broadcast the engine's presence on the local network
    sp.set_bool(lt::settings_pack::enable_upnp, false);
    sp.set_bool(lt::settings_pack::enable_natpmp, false);
    sp.set_bool(lt::settings_pack::enable_lsd, false);

    // Prefer MSE/PE RC4 encryption but accept plaintext as fallback.
    // pe_forced excluded too many peers (old clients, seedboxes without MSE)
    // causing very low download speeds. Privacy is already guaranteed by
    // anonymous_mode, spoofed user_agent/peer_fingerprint, and DoH.
    sp.set_int(lt::settings_pack::out_enc_policy,
        lt::settings_pack::pe_enabled);
    sp.set_int(lt::settings_pack::in_enc_policy,
        lt::settings_pack::pe_enabled);
    sp.set_int(lt::settings_pack::allowed_enc_level,
        lt::settings_pack::pe_rc4);

    // Anonymous mode: suppresses client version in BEP-10 extension handshake,
    // uses generic user-agent toward trackers, omits private IPs from announces
    sp.set_bool(lt::settings_pack::anonymous_mode, true);

    // Override user-agent sent to HTTP trackers.
    // Generic string prevents client fingerprinting at the tracker layer.
    sp.set_str(lt::settings_pack::user_agent, "Mozilla/5.0");

    // Suppress client version field ("v") in BEP-10 extension handshake.
    // Empty string means libtorrent omits the field entirely.
    sp.set_str(lt::settings_pack::handshake_client_version, "");

    // Override peer ID prefix (first 8 bytes of the 20-byte peer ID).
    // Default "-lt{ver}-" identifies libtorrent; spoof as a common client
    // to prevent client fingerprinting. Remaining 12 bytes are random.
    sp.set_str(lt::settings_pack::peer_fingerprint, "-UT3600-");

    sp.set_int(lt::settings_pack::request_timeout, 10);
    sp.set_int(lt::settings_pack::peer_timeout, 30);
    sp.set_bool(lt::settings_pack::strict_end_game_mode, false);
    sp.set_bool(lt::settings_pack::announce_to_all_tiers, true);
    sp.set_bool(lt::settings_pack::announce_to_all_trackers, true);
    sp.set_bool(lt::settings_pack::enable_dht, true);

    sp.set_int(lt::settings_pack::alert_queue_size, config.alert_queue_size);

#ifdef TORRENT_USE_RTC
    if (config.enable_webtorrent) {
        sp.set_str(lt::settings_pack::webtorrent_stun_server, "stun.l.google.com:19302");
        spdlog::info("WebTorrent enabled (STUN: stun.l.google.com:19302)");
    }
#endif

    return sp;
}

Result<TorrentId> TorrentSessionManager::add_torrent(const AddTorrentParams& params) {
    lt::add_torrent_params atp;

    if (params.uri.substr(0, 7) == "magnet:") {
        lt::error_code ec;
        lt::parse_magnet_uri(params.uri, atp, ec);
        if (ec) {
            spdlog::error("Failed to parse magnet URI: {}", ec.message());
            return make_error_code(errc::invalid_argument);
        }
    } else {
        lt::error_code ec;
        atp = lt::load_torrent_file(params.uri, ec, lt::load_torrent_limits{});
        if (ec) {
            spdlog::error("Failed to load .torrent file '{}': {}", params.uri, ec.message());
            return make_error_code(errc::invalid_argument);
        }
    }

    atp.save_path = params.save_path.empty() ? config_.save_path : params.save_path;

    for (const auto& tracker : config_.extra_trackers) {
        atp.trackers.push_back(tracker);
    }

    lt::torrent_handle h = session_->add_torrent(std::move(atp));
    auto id = torrent_id_from_handle(h);

    {
        std::lock_guard lock(mu_);
        handles_[id] = h;
    }

    spdlog::info("TorrentSessionManager: added torrent {}", id);
    return id;
}

Result<void> TorrentSessionManager::remove_torrent(const TorrentId& id, bool delete_files) {
    std::lock_guard lock(mu_);
    auto it = handles_.find(id);
    if (it == handles_.end()) {
        return make_error_code(errc::torrent_not_found);
    }

    lt::remove_flags_t flags{};
    if (delete_files) {
        flags = lt::session::delete_files;
    }

    session_->remove_torrent(it->second, flags);
    handles_.erase(it);
    spdlog::info("TorrentSessionManager: removed torrent {}", id);
    return Result<void>{};
}

lt::torrent_handle TorrentSessionManager::get_handle(const TorrentId& id) const {
    std::lock_guard lock(mu_);
    auto it = handles_.find(id);
    if (it == handles_.end()) {
        return {};
    }
    return it->second;
}

bool TorrentSessionManager::has_torrent(const TorrentId& id) const {
    std::lock_guard lock(mu_);
    return handles_.count(id) > 0;
}

std::vector<TorrentId> TorrentSessionManager::list_torrents() const {
    std::lock_guard lock(mu_);
    std::vector<TorrentId> ids;
    ids.reserve(handles_.size());
    for (const auto& [id, _] : handles_) {
        ids.push_back(id);
    }
    return ids;
}

TorrentId TorrentSessionManager::torrent_id_from_handle(const lt::torrent_handle& h) const {
    auto status = h.status(lt::torrent_handle::query_name);
    auto ih = status.info_hashes;
    if (ih.has_v2()) {
        return lt::aux::to_hex({ih.v2.data(), static_cast<ptrdiff_t>(ih.v2.size())});
    }
    return lt::aux::to_hex({ih.v1.data(), static_cast<ptrdiff_t>(ih.v1.size())});
}

} // namespace seekserve
