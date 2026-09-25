#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/add_torrent_params.hpp>

#include "seekserve/types.hpp"
#include "seekserve/error.hpp"
#include "seekserve/config.hpp"
#include "seekserve/alert_dispatcher.hpp"

namespace seekserve {

struct AddTorrentParams {
    std::string uri;              // magnet: URI or .torrent file path
    std::string save_path;        // override default save path (optional)
};

class TorrentSessionManager {
public:
    explicit TorrentSessionManager(const SessionConfig& config);
    ~TorrentSessionManager();

    TorrentSessionManager(const TorrentSessionManager&) = delete;
    TorrentSessionManager& operator=(const TorrentSessionManager&) = delete;

    Result<TorrentId> add_torrent(const AddTorrentParams& params);
    Result<void> remove_torrent(const TorrentId& id, bool delete_files = false);

    lt::torrent_handle get_handle(const TorrentId& id) const;
    // The id a torrent was added under, carried in its userdata; nothing for
    // a handle whose torrent no longer exists or that add_torrent did not add.
    // Alert handlers use it: the torrent's hashes can change after the add
    // (a hybrid torrent added from a magnet with only its v1 hash gets the v2
    // hash with its metadata, app BUG-71).
    std::optional<TorrentId> id_of(const lt::torrent_handle& h) const;
    bool has_torrent(const TorrentId& id) const;
    std::vector<TorrentId> list_torrents() const;

    void set_proxy(const ProxyConfig& proxy);

    lt::session& session() { return *session_; }
    AlertDispatcher& alert_dispatcher() { return dispatcher_; }

private:
    static lt::settings_pack make_settings(const SessionConfig& config);
    static void apply_proxy_settings(lt::settings_pack& sp, const ProxyConfig& proxy);
    void load_tracker_cas(const std::string& ca_file);
    // Hex of the v2 hash when there is one, else of the v1 hash.
    static TorrentId id_from_hashes(const lt::info_hash_t& ih);

    SessionConfig config_;
    // Every id added in this session, never erased: a torrent's userdata
    // points to its entry, also after its removal (late alerts). Declared
    // before session_ so that it outlives the torrents.
    std::unordered_map<TorrentId, TorrentId> ids_;
    std::unique_ptr<lt::session> session_;
    AlertDispatcher dispatcher_;
    std::unordered_map<TorrentId, lt::torrent_handle> handles_;
    mutable std::mutex mu_;
};

} // namespace seekserve
