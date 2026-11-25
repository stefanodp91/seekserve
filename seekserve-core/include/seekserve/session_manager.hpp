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
#include "seekserve/peer_share_plugin.hpp"
#include "seekserve/upload_tap.hpp"
#include "seekserve/alert_dispatcher.hpp"

namespace seekserve {

struct AddTorrentParams {
    std::string uri;              // magnet: URI or .torrent file path
    std::string save_path;        // override default save path (optional)
    bool paused = false;          // added without starting (a restored torrent)
};

class TorrentSessionManager {
public:
    explicit TorrentSessionManager(const SessionConfig& config);
    ~TorrentSessionManager();

    TorrentSessionManager(const TorrentSessionManager&) = delete;
    TorrentSessionManager& operator=(const TorrentSessionManager&) = delete;

    Result<TorrentId> add_torrent(const AddTorrentParams& params);
    // Adds a torrent again from its resume data (decode_resume_data), under
    // the id it had: the id is not derived from the hashes again, which
    // for a hybrid torrent added by its v1 hash would give another one
    // (app BUG-71). Always paused and outside the queue: what starts after
    // a restart is the caller's decision.
    Result<TorrentId> restore_torrent(const TorrentId& id, lt::add_torrent_params params);
    Result<void> remove_torrent(const TorrentId& id, bool delete_files = false);

    lt::torrent_handle get_handle(const TorrentId& id) const;
    // The id a torrent was added under, carried in its userdata; nothing for
    // a handle whose torrent no longer exists or that add_torrent did not add.
    // Alert handlers use it: the torrent's hashes can change after the add
    // (a hybrid torrent added from a magnet with only its v1 hash gets the v2
    // hash with its metadata, app BUG-71).
    std::optional<TorrentId> id_of(const lt::torrent_handle& h) const;
    bool has_torrent(const TorrentId& id) const;
    // What each kind of peer of `id` gave and got since it was added (app
    // DEC-995, DEC-996, BUG-792); nothing with peer_share_interval 0, or for
    // a torrent no longer in the session.
    std::shared_ptr<PeerShareBook> peer_share(const TorrentId& id) const;
    std::vector<TorrentId> list_torrents() const;

    void set_proxy(const ProxyConfig& proxy);
    // How much the session shares while it downloads (app DEC-996), as a
    // partial pack: nothing else changes.
    void set_upload_gear(UploadGear gear);

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
    // The torrents' books of who gave and got, kept by the session's plugin.
    std::shared_ptr<PeerShareBooks> peer_shares_ = std::make_shared<PeerShareBooks>();
    std::unique_ptr<lt::session> session_;
    AlertDispatcher dispatcher_;
    std::unordered_map<TorrentId, lt::torrent_handle> handles_;
    mutable std::mutex mu_;
};

} // namespace seekserve
