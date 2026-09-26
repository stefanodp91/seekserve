#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <functional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "seekserve/types.hpp"
#include "seekserve/error.hpp"
#include "seekserve/config.hpp"
#include "seekserve/byte_source.hpp"

namespace seekserve {

namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpRangeServer : public std::enable_shared_from_this<HttpRangeServer> {
public:
    HttpRangeServer(net::io_context& ioc, const ServerConfig& config);
    ~HttpRangeServer();

    void set_byte_source(std::shared_ptr<ByteSource> source,
                         const TorrentId& torrent_id,
                         FileIndex file_index,
                         const std::string& file_path = "");
    void remove_byte_source(const TorrentId& torrent_id, FileIndex file_index);
    void remove_byte_sources_for_torrent(const TorrentId& torrent_id);

    Result<std::uint16_t> start(std::uint16_t port = 0);
    void stop();
    std::uint16_t port() const;
    std::string stream_url(const TorrentId& id, FileIndex fi) const;
    void set_auth_token(const std::string& token);

    // Scheduler notification, with the range still to send: invoked when a
    // GET starts, whenever the newest reader of a stream moves into another
    // piece, and every couple of seconds while it waits for a piece. Set it
    // before start(); stop() clears it and waits for calls in progress.
    using RangeCallback = std::function<void(const ByteRange&, const TorrentId&, FileIndex)>;
    void set_range_callback(RangeCallback cb);

    // Connections whose thread has not finished yet. After stop() they end
    // promptly: their sources are cancelled and their sockets shut down.
    int active_connections() const;

private:
    struct SourceEntry {
        std::shared_ptr<ByteSource> source;
        TorrentId torrent_id;
        FileIndex file_index;
        std::string file_path;  // for MIME type detection
    };

    void do_accept();
    void handle_connection(tcp::socket& socket);
    void notify_range(const ByteRange& range, const TorrentId& id, FileIndex fi);

    // The newest body response of a stream owns its position: only its
    // reader moves the scheduler, so a stale connection cannot pull it back.
    // Claiming and reporting happen under readers_mu_, so a report from the
    // previous owner cannot land after the new owner's first one.
    std::uint64_t claim_and_report(const std::string& key, const ByteRange& range,
                                   const TorrentId& id, FileIndex fi);
    bool report_if_owner(const std::string& key, std::uint64_t reader,
                         const ByteRange& range, const TorrentId& id, FileIndex fi);
    void release_reader(const std::string& key, std::uint64_t reader);

    // Shared with the connection threads, which can outlive the server.
    struct Connections {
        std::atomic<int> active{0};
        std::mutex mu;
        std::unordered_set<int> fds;  // sockets still open, for stop()
    };

    net::io_context& ioc_;
    ServerConfig config_;
    std::unique_ptr<tcp::acceptor> acceptor_;

    mutable std::mutex sources_mu_;
    // Key: "torrentId/fileIndex"
    std::unordered_map<std::string, SourceEntry> sources_;

    std::string auth_token_;
    std::shared_mutex callback_mu_;
    RangeCallback range_callback_;

    // Lock order: readers_mu_, then callback_mu_, then whatever the
    // callback takes.
    std::mutex readers_mu_;
    std::unordered_map<std::string, std::uint64_t> position_owner_;
    std::uint64_t next_reader_{1};

    std::shared_ptr<Connections> connections_ = std::make_shared<Connections>();

    std::atomic<std::uint16_t> port_{0};
    std::atomic<bool> running_{false};
};

} // namespace seekserve
