#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <atomic>
#include <functional>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include "seekserve/types.hpp"
#include "seekserve/error.hpp"
#include "seekserve/config.hpp"
#include "seekserve/session_manager.hpp"
#include "seekserve/metadata_catalog.hpp"
#include "seekserve/piece_availability.hpp"
#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/wait_piece.hpp"
#include "seekserve/startup_focus.hpp"
#include "seekserve/wait_focus.hpp"
#include "seekserve/piece_reader.hpp"
#include "seekserve/byte_source.hpp"
#include "seekserve/streaming_scheduler.hpp"
#include "seekserve/offline_cache.hpp"
#include "seekserve/http_range_server.hpp"
#include "seekserve/control_api_server.hpp"

namespace seekserve {

/// High-level facade that owns and wires all SeekServe modules.
/// Designed as the single entry point for the C API / Flutter FFI.
class SeekServeEngine {
public:
    struct Config {
        SessionConfig session;
        ServerConfig server;
        CacheConfig cache;
        SchedulerConfig scheduler;
        std::string auth_token;
    };

    explicit SeekServeEngine(const Config& config);
    ~SeekServeEngine();

    SeekServeEngine(const SeekServeEngine&) = delete;
    SeekServeEngine& operator=(const SeekServeEngine&) = delete;

    // Torrent management
    Result<TorrentId> add_torrent(const std::string& uri,
                                  const std::string& save_path = "");
    Result<void> remove_torrent(const TorrentId& id, bool delete_files);
    std::vector<TorrentId> list_torrents() const;

    // Pause / resume
    Result<void> pause_torrent(const TorrentId& id);
    // Resumes into libtorrent's download queue (max_active_downloads).
    Result<void> resume_torrent(const TorrentId& id);
    // Resumes outside the queue, as add_torrent does: for streaming, which
    // must not wait for (or push out) a queued download.
    Result<void> start_torrent(const TorrentId& id);
    Result<void> force_reannounce(const TorrentId& id);
    // Checks the pieces on disk again (app BUG-202): libtorrent keeps the
    // pieces it had in memory, so a file deleted while the torrent is here
    // stays whole for it until it reads the files again. The torrent leaves
    // its peers while it checks, then downloads what is missing.
    Result<void> recheck_torrent(const TorrentId& id);

    // File management
    Result<std::vector<FileInfo>> list_files(const TorrentId& id);
    Result<void> select_file(const TorrentId& id, FileIndex fi);
    // The two files a torrent may want at once (app DEC-42): `reader` is
    // the one a player reads, with the stream and the deadlines, and
    // `background` downloads behind it; -1 for none, and every other file
    // is not downloaded. The reader keeps its stream while only the file
    // behind it changes. select_file(id, fi) is a new reader alone.
    Result<void> set_wanted_files(const TorrentId& id, FileIndex reader, FileIndex background);

    // Streaming
    Result<std::string> get_stream_url(const TorrentId& id, FileIndex fi);
    std::string get_status_json(const TorrentId& id);

    // Server lifecycle
    Result<std::uint16_t> start_server(std::uint16_t port = 0);
    // Also closes every share.
    void stop_server();

    // Shares (app WORK-37): the file a player reads, served to one other
    // device, a TV, by a server of its own on `bind_address` (the device's
    // address on that network, never a wildcard) that serves only
    // `peer_address`. Its URL has a random path and token of its own, never
    // the torrent's id, which is its info hash. The share follows its file
    // when the file is selected again; it stays until close_share or
    // stop_server, and answers 404 once the file is no longer the reader's.
    struct Share {
        std::string id;   // for close_share
        std::string url;  // for the device
    };
    Result<Share> open_share(const TorrentId& id, FileIndex fi,
                             const std::string& bind_address,
                             const std::string& peer_address);
    // A share of a whole file on disk, no torrent's (app WORK-37, DEC-627):
    // a download already complete, served the same way, with no torrent and
    // no peer, so also while the session cannot reach the network (the
    // app's DoH mode). Only a regular file inside the save path, links
    // resolved: anything else is errc::invalid_argument, a path with nothing
    // there errc::file_not_found. It lasts until close_share or stop_server.
    Result<Share> open_file_share(const std::string& file_path,
                                  const std::string& bind_address,
                                  const std::string& peer_address);
    Result<void> close_share(const std::string& share_id);

    // Proxy configuration (runtime update via apply_settings)
    void set_proxy(const ProxyConfig& proxy);

    // Event callbacks (fired from alert thread or timer thread)
    using EventCallback = std::function<void(const std::string& event_json)>;
    void set_event_callback(EventCallback cb);

private:
    // What the file a player reads needs: where its bytes are, the stream
    // that serves them and the scheduler that asks for them in time. One
    // for each torrent with a reader; a file that only downloads has none.
    struct TorrentState {
        PieceAvailabilityIndex avail;
        std::unique_ptr<ByteRangeMapper> mapper;
        std::shared_ptr<ByteSource> source;
        // Reads the pieces that arrive during the stream through libtorrent
        // (app BUG-236, DEC-657).
        std::shared_ptr<TorrentPieceReader> pieces;
        std::unique_ptr<StreamingScheduler> scheduler;
        // The start of the stream with priority (app BUG-228, DEC-656).
        std::unique_ptr<StartupFocus> focus;
        // A reader that waits for the same piece too long gets the start
        // with priority again, from that piece (app BUG-246, DEC-669).
        std::unique_ptr<WaitFocusWatch> wait_focus;
        // Since when the piece a reader waits for got no block (DEC-661).
        WaitPieceWatch wait_watch;
        FileIndex selected_file{-1};  // the reader's file
    };

    // The files `id` wants from now on. With `new_reader` the reader gets a
    // new state and a new stream even if it is the file a player has now,
    // which is what select_file has always done.
    Result<void> want_files(const TorrentId& id, FileIndex reader,
                            FileIndex background, bool new_reader);

    // A range request or a reader's progress on a stream of file `fi` of
    // `id`, from the engine's server or from a share.
    void on_stream_range(const ByteRange& range, const TorrentId& id, FileIndex fi);
    // A reader that starts or jumps far at byte `offset`: its first bytes
    // come first (app BUG-228, DEC-656). Called with mu_ held.
    void begin_startup_focus(TorrentState& state, lt::torrent_handle& handle,
                             std::int64_t offset);
    // The first piece missing from the reader's position on, while a reader
    // of `state` waits for a download; -1 otherwise. Called with mu_ held.
    static PieceIndex awaited_piece(const TorrentState& state);
    // Who has `piece` and where its blocks are (app BUG-238, BUG-228,
    // DEC-661); nothing when the torrent went. Called without mu_: it asks
    // libtorrent.
    std::optional<WaitPieceStats> awaited_piece_stats(const TorrentId& id,
                                                      const lt::torrent_handle& handle,
                                                      PieceIndex piece);
    // The start with priority of `id` is over: the files' priorities again.
    // Called with mu_ held.
    void end_startup_focus(const TorrentId& id, TorrentState& state,
                           lt::torrent_handle& handle);
    // Stops a stream server and waits, bounded, for its connection threads,
    // which own sockets of ioc_.
    static void stop_and_drain(HttpRangeServer& server);
    // The shares of file `fi` of `id` (any file with fi = -1) get `source`,
    // or lose theirs with nullptr.
    // A share's own server, for one client, serving `source`; started and
    // listed. Called with shares_mu_ held and the servers running.
    Result<Share> start_share(std::shared_ptr<StreamSource> source,
                              const std::string& file_path,
                              const std::string& bind_address,
                              const std::string& peer_address,
                              const TorrentId& torrent, FileIndex fi);
    void update_shares(const TorrentId& id, FileIndex fi,
                       const std::shared_ptr<ByteSource>& source,
                       const std::string& file_path = "");

    void wire_alerts();
    void start_tick_timer();
    void on_tick(const boost::system::error_code& ec);
    TorrentState* find_state(const TorrentId& id);
    void fire_event(const std::string& type, const std::string& data);
    // Files of a torrent whose metadata arrived: catalog, cache, event.
    void register_metadata(const TorrentId& id,
                           std::shared_ptr<const lt::torrent_info> ti);

    // Resume data (SessionConfig::resume_data): asks libtorrent for what a
    // torrent needs to start again without its peers; the answer comes as
    // an alert and goes into the cache database. A no-op when switched off.
    // By handle where an alert has it: the session manager may not know
    // the id yet while the torrent is still being added.
    void request_resume_data(const lt::torrent_handle& handle, bool flush_to_disk = false);
    void request_resume_data(const TorrentId& id, bool flush_to_disk = false);
    void request_all_resume_data(bool flush_to_disk = false);
    // One request was answered, with data or with nothing to write.
    void resume_data_answered();
    // Asks for every torrent and waits for the answers, up to
    // SessionConfig::resume_flush_timeout.
    void flush_resume_data();

    Config config_;
    net::io_context ioc_;
    using work_guard_t = net::executor_work_guard<net::io_context::executor_type>;
    std::unique_ptr<work_guard_t> work_guard_;
    std::thread io_thread_;

    std::unique_ptr<TorrentSessionManager> sessions_;
    MetadataCatalog catalog_;
    std::unique_ptr<OfflineCacheManager> cache_;
    std::shared_ptr<HttpRangeServer> http_server_;
    std::unique_ptr<ControlApiServer> api_server_;
    std::unique_ptr<net::steady_timer> tick_timer_;

    std::unordered_map<TorrentId, std::unique_ptr<TorrentState>> states_;
    // Guards alert handlers against late alerts of a removed torrent; an id
    // leaves it when the torrent is added again (app BUG-68).
    std::unordered_set<TorrentId> removed_ids_;
    mutable std::mutex mu_;

    EventCallback event_cb_;
    mutable std::mutex event_mu_;

    std::atomic<bool> server_running_{false};
    // libtorrent's alerts_dropped_alert: alerts lost to a full queue, a read
    // answer among them maybe (app BUG-238, DEC-660).
    std::atomic<std::int64_t> alerts_dropped_{0};

    struct ShareState {
        std::shared_ptr<HttpRangeServer> server;
        TorrentId torrent;  // empty for a file share
        FileIndex file;     // -1 for a file share
    };
    // By share id, which is also the share's stream id in its URL.
    std::unordered_map<std::string, ShareState> shares_;
    std::mutex shares_mu_;

    // Resume data requests libtorrent has not answered yet.
    int resume_pending_ = 0;
    std::mutex resume_mu_;
    std::condition_variable resume_cv_;
    // Timer ticks (seconds) since the last periodic save.
    std::int64_t ticks_since_resume_save_ = 0;
};

} // namespace seekserve
