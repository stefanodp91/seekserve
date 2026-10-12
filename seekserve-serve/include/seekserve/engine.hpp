#pragma once

#include <memory>
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
    void stop_server();

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
        std::unique_ptr<StreamingScheduler> scheduler;
        FileIndex selected_file{-1};  // the reader's file
    };

    // The files `id` wants from now on. With `new_reader` the reader gets a
    // new state and a new stream even if it is the file a player has now,
    // which is what select_file has always done.
    Result<void> want_files(const TorrentId& id, FileIndex reader,
                            FileIndex background, bool new_reader);

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

    // Resume data requests libtorrent has not answered yet.
    int resume_pending_ = 0;
    std::mutex resume_mu_;
    std::condition_variable resume_cv_;
    // Timer ticks (seconds) since the last periodic save.
    std::int64_t ticks_since_resume_save_ = 0;
};

} // namespace seekserve
