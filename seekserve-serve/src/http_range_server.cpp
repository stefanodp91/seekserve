#include "seekserve/http_range_server.hpp"
#include "seekserve/auth_utils.hpp"
#include "seekserve/range_parser.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>

#include <spdlog/spdlog.h>

#include <cerrno>
#include <chrono>
#include <optional>
#include <regex>
#include <thread>
#include <sys/socket.h>

namespace seekserve {

namespace beast = boost::beast;
namespace http = beast::http;

static const std::int64_t kChunkSize = 65536; // 64 KB

// A response waiting for pieces checks this often whether its client is
// still there, and reports its position again every kReportEverySlices
// slices, so a scheduler moved elsewhere comes back to it.
static constexpr std::chrono::milliseconds kWaitSlice{500};
static constexpr int kReportEverySlices = 4;

// True once the client has closed the connection (or it failed). Anything
// the client sent after its request is read and dropped, since a connection
// serves one request, so a FIN queued behind it is still seen.
static bool peer_closed(tcp::socket& socket) {
    char scratch[512];
    for (int i = 0; i < 16; ++i) {
        auto n = ::recv(socket.native_handle(), scratch, sizeof scratch, MSG_DONTWAIT);
        if (n > 0) continue;
        if (n == 0) return true;
        if (errno == EINTR) continue;
        return errno != EAGAIN && errno != EWOULDBLOCK;
    }
    return false;  // still sending: check again at the next slice
}

static std::string mime_type(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    auto ext = path.substr(dot);
    if (ext == ".mp4")  return "video/mp4";
    if (ext == ".mkv")  return "video/x-matroska";
    if (ext == ".avi")  return "video/x-msvideo";
    if (ext == ".webm") return "video/webm";
    if (ext == ".ogv")  return "video/ogg";
    if (ext == ".mov")  return "video/quicktime";
    if (ext == ".mp3")  return "audio/mpeg";
    if (ext == ".ogg")  return "audio/ogg";
    if (ext == ".flac") return "audio/flac";
    return "application/octet-stream";
}

// Parse URL target: /stream/{torrentId}/{fileIndex}?token=XXX
struct ParsedTarget {
    std::string torrent_id;
    int file_index;
    std::string token;
};

static std::optional<ParsedTarget> parse_target(const std::string& target) {
    // Split off query string
    std::string path = target;
    std::string query;
    auto qpos = target.find('?');
    if (qpos != std::string::npos) {
        path = target.substr(0, qpos);
        query = target.substr(qpos + 1);
    }

    // Match /stream/{torrentId}/{fileIndex}
    static const std::regex re(R"(/stream/([a-fA-F0-9]+)/(\d+))");
    std::smatch m;
    if (!std::regex_match(path, m, re)) return std::nullopt;

    ParsedTarget pt;
    pt.torrent_id = m[1].str();
    try {
        pt.file_index = std::stoi(m[2].str());
    } catch (...) {
        return std::nullopt;
    }

    // Parse token from query
    auto tpos = query.find("token=");
    if (tpos != std::string::npos) {
        auto val_start = tpos + 6;
        auto amp = query.find('&', val_start);
        pt.token = query.substr(val_start, amp == std::string::npos ? amp : amp - val_start);
    }

    return pt;
}

// Strip token= from URL for safe logging
static std::string sanitize_url(std::string_view url) {
    auto pos = url.find("token=");
    if (pos == std::string_view::npos) return std::string(url);
    auto end = url.find('&', pos);
    return std::string(url.substr(0, pos)) + "token=***" +
           (end != std::string_view::npos ? std::string(url.substr(end)) : "");
}

HttpRangeServer::HttpRangeServer(net::io_context& ioc, const ServerConfig& config)
    : ioc_(ioc)
    , config_(config)
{
}

HttpRangeServer::~HttpRangeServer() {
    stop();
}

void HttpRangeServer::set_byte_source(std::shared_ptr<ByteSource> source,
                                      const TorrentId& torrent_id,
                                      FileIndex file_index,
                                      const std::string& file_path) {
    std::string key = torrent_id + "/" + std::to_string(file_index);
    std::lock_guard lock(sources_mu_);
    sources_[key] = SourceEntry{std::move(source), torrent_id, file_index, file_path};
}

void HttpRangeServer::remove_byte_source(const TorrentId& torrent_id, FileIndex file_index) {
    std::string key = torrent_id + "/" + std::to_string(file_index);
    std::shared_ptr<ByteSource> to_cancel;
    {
        std::lock_guard lock(sources_mu_);
        auto it = sources_.find(key);
        if (it != sources_.end()) {
            to_cancel = std::move(it->second.source);
            sources_.erase(it);
        }
    }
    if (to_cancel) to_cancel->cancel();
}

void HttpRangeServer::remove_byte_sources_for_torrent(const TorrentId& torrent_id) {
    std::vector<std::shared_ptr<ByteSource>> to_cancel;
    {
        std::lock_guard lock(sources_mu_);
        for (auto it = sources_.begin(); it != sources_.end(); ) {
            if (it->second.torrent_id == torrent_id) {
                to_cancel.push_back(std::move(it->second.source));
                it = sources_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& src : to_cancel) {
        if (src) src->cancel();
    }
}

Result<std::uint16_t> HttpRangeServer::start(std::uint16_t port) {
    if (running_.load()) {
        return make_error_code(errc::server_already_running);
    }

    boost::system::error_code ec;
    auto addr = net::ip::make_address(config_.bind_address, ec);
    if (ec) {
        spdlog::error("HttpRangeServer: invalid bind address '{}': {}", config_.bind_address, ec.message());
        return make_error_code(errc::invalid_argument);
    }

    auto endpoint = tcp::endpoint(addr, port);
    acceptor_ = std::make_unique<tcp::acceptor>(ioc_);

    acceptor_->open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("HttpRangeServer: open failed: {}", ec.message());
        return make_error_code(errc::io_error);
    }

    acceptor_->set_option(net::socket_base::reuse_address(true), ec);
    acceptor_->bind(endpoint, ec);
    if (ec) {
        spdlog::error("HttpRangeServer: bind failed: {}", ec.message());
        return make_error_code(errc::io_error);
    }

    acceptor_->listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("HttpRangeServer: listen failed: {}", ec.message());
        return make_error_code(errc::io_error);
    }

    auto actual_port = acceptor_->local_endpoint().port();
    port_.store(actual_port);
    running_.store(true);

    spdlog::info("HttpRangeServer: listening on {}:{}", config_.bind_address, actual_port);

    do_accept();
    return actual_port;
}

void HttpRangeServer::stop() {
    if (!running_.exchange(false)) return;

    if (acceptor_ && acceptor_->is_open()) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }

    // Cancel all byte sources and clear the map
    {
        std::lock_guard lock(sources_mu_);
        for (auto& [key, entry] : sources_) {
            if (entry.source) entry.source->cancel();
        }
        sources_.clear();
    }

    // Wake connection threads blocked on a client: in the request read, or
    // in a write to a client that stopped reading (a paused player). The
    // fds stay owned by their threads, which close them.
    {
        std::lock_guard lock(connections_->mu);
        for (int fd : connections_->fds) ::shutdown(fd, SHUT_RDWR);
    }

    // Once this returns no connection thread calls the callback again. The
    // threads themselves may still be ending: an owner that destroys what
    // they use (the io_context) waits for active_connections() to reach 0.
    {
        std::unique_lock lock(callback_mu_);
        range_callback_ = nullptr;
    }
    spdlog::info("HttpRangeServer: stopped");
}

std::uint16_t HttpRangeServer::port() const {
    return port_.load();
}

std::string HttpRangeServer::stream_url(const TorrentId& id, FileIndex fi) const {
    return "http://" + config_.bind_address + ":" + std::to_string(port_.load())
         + "/stream/" + id + "/" + std::to_string(fi)
         + "?token=" + auth_token_;
}

void HttpRangeServer::set_auth_token(const std::string& token) {
    auth_token_ = token;
}

void HttpRangeServer::set_range_callback(RangeCallback cb) {
    std::unique_lock lock(callback_mu_);
    range_callback_ = std::move(cb);
}

int HttpRangeServer::active_connections() const {
    return connections_->active.load();
}

void HttpRangeServer::notify_range(const ByteRange& range, const TorrentId& id, FileIndex fi) {
    std::shared_lock lock(callback_mu_);
    if (range_callback_) range_callback_(range, id, fi);
}

std::uint64_t HttpRangeServer::claim_and_report(const std::string& key, const ByteRange& range,
                                                 const TorrentId& id, FileIndex fi) {
    std::lock_guard lock(readers_mu_);
    auto reader = next_reader_++;
    position_owner_[key] = reader;
    notify_range(range, id, fi);
    return reader;
}

bool HttpRangeServer::report_if_owner(const std::string& key, std::uint64_t reader,
                                      const ByteRange& range, const TorrentId& id, FileIndex fi) {
    std::lock_guard lock(readers_mu_);
    // When the newest reader has finished, the next one still reading takes over.
    auto [it, inserted] = position_owner_.try_emplace(key, reader);
    if (!inserted && it->second != reader) return false;
    notify_range(range, id, fi);
    return true;
}

void HttpRangeServer::release_reader(const std::string& key, std::uint64_t reader) {
    std::lock_guard lock(readers_mu_);
    auto it = position_owner_.find(key);
    if (it != position_owner_.end() && it->second == reader) {
        position_owner_.erase(it);
    }
}

void HttpRangeServer::do_accept() {
    if (!running_.load()) return;

    auto self = shared_from_this();
    acceptor_->async_accept(
        [this, self](boost::system::error_code ec, tcp::socket socket) {
            if (ec) {
                if (ec != net::error::operation_aborted) {
                    spdlog::warn("HttpRangeServer: accept error: {}", ec.message());
                }
                return;
            }

            // Connection limit check
            if (connections_->active.load() >= config_.max_concurrent_streams) {
                spdlog::warn("HttpRangeServer: connection limit reached ({}/{}), rejecting",
                             connections_->active.load(), config_.max_concurrent_streams);
                boost::system::error_code close_ec;
                socket.close(close_ec);
            } else {
                // Counted here, on the accept thread, so a burst of
                // connections cannot pass the limit before their threads run.
                auto connections = connections_;
                connections->active.fetch_add(1);
                const int fd = socket.native_handle();
                {
                    std::lock_guard lock(connections->mu);
                    connections->fds.insert(fd);
                }

                // Set socket timeouts
                struct timeval tv_read;
                tv_read.tv_sec = config_.read_timeout.count();
                tv_read.tv_usec = 0;
                setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO,
                           &tv_read, sizeof(tv_read));

                struct timeval tv_send;
                tv_send.tv_sec = config_.connection_timeout.count();
                tv_send.tv_usec = 0;
                setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO,
                           &tv_send, sizeof(tv_send));

                // Handle each connection in its own thread so long-running
                // streams don't block the accept loop (important for VLC seeks).
                // Capture shared_ptr to keep HttpRangeServer alive for the
                // duration of the connection even if stop() is called.
                auto conn = std::make_unique<tcp::socket>(std::move(socket));
                try {
                    std::thread([self, connections, fd, conn = std::move(conn)]() mutable {
                        self->handle_connection(*conn);
                        {
                            // Out of the set before the fd can be reused.
                            std::lock_guard lock(connections->mu);
                            connections->fds.erase(fd);
                        }
                        // The socket and possibly the server go before the
                        // count drops: the owner may then destroy the
                        // io_context they belong to.
                        conn.reset();
                        self.reset();
                        connections->active.fetch_sub(1);
                    }).detach();
                } catch (const std::system_error& e) {
                    {
                        std::lock_guard lock(connections->mu);
                        connections->fds.erase(fd);
                    }
                    connections->active.fetch_sub(1);
                    spdlog::error("HttpRangeServer: cannot start a connection thread: {}", e.what());
                }
            }

            do_accept();
        });
}

void HttpRangeServer::handle_connection(tcp::socket& socket) {
    spdlog::debug("HttpRangeServer: new connection (active: {})", active_connections());
    beast::flat_buffer buffer;
    http::request<http::string_body> req;

    boost::system::error_code ec;
    http::read(socket, buffer, req, ec);
    if (ec) {
        spdlog::debug("HttpRangeServer: read error: {}", ec.message());
        return;
    }
    spdlog::debug("HttpRangeServer: {} {} range={}",
                  req.method_string(), sanitize_url(req.target()),
                  req.count(http::field::range) ? std::string(req[http::field::range]) : "(none)");

    auto send_small_response = [&](auto&& res) {
        res.set(http::field::server, "SeekServe/0.1");
        res.set(http::field::connection, req.keep_alive() ? "keep-alive" : "close");
        res.prepare_payload();
        boost::system::error_code write_ec;
        http::write(socket, res, write_ec);
    };

    auto send_head = [&](auto&& res, std::int64_t cl) {
        res.set(http::field::server, "SeekServe/0.1");
        res.set(http::field::connection, req.keep_alive() ? "keep-alive" : "close");
        res.content_length(cl);
        boost::system::error_code write_ec;
        http::write(socket, res, write_ec);
    };

    auto send_error = [&](http::status status, const std::string& body) {
        http::response<http::string_body> res{status, req.version()};
        res.set(http::field::content_type, "text/plain");
        res.body() = body;
        send_small_response(res);
    };

    // Streaming body write: sends headers via Beast, then body chunks via Asio.
    // This ensures VLC gets data immediately instead of waiting for full buffering.
    auto stream_body = [&](http::status status, const std::string& ct,
                           std::int64_t cl, const std::string& cr,
                           std::shared_ptr<ByteSource>& src,
                           std::int64_t start_offset, std::int64_t length,
                           const std::string& key, const TorrentId& torrent_id,
                           FileIndex file_index, std::uint64_t reader) {
        struct ReaderGuard {
            HttpRangeServer& server;
            const std::string& key;
            std::uint64_t reader;
            ~ReaderGuard() { server.release_reader(key, reader); }
        } reader_guard{*this, key, reader};

        // Send headers using Beast's empty_body serializer
        http::response<http::empty_body> header{status, req.version()};
        header.set(http::field::server, "SeekServe/0.1");
        header.set(http::field::connection, req.keep_alive() ? "keep-alive" : "close");
        header.set(http::field::content_type, ct);
        header.set(http::field::accept_ranges, "bytes");
        if (!cr.empty()) header.set(http::field::content_range, cr);
        header.content_length(static_cast<std::uint64_t>(cl));

        http::response_serializer<http::empty_body> sr{header};
        boost::system::error_code write_ec;
        http::write_header(socket, sr, write_ec);
        if (write_ec) {
            spdlog::debug("HttpRangeServer: header write error: {}", write_ec.message());
            return;
        }

        // Stream body data directly via Asio
        const std::int64_t last_offset = start_offset + length - 1;
        std::int64_t offset = start_offset;
        std::int64_t remaining = length;
        PieceIndex piece = src->piece_at(offset);
        while (remaining > 0) {
            // A chunk never crosses a piece boundary, so what is on disk goes
            // out before the wait for the next piece.
            auto chunk_len = std::min(kChunkSize, remaining);
            auto to_piece_end = src->bytes_to_piece_end(offset);
            if (to_piece_end > 0) chunk_len = std::min(chunk_len, to_piece_end);

            // Move the scheduler's window with the reader: a single request
            // can run to the end of the file, and the deadlines set when it
            // started only cover its first pieces.
            auto current_piece = src->piece_at(offset);
            if (current_piece >= 0 && current_piece != piece) {
                piece = current_piece;
                report_if_owner(key, reader, ByteRange{offset, last_offset}, torrent_id, file_index);
            }

            // The headers promised the whole range, and a body that ends
            // short reads as the end of the video to the player (app
            // BUG-83): wait for the pieces for as long as the client stays,
            // checking in between whether it is gone.
            Result<std::vector<std::uint8_t>> read_result =
                make_error_code(errc::timeout_waiting_for_piece);
            std::optional<std::chrono::steady_clock::time_point> waiting_since;
            int slices = 0;
            for (;;) {
                read_result = src->read(offset, chunk_len, kWaitSlice);
                if (read_result.ok() ||
                    read_result.error() != make_error_code(errc::timeout_waiting_for_piece)) {
                    break;
                }
                auto now = std::chrono::steady_clock::now();
                if (!waiting_since) {
                    waiting_since = now - kWaitSlice;
                    spdlog::debug("HttpRangeServer: waiting for pieces at offset {}", offset);
                }
                // The scheduler may have been moved by another request since
                // (a HEAD, a finished reader): the waiting owner re-anchors it.
                if (++slices % kReportEverySlices == 0) {
                    report_if_owner(key, reader, ByteRange{offset, last_offset},
                                    torrent_id, file_index);
                }
                if (!running_.load()) return;
                if (peer_closed(socket)) {
                    spdlog::debug("HttpRangeServer: client left while waiting at offset {}",
                                  offset);
                    return;
                }
                if (config_.stall_timeout.count() > 0 &&
                    now - *waiting_since >= config_.stall_timeout) {
                    spdlog::warn("HttpRangeServer: no data at offset {} for {} s, giving up",
                                 offset, config_.stall_timeout.count());
                    return;
                }
            }
            if (!read_result.ok()) {
                spdlog::error("HttpRangeServer: read error at offset {}: {}",
                              offset, read_result.error().message());
                return;
            }
            if (waiting_since) {
                spdlog::info("HttpRangeServer: data at offset {} after waiting {} ms", offset,
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - *waiting_since).count());
            }
            auto& data = read_result.value();
            auto bytes_read = static_cast<std::int64_t>(data.size());

            net::write(socket, net::buffer(data.data(), data.size()), write_ec);
            if (write_ec) {
                spdlog::debug("HttpRangeServer: body write error: {}", write_ec.message());
                return;
            }

            remaining -= bytes_read;
            offset += bytes_read;
            if (bytes_read < chunk_len) break;
        }
        spdlog::debug("HttpRangeServer: streamed {} bytes", length - remaining);
    };

    // Only GET and HEAD supported
    if (req.method() != http::verb::get && req.method() != http::verb::head) {
        send_error(http::status::method_not_allowed, "Method not allowed");
        return;
    }

    // Parse target URL
    auto parsed = parse_target(std::string(req.target()));
    if (!parsed) {
        send_error(http::status::not_found, "Not found");
        return;
    }

    // Auth check: accepts Authorization: Bearer <token> or ?token= query param
    if (!auth_token_.empty()) {
        auto token = extract_token(req);
        if (!constant_time_compare(token, auth_token_)) {
            send_error(http::status::forbidden, "Forbidden");
            return;
        }
    }

    // Find byte source
    std::string key = parsed->torrent_id + "/" + std::to_string(parsed->file_index);
    std::shared_ptr<ByteSource> source;
    std::string content_type;
    {
        std::lock_guard lock(sources_mu_);
        auto it = sources_.find(key);
        if (it == sources_.end()) {
            send_error(http::status::not_found, "Stream not found");
            return;
        }
        source = it->second.source;
        content_type = it->second.file_path.empty()
            ? "application/octet-stream"
            : mime_type(it->second.file_path);
    }

    auto file_size = source->file_size();

    // Check for Range header
    auto range_it = req.find(http::field::range);

    // Only a GET reads the file, so only a GET moves the scheduler, and it
    // becomes the stream's newest reader before its first report.
    if (range_it == req.end()) {
        // No Range header → 200 OK with Accept-Ranges
        if (req.method() == http::verb::head) {
            http::response<http::empty_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, content_type);
            res.set(http::field::accept_ranges, "bytes");
            send_head(res, file_size);
        } else {
            auto reader = claim_and_report(key, ByteRange{0, file_size - 1},
                                           parsed->torrent_id, parsed->file_index);
            stream_body(http::status::ok, content_type, file_size, "", source, 0, file_size,
                        key, parsed->torrent_id, parsed->file_index, reader);
        }
        return;
    }

    // Parse Range header
    auto range = parse_range_header(std::string(range_it->value()), file_size);
    if (!range) {
        http::response<http::string_body> res{http::status::range_not_satisfiable, req.version()};
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::content_range, "bytes */" + std::to_string(file_size));
        res.body() = "Range Not Satisfiable";
        send_small_response(res);
        return;
    }

    auto content_length = range->end - range->start + 1;
    auto content_range = "bytes " + std::to_string(range->start) + "-"
                       + std::to_string(range->end) + "/"
                       + std::to_string(file_size);

    if (req.method() == http::verb::head) {
        http::response<http::empty_body> res{http::status::partial_content, req.version()};
        res.set(http::field::content_type, content_type);
        res.set(http::field::accept_ranges, "bytes");
        res.set(http::field::content_range, content_range);
        send_head(res, content_length);
        return;
    }

    // 206 Partial Content — stream body chunks
    auto reader = claim_and_report(key, ByteRange{range->start, range->end},
                                   parsed->torrent_id, parsed->file_index);
    stream_body(http::status::partial_content, content_type, content_length,
                content_range, source, range->start, content_length,
                key, parsed->torrent_id, parsed->file_index, reader);
}

} // namespace seekserve
