#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace seekserve {

struct ProxyConfig {
    bool enabled = false;
    std::string hostname = "127.0.0.1";
    int port = 9050;
};

struct SessionConfig {
    std::string save_path = "./downloads";
    bool enable_webtorrent = true;
    std::vector<std::string> extra_trackers;
    int listen_port_start = 6881;
    int listen_port_end = 6891;
    int alert_queue_size = 5000;
    // How many queued (auto-managed) torrents download at once; <= 0 keeps
    // libtorrent's default (3). Torrents start outside the queue when added
    // and join it on resume_torrent.
    int max_active_downloads = 0;
    // PEM file with the CAs that HTTPS trackers and web seeds are checked
    // against, in addition to OpenSSL's default locations. Empty = defaults
    // only. Needed on Android, where the defaults do not exist and OpenSSL
    // ignores SSL_CERT_FILE in app processes.
    std::string ca_cert_file;
    // libtorrent's close_redundant_connections: when a torrent has every
    // wanted piece, libtorrent disconnects the seeds. Off = the connections
    // stay, so the next file of a pack starts at once (app BUG-198).
    bool close_redundant_connections = true;
    // libtorrent's min_reconnect_time: how long a peer that was let go waits
    // before it is taken back (multiplied by its failure count).
    std::chrono::seconds min_reconnect_time{60};
    // Keeps each torrent's resume data (its metadata, the pieces it has, its
    // file priorities) in the cache database, without peer addresses, and
    // restores torrents from it: a restart then needs neither the metadata
    // from peers nor a full re-check, and every restored torrent comes back
    // paused. Off = torrents are restored from their saved URI only, and
    // start at once, as before.
    bool resume_data = false;
    // How often the resume data of a torrent that changed is saved again;
    // 0 = only on events (metadata, a completed file, a pause, shutdown).
    std::chrono::seconds resume_save_interval{0};
    // How long shutdown waits for the last resume data to be written.
    std::chrono::milliseconds resume_flush_timeout{3000};
    ProxyConfig proxy;
};

struct SchedulerConfig {
    int hot_window_pieces = 5;
    int lookahead_pieces = 20;
    int seek_boost_pieces = 10;
    int deadline_budget = 30;
    int seek_boost_duration_ms = 3000;

    std::int64_t min_contiguous_bytes = 2 * 1024 * 1024;  // 2 MB
    int stall_count_threshold = 3;
    double min_sustained_rate = 500'000;  // 500 KB/s

    // The start of a stream with priority (app BUG-228, DEC-656): when a
    // reader starts or jumps far, the pieces of its first
    // `startup_focus_bytes` come first and every other wanted piece last,
    // until those bytes are there or `startup_focus_timeout` passed. 0 = off,
    // as before.
    std::int64_t startup_focus_bytes = 0;
    std::chrono::milliseconds startup_focus_timeout{30000};
    // A reader that waits for the same piece and gets no block of it for this
    // long gets that piece asked first, again after each such while, as long
    // as a connected peer that has it sends us blocks (app BUG-246, DEC-670,
    // BUG-247; WaitFocusWatch). 0 = never, as before.
    std::chrono::milliseconds wait_focus_after{0};
};

struct ServerConfig {
    std::string bind_address = "127.0.0.1";
    // The only client address served; empty = any. A connection from any
    // other address is closed at once, without an answer (app WORK-37).
    std::string allowed_peer;
    std::uint16_t stream_port = 0;   // 0 = auto
    std::uint16_t control_port = 0;  // 0 = auto
    int max_concurrent_streams = 4;
    // How long a client may take to send its whole request after it
    // connected (app BUG-88).
    std::chrono::seconds read_timeout{30};
    // How long a write to the newest reader of a stream may wait for the
    // client to read. 0 = as long as it stays connected: a paused player
    // keeps its connection (app BUG-88).
    std::chrono::seconds connection_timeout{0};
    // How long a reader that a newer request of the same stream left behind
    // may wait, for its client or for pieces, before it is closed. 0 = as
    // long as it stays connected. A TV that seeks leaves its old connection
    // open without reading it (app BUG-88, WORK-37).
    std::chrono::seconds stale_reader_timeout{0};
    // How long a stream response may wait for missing pieces before giving
    // up. 0 = as long as the client stays connected: the response then never
    // ends short while the player still waits for a seek target (app BUG-83).
    std::chrono::seconds stall_timeout{0};
    // How many bytes of pieces read through libtorrent a stream keeps for its
    // next chunks; always at least one piece (app BUG-236, DEC-657).
    std::int64_t piece_cache_bytes = 16 * 1024 * 1024;
};

struct CacheConfig {
    std::string db_path = "seekserve_cache.db";
    std::int64_t max_storage_bytes = 0;  // 0 = unlimited
    std::chrono::hours ttl{24 * 30};     // 30 days
    bool offline_download_enabled = true;
};

} // namespace seekserve
