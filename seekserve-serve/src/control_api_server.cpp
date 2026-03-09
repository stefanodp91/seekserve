#include "seekserve/control_api_server.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/write.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <regex>
#include <thread>
#include <sys/socket.h>

#include <libtorrent/torrent_status.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/hex.hpp>

namespace seekserve {

namespace beast = boost::beast;
namespace http = beast::http;
using json = nlohmann::json;

static std::string sanitize_url(std::string_view url) {
    auto pos = url.find("token=");
    if (pos == std::string_view::npos) return std::string(url);
    auto end = url.find('&', pos);
    return std::string(url.substr(0, pos)) + "token=***" +
           (end != std::string_view::npos ? std::string(url.substr(end)) : "");
}

static bool constant_time_compare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile int result = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        result |= (a[i] ^ b[i]);
    }
    return result == 0;
}

// Extract Bearer token from Authorization header or ?token= query param
static std::string extract_token(const http::request<http::string_body>& req) {
    // Check Authorization header first
    auto auth_it = req.find(http::field::authorization);
    if (auth_it != req.end()) {
        auto val = std::string(auth_it->value());
        if (val.size() > 7 && val.substr(0, 7) == "Bearer ") {
            return val.substr(7);
        }
    }

    // Check query param ?token=
    auto target = std::string(req.target());
    auto qpos = target.find("token=");
    if (qpos != std::string::npos) {
        auto val_start = qpos + 6;
        auto amp = target.find('&', val_start);
        return target.substr(val_start, amp == std::string::npos ? amp : amp - val_start);
    }

    return {};
}

// Extract path without query string
static std::string extract_path(const std::string& target) {
    auto qpos = target.find('?');
    return qpos == std::string::npos ? target : target.substr(0, qpos);
}

ControlApiServer::ControlApiServer(net::io_context& ioc,
                                   TorrentSessionManager& sessions,
                                   MetadataCatalog& catalog,
                                   HttpRangeServer& range_server,
                                   OfflineCacheManager& cache,
                                   const std::string& auth_token)
    : ioc_(ioc)
    , sessions_(sessions)
    , catalog_(catalog)
    , range_server_(range_server)
    , cache_(cache)
    , auth_token_(auth_token)
{
}

ControlApiServer::~ControlApiServer() {
    stop();
}

Result<std::uint16_t> ControlApiServer::start(std::uint16_t port) {
    if (running_.load()) {
        return make_error_code(errc::server_already_running);
    }

    boost::system::error_code ec;
    auto addr = net::ip::make_address("127.0.0.1", ec);
    if (ec) {
        spdlog::error("ControlApiServer: invalid bind address: {}", ec.message());
        return make_error_code(errc::invalid_argument);
    }

    auto endpoint = tcp::endpoint(addr, port);
    acceptor_ = std::make_unique<tcp::acceptor>(ioc_);

    acceptor_->open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("ControlApiServer: open failed: {}", ec.message());
        return make_error_code(errc::io_error);
    }

    acceptor_->set_option(net::socket_base::reuse_address(true), ec);
    acceptor_->bind(endpoint, ec);
    if (ec) {
        spdlog::error("ControlApiServer: bind failed: {}", ec.message());
        return make_error_code(errc::io_error);
    }

    acceptor_->listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("ControlApiServer: listen failed: {}", ec.message());
        return make_error_code(errc::io_error);
    }

    auto actual_port = acceptor_->local_endpoint().port();
    port_.store(actual_port);
    running_.store(true);

    spdlog::info("ControlApiServer: listening on 127.0.0.1:{}", actual_port);

    do_accept();
    return actual_port;
}

void ControlApiServer::stop() {
    if (!running_.exchange(false)) return;

    if (acceptor_ && acceptor_->is_open()) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }
    spdlog::info("ControlApiServer: stopped");
}

std::uint16_t ControlApiServer::port() const {
    return port_.load();
}

void ControlApiServer::set_shutdown_callback(ShutdownCallback cb) {
    shutdown_cb_ = std::move(cb);
}

void ControlApiServer::do_accept() {
    if (!running_.load()) return;

    acceptor_->async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (ec) {
                if (ec != net::error::operation_aborted) {
                    spdlog::warn("ControlApiServer: accept error: {}", ec.message());
                }
                return;
            }

            // Connection limit (reuse max_concurrent_streams as general limit)
            if (active_connections_.load() >= 20) {
                spdlog::warn("ControlApiServer: connection limit reached, rejecting");
                boost::system::error_code close_ec;
                socket.close(close_ec);
            } else {
                struct timeval tv;
                tv.tv_sec = 30;
                tv.tv_usec = 0;
                setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

                std::thread([this, s = std::move(socket)]() mutable {
                    handle_connection(std::move(s));
                }).detach();
            }

            do_accept();
        });
}

void ControlApiServer::handle_connection(tcp::socket socket) {
    active_connections_.fetch_add(1);
    struct ConnGuard {
        std::atomic<int>& count;
        ~ConnGuard() { count.fetch_sub(1); }
    } guard{active_connections_};

    beast::flat_buffer buffer;
    http::request<http::string_body> req;

    boost::system::error_code ec;
    http::read(socket, buffer, req, ec);
    if (ec) {
        spdlog::debug("ControlApiServer: read error: {}", ec.message());
        return;
    }

    spdlog::debug("ControlApiServer: {} {}", req.method_string(), sanitize_url(req.target()));

    // Reject oversized request bodies (> 1MB)
    static constexpr std::size_t kMaxBodySize = 1024 * 1024;
    if (req.body().size() > kMaxBodySize) {
        http::response<http::string_body> res{http::status::payload_too_large, req.version()};
        res.set(http::field::server, "SeekServe/0.1");
        res.set(http::field::content_type, "application/json");
        res.set(http::field::connection, "close");
        res.body() = R"({"error":"Request body too large"})";
        res.prepare_payload();
        boost::system::error_code write_ec;
        http::write(socket, res, write_ec);
        return;
    }

    auto send_json = [&](http::status status, const json& body) {
        http::response<http::string_body> res{status, req.version()};
        res.set(http::field::server, "SeekServe/0.1");
        res.set(http::field::content_type, "application/json");
        res.set(http::field::connection, "close");
        res.set(http::field::access_control_allow_origin, "*");
        res.body() = body.dump();
        res.prepare_payload();
        boost::system::error_code write_ec;
        http::write(socket, res, write_ec);
    };

    auto send_error = [&](http::status status, const std::string& message) {
        send_json(status, json{{"error", message}});
    };

    // CORS preflight
    if (req.method() == http::verb::options) {
        http::response<http::empty_body> res{http::status::no_content, req.version()};
        res.set(http::field::server, "SeekServe/0.1");
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, POST, DELETE, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Authorization, Content-Type");
        res.set(http::field::connection, "close");
        res.prepare_payload();
        boost::system::error_code write_ec;
        http::write(socket, res, write_ec);
        return;
    }

    // Auth check
    if (!auth_token_.empty()) {
        auto token = extract_token(req);
        if (!constant_time_compare(token, auth_token_)) {
            send_error(http::status::forbidden, "Invalid or missing auth token");
            return;
        }
    }

    auto path = extract_path(std::string(req.target()));
    auto method = req.method();

    // POST /api/torrents — add torrent
    if (method == http::verb::post && path == "/api/torrents") {
        json body_json;
        try {
            body_json = json::parse(req.body());
        } catch (...) {
            send_error(http::status::bad_request, "Invalid JSON body");
            return;
        }

        auto uri = body_json.value("uri", "");
        if (uri.empty()) {
            send_error(http::status::bad_request, "Missing 'uri' field");
            return;
        }

        AddTorrentParams atp;
        atp.uri = uri;
        atp.save_path = body_json.value("save_path", "");

        auto result = sessions_.add_torrent(atp);
        if (!result) {
            send_error(http::status::bad_request, result.error().message());
            return;
        }

        send_json(http::status::created, json{
            {"torrent_id", result.value()}
        });
        return;
    }

    // GET /api/torrents — list torrents
    if (method == http::verb::get && path == "/api/torrents") {
        auto ids = sessions_.list_torrents();
        json arr = json::array();
        for (const auto& id : ids) {
            json entry;
            entry["torrent_id"] = id;
            entry["has_metadata"] = catalog_.has_metadata(id);

            auto handle = sessions_.get_handle(id);
            if (handle.is_valid()) {
                auto st = handle.status(lt::torrent_handle::query_name);
                entry["name"] = st.name;
                entry["progress"] = st.progress;
                entry["download_rate"] = st.download_rate;
                entry["num_peers"] = st.num_peers;
            }

            arr.push_back(std::move(entry));
        }
        send_json(http::status::ok, json{{"torrents", arr}});
        return;
    }

    // Match /api/torrents/{id}/*
    static const std::regex re_torrent(R"(/api/torrents/([a-fA-F0-9]+)(/.*)?)");
    std::smatch m;
    if (!std::regex_match(path, m, re_torrent)) {
        // POST /api/server/stop
        if (method == http::verb::post && path == "/api/server/stop") {
            send_json(http::status::ok, json{{"status", "shutting_down"}});
            if (shutdown_cb_) {
                shutdown_cb_();
            }
            return;
        }

        // GET /api/cache — list cached files
        if (method == http::verb::get && path == "/api/cache") {
            auto entries = cache_.list_cached();
            json arr = json::array();
            for (const auto& e : entries) {
                arr.push_back(json{
                    {"torrent_id", e.torrent_id},
                    {"file_index", e.file_index},
                    {"file_path", e.file_path},
                    {"file_size", e.file_size},
                    {"progress", e.progress},
                    {"offline_ready", e.offline_ready},
                    {"last_access", std::chrono::duration_cast<std::chrono::seconds>(
                        e.last_access.time_since_epoch()).count()},
                    {"added_at", std::chrono::duration_cast<std::chrono::seconds>(
                        e.added.time_since_epoch()).count()}
                });
            }
            send_json(http::status::ok, json{{"cache", arr}});
            return;
        }

        send_error(http::status::not_found, "Not found");
        return;
    }

    auto torrent_id = m[1].str();
    auto sub_path = m[2].str();

    if (!sessions_.has_torrent(torrent_id)) {
        send_error(http::status::not_found, "Torrent not found");
        return;
    }

    // DELETE /api/torrents/{id}
    if (method == http::verb::delete_ && sub_path.empty()) {
        auto result = sessions_.remove_torrent(torrent_id, false);
        if (!result) {
            send_error(http::status::internal_server_error, result.error().message());
            return;
        }
        send_json(http::status::ok, json{{"status", "removed"}});
        return;
    }

    // GET /api/torrents/{id}/files
    if (method == http::verb::get && sub_path == "/files") {
        auto files_result = catalog_.list_files(torrent_id);
        if (!files_result) {
            send_error(http::status::bad_request, files_result.error().message());
            return;
        }

        json arr = json::array();
        for (const auto& f : files_result.value()) {
            arr.push_back(json{
                {"index", f.index},
                {"path", f.path},
                {"size", f.size},
                {"first_piece", f.first_piece},
                {"end_piece", f.end_piece}
            });
        }

        auto selected = catalog_.selected_file(torrent_id);
        send_json(http::status::ok, json{
            {"files", arr},
            {"selected_file", selected.has_value() ? json(*selected) : json(nullptr)}
        });
        return;
    }

    // POST /api/torrents/{id}/files/{fileId}/select
    static const std::regex re_select(R"(/files/(\d+)/select)");
    std::smatch sm;
    if (method == http::verb::post && std::regex_match(sub_path, sm, re_select)) {
        int file_idx;
        try {
            file_idx = std::stoi(sm[1].str());
        } catch (...) {
            send_error(http::status::bad_request, "Invalid file index");
            return;
        }

        auto handle = sessions_.get_handle(torrent_id);
        if (!handle.is_valid()) {
            send_error(http::status::internal_server_error, "Invalid torrent handle");
            return;
        }

        auto result = catalog_.select_file(torrent_id, file_idx, handle);
        if (!result) {
            send_error(http::status::bad_request, result.error().message());
            return;
        }

        send_json(http::status::ok, json{
            {"status", "selected"},
            {"file_index", file_idx}
        });
        return;
    }

    // POST /api/torrents/{id}/pause
    if (method == http::verb::post && sub_path == "/pause") {
        auto handle = sessions_.get_handle(torrent_id);
        if (!handle.is_valid()) {
            send_error(http::status::internal_server_error, "Invalid torrent handle");
            return;
        }
        handle.pause();
        send_json(http::status::ok, json{{"status", "paused"}});
        return;
    }

    // POST /api/torrents/{id}/resume
    if (method == http::verb::post && sub_path == "/resume") {
        auto handle = sessions_.get_handle(torrent_id);
        if (!handle.is_valid()) {
            send_error(http::status::internal_server_error, "Invalid torrent handle");
            return;
        }
        handle.resume();
        send_json(http::status::ok, json{{"status", "resumed"}});
        return;
    }

    // GET /api/torrents/{id}/status
    if (method == http::verb::get && sub_path == "/status") {
        auto handle = sessions_.get_handle(torrent_id);
        if (!handle.is_valid()) {
            send_error(http::status::internal_server_error, "Invalid torrent handle");
            return;
        }

        auto st = handle.status();
        auto selected = catalog_.selected_file(torrent_id);

        json status_json = {
            {"torrent_id", torrent_id},
            {"name", st.name},
            {"progress", st.progress},
            {"download_rate", st.download_rate},
            {"upload_rate", st.upload_rate},
            {"num_peers", st.num_peers},
            {"num_seeds", st.num_seeds},
            {"total_download", st.total_download},
            {"total_upload", st.total_upload},
            {"state", static_cast<int>(st.state)},
            {"paused", !!(st.flags & lt::torrent_flags::paused)},
            {"has_metadata", catalog_.has_metadata(torrent_id)},
            {"selected_file", selected.has_value() ? json(*selected) : json(nullptr)}
        };

        // Add file-level cache info if a file is selected
        if (selected.has_value()) {
            status_json["offline_ready"] = cache_.is_offline_ready(torrent_id, *selected);
        }

        send_json(http::status::ok, status_json);
        return;
    }

    // GET /api/torrents/{id}/stream-url
    if (method == http::verb::get && sub_path == "/stream-url") {
        auto selected = catalog_.selected_file(torrent_id);
        if (!selected.has_value()) {
            send_error(http::status::bad_request, "No file selected for streaming");
            return;
        }

        auto url = range_server_.stream_url(torrent_id, *selected);
        send_json(http::status::ok, json{
            {"stream_url", url},
            {"file_index", *selected}
        });
        return;
    }

    send_error(http::status::not_found, "Not found");
}

} // namespace seekserve
