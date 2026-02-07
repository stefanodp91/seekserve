#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <functional>
#include <unordered_map>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "seekserve/types.hpp"
#include "seekserve/error.hpp"
#include "seekserve/config.hpp"
#include "seekserve/byte_source.hpp"

namespace seekserve {

namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpRangeServer {
public:
    HttpRangeServer(net::io_context& ioc, const ServerConfig& config);
    ~HttpRangeServer();

    void set_byte_source(std::shared_ptr<ByteSource> source,
                         const TorrentId& torrent_id,
                         FileIndex file_index,
                         const std::string& file_path = "");

    Result<std::uint16_t> start(std::uint16_t port = 0);
    void stop();
    std::uint16_t port() const;
    std::string stream_url(const TorrentId& id, FileIndex fi) const;
    void set_auth_token(const std::string& token);

    // Callback invoked on each Range request (for scheduler notification).
    using RangeCallback = std::function<void(const ByteRange&, const TorrentId&, FileIndex)>;
    void set_range_callback(RangeCallback cb);

private:
    struct SourceEntry {
        std::shared_ptr<ByteSource> source;
        TorrentId torrent_id;
        FileIndex file_index;
        std::string file_path;  // for MIME type detection
    };

    void do_accept();
    void handle_connection(tcp::socket socket);

    net::io_context& ioc_;
    ServerConfig config_;
    std::unique_ptr<tcp::acceptor> acceptor_;

    mutable std::mutex sources_mu_;
    // Key: "torrentId/fileIndex"
    std::unordered_map<std::string, SourceEntry> sources_;

    std::string auth_token_;
    RangeCallback range_callback_;
    std::atomic<std::uint16_t> port_{0};
    std::atomic<bool> running_{false};
};

} // namespace seekserve
