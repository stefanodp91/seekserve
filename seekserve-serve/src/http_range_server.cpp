#include "seekserve/http_range_server.hpp"
#include "seekserve/range_parser.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>

#include <spdlog/spdlog.h>

#include <regex>
#include <thread>

namespace seekserve {

namespace beast = boost::beast;
namespace http = beast::http;

static const std::int64_t kChunkSize = 65536; // 64 KB

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

static bool constant_time_compare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile int result = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        result |= (a[i] ^ b[i]);
    }
    return result == 0;
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

    // Cancel all byte sources
    std::lock_guard lock(sources_mu_);
    for (auto& [key, entry] : sources_) {
        if (entry.source) entry.source->cancel();
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
    range_callback_ = std::move(cb);
}

void HttpRangeServer::do_accept() {
    if (!running_.load()) return;

    acceptor_->async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (ec) {
                if (ec != net::error::operation_aborted) {
                    spdlog::warn("HttpRangeServer: accept error: {}", ec.message());
                }
                return;
            }

            // Handle each connection in its own thread so long-running
            // streams don't block the accept loop (important for VLC seeks).
            std::thread([this, s = std::move(socket)]() mutable {
                handle_connection(std::move(s));
            }).detach();

            do_accept();
        });
}

void HttpRangeServer::handle_connection(tcp::socket socket) {
    spdlog::debug("HttpRangeServer: new connection");
    beast::flat_buffer buffer;
    http::request<http::string_body> req;

    boost::system::error_code ec;
    http::read(socket, buffer, req, ec);
    if (ec) {
        spdlog::debug("HttpRangeServer: read error: {}", ec.message());
        return;
    }
    spdlog::debug("HttpRangeServer: {} {} range={}",
                  req.method_string(), req.target(),
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
                           std::int64_t start_offset, std::int64_t length) {
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
        std::int64_t offset = start_offset;
        std::int64_t remaining = length;
        while (remaining > 0) {
            auto chunk_len = std::min(kChunkSize, remaining);
            auto read_result = src->read(offset, chunk_len);
            if (!read_result.ok()) {
                spdlog::error("HttpRangeServer: read error at offset {}: {}",
                              offset, read_result.error().message());
                return;
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

    // Auth check
    if (!auth_token_.empty()) {
        if (!constant_time_compare(parsed->token, auth_token_)) {
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

    if (range_it == req.end()) {
        // No Range header → 200 OK with Accept-Ranges
        // Notify scheduler: full-file request
        if (range_callback_) {
            range_callback_(ByteRange{0, file_size - 1}, parsed->torrent_id, parsed->file_index);
        }
        if (req.method() == http::verb::head) {
            http::response<http::empty_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, content_type);
            res.set(http::field::accept_ranges, "bytes");
            send_head(res, file_size);
        } else {
            stream_body(http::status::ok, content_type, file_size, "", source, 0, file_size);
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

    // Notify scheduler: partial range request
    if (range_callback_) {
        range_callback_(ByteRange{range->start, range->end}, parsed->torrent_id, parsed->file_index);
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
    stream_body(http::status::partial_content, content_type, content_length,
                content_range, source, range->start, content_length);
}

} // namespace seekserve
