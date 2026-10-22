#include "seekserve_c.h"
#include "seekserve/engine.hpp"

#include <libtorrent/error_code.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstring>
#include <string>
#include <utility>

using json = nlohmann::json;

// The opaque handle is just a SeekServeEngine pointer.
struct SeekServeEngine : public seekserve::SeekServeEngine {
    using seekserve::SeekServeEngine::SeekServeEngine;
};

static ss_error_t map_error(const std::error_code& ec) {
    if (!ec) return SS_OK;

    using seekserve::errc;
    auto val = static_cast<errc>(ec.value());
    switch (val) {
        case errc::success:                return SS_OK;
        case errc::invalid_argument:       return SS_ERR_INVALID_ARG;
        case errc::torrent_not_found:
        case errc::file_not_found:         return SS_ERR_NOT_FOUND;
        case errc::metadata_not_ready:     return SS_ERR_METADATA_PENDING;
        case errc::timeout_waiting_for_piece: return SS_ERR_TIMEOUT;
        case errc::io_error:               return SS_ERR_IO;
        case errc::server_already_running: return SS_ERR_ALREADY_RUNNING;
        case errc::cancelled:              return SS_ERR_CANCELLED;
        default:                           return SS_ERR_IO;
    }
}

// No exception leaves the library: nothing beyond it can catch one (the
// app's JNI, Dart's FFI), and the process would end. libtorrent throws on a
// handle whose torrent another thread removed between the engine's check
// and its call (app BUG-199, BUG-200): to the caller that torrent is not
// there. Anything else is an error of the call.
template <typename Call>
static ss_error_t guarded(const char* name, Call&& call) noexcept {
    try {
        return std::forward<Call>(call)();
    } catch (const lt::system_error& e) {
        if (e.code() == lt::errors::invalid_torrent_handle) return SS_ERR_NOT_FOUND;
        spdlog::error("C API: {} failed: {}", name, e.what());
    } catch (const std::exception& e) {
        spdlog::error("C API: {} failed: {}", name, e.what());
    } catch (...) {
        spdlog::error("C API: {} failed", name);
    }
    return SS_ERR_IO;
}

static char* alloc_string(const std::string& s) {
    char* buf = new (std::nothrow) char[s.size() + 1];
    if (buf) {
        std::memcpy(buf, s.c_str(), s.size() + 1);
    }
    return buf;
}

static seekserve::SeekServeEngine::Config parse_config(const char* config_json) {
    seekserve::SeekServeEngine::Config config;

    if (!config_json || config_json[0] == '\0') {
        return config;
    }

    try {
        auto j = json::parse(config_json);

        if (j.contains("save_path"))
            config.session.save_path = j["save_path"].get<std::string>();
        if (j.contains("auth_token"))
            config.auth_token = j["auth_token"].get<std::string>();
        if (j.contains("enable_webtorrent"))
            config.session.enable_webtorrent = j["enable_webtorrent"].get<bool>();
        if (j.contains("stream_port"))
            config.server.stream_port = j["stream_port"].get<std::uint16_t>();
        if (j.contains("control_port"))
            config.server.control_port = j["control_port"].get<std::uint16_t>();
        if (j.contains("max_storage_bytes"))
            config.cache.max_storage_bytes = j["max_storage_bytes"].get<std::int64_t>();
        if (j.contains("cache_db_path"))
            config.cache.db_path = j["cache_db_path"].get<std::string>();
        if (j.contains("log_level")) {
            auto level = j["log_level"].get<std::string>();
            if (level == "debug") spdlog::set_level(spdlog::level::debug);
            else if (level == "warn") spdlog::set_level(spdlog::level::warn);
            else if (level == "error") spdlog::set_level(spdlog::level::err);
            else spdlog::set_level(spdlog::level::info);
        }
        if (j.contains("extra_trackers")) {
            config.session.extra_trackers.clear();
            for (const auto& t : j["extra_trackers"]) {
                config.session.extra_trackers.push_back(t.get<std::string>());
            }
        }
        if (j.contains("max_concurrent_torrents"))
            config.session.max_active_downloads = j["max_concurrent_torrents"].get<int>();
        if (j.contains("ca_cert_file"))
            config.session.ca_cert_file = j["ca_cert_file"].get<std::string>();
        if (j.contains("proxy_enabled"))
            config.session.proxy.enabled = j["proxy_enabled"].get<bool>();
        if (j.contains("proxy_hostname"))
            config.session.proxy.hostname = j["proxy_hostname"].get<std::string>();
        if (j.contains("proxy_port"))
            config.session.proxy.port = j["proxy_port"].get<int>();
        // Read after the proxy keys: a value of the wrong type stops the
        // parsing here, and must not cost the settings above.
        if (j.contains("resume_data"))
            config.session.resume_data = j["resume_data"].get<bool>();
        if (j.contains("resume_save_interval"))
            config.session.resume_save_interval =
                std::chrono::seconds(j["resume_save_interval"].get<int>());
        // After the proxy and resume keys too, for the same reason (app
        // BUG-198).
        if (j.contains("close_redundant_connections"))
            config.session.close_redundant_connections =
                j["close_redundant_connections"].get<bool>();
        if (j.contains("min_reconnect_time"))
            config.session.min_reconnect_time =
                std::chrono::seconds(j["min_reconnect_time"].get<int>());
        // After every other key too (app BUG-88, WORK-37).
        if (j.contains("read_timeout"))
            config.server.read_timeout = std::chrono::seconds(j["read_timeout"].get<int>());
        if (j.contains("connection_timeout"))
            config.server.connection_timeout =
                std::chrono::seconds(j["connection_timeout"].get<int>());
        if (j.contains("stale_reader_timeout"))
            config.server.stale_reader_timeout =
                std::chrono::seconds(j["stale_reader_timeout"].get<int>());
    } catch (const std::exception& e) {
        spdlog::warn("C API: failed to parse config JSON: {}", e.what());
    }

    return config;
}

extern "C" {

SeekServeEngine* ss_engine_create(const char* config_json) {
    try {
        auto config = parse_config(config_json);
        return new (std::nothrow) SeekServeEngine(config);
    } catch (const std::exception& e) {
        spdlog::error("C API: ss_engine_create failed: {}", e.what());
        return nullptr;
    } catch (...) {
        spdlog::error("C API: ss_engine_create failed");
        return nullptr;
    }
}

void ss_engine_destroy(SeekServeEngine* engine) {
    delete engine;
}

ss_error_t ss_add_torrent(SeekServeEngine* engine, const char* uri,
                          char* out_torrent_id, int32_t out_torrent_id_len) {
    if (!engine || !uri) return SS_ERR_INVALID_ARG;

    return guarded("ss_add_torrent", [&]() -> ss_error_t {
        auto result = engine->add_torrent(uri);
        if (!result) return map_error(result.error());

        const auto& id = result.value();
        if (out_torrent_id && out_torrent_id_len > 0) {
            auto copy_len = std::min(static_cast<std::size_t>(out_torrent_id_len - 1), id.size());
            std::memcpy(out_torrent_id, id.c_str(), copy_len);
            out_torrent_id[copy_len] = '\0';
        }

        return SS_OK;
    });
}

ss_error_t ss_remove_torrent(SeekServeEngine* engine, const char* torrent_id,
                             bool delete_files) {
    if (!engine || !torrent_id) return SS_ERR_INVALID_ARG;

    return guarded("ss_remove_torrent", [&]() -> ss_error_t {
        auto result = engine->remove_torrent(torrent_id, delete_files);
        return result ? SS_OK : map_error(result.error());
    });
}

ss_error_t ss_pause_torrent(SeekServeEngine* engine, const char* torrent_id) {
    if (!engine || !torrent_id) return SS_ERR_INVALID_ARG;

    return guarded("ss_pause_torrent", [&]() -> ss_error_t {
        auto result = engine->pause_torrent(torrent_id);
        return result ? SS_OK : map_error(result.error());
    });
}

ss_error_t ss_resume_torrent(SeekServeEngine* engine, const char* torrent_id) {
    if (!engine || !torrent_id) return SS_ERR_INVALID_ARG;

    return guarded("ss_resume_torrent", [&]() -> ss_error_t {
        auto result = engine->resume_torrent(torrent_id);
        return result ? SS_OK : map_error(result.error());
    });
}

ss_error_t ss_start_torrent(SeekServeEngine* engine, const char* torrent_id) {
    if (!engine || !torrent_id) return SS_ERR_INVALID_ARG;

    return guarded("ss_start_torrent", [&]() -> ss_error_t {
        auto result = engine->start_torrent(torrent_id);
        return result ? SS_OK : map_error(result.error());
    });
}

ss_error_t ss_force_reannounce(SeekServeEngine* engine, const char* torrent_id) {
    if (!engine || !torrent_id) return SS_ERR_INVALID_ARG;

    return guarded("ss_force_reannounce", [&]() -> ss_error_t {
        auto result = engine->force_reannounce(torrent_id);
        return result ? SS_OK : map_error(result.error());
    });
}

ss_error_t ss_recheck_torrent(SeekServeEngine* engine, const char* torrent_id) {
    if (!engine || !torrent_id) return SS_ERR_INVALID_ARG;

    return guarded("ss_recheck_torrent", [&]() -> ss_error_t {
        auto result = engine->recheck_torrent(torrent_id);
        return result ? SS_OK : map_error(result.error());
    });
}

ss_error_t ss_list_torrents(SeekServeEngine* engine, char** out_json) {
    if (!engine || !out_json) return SS_ERR_INVALID_ARG;

    return guarded("ss_list_torrents", [&]() -> ss_error_t {
        auto ids = engine->list_torrents();
        json arr = json::array();
        for (const auto& id : ids) {
            arr.push_back(id);
        }

        *out_json = alloc_string(arr.dump());
        return *out_json ? SS_OK : SS_ERR_IO;
    });
}

ss_error_t ss_list_files(SeekServeEngine* engine, const char* torrent_id,
                         char** out_json) {
    if (!engine || !torrent_id || !out_json) return SS_ERR_INVALID_ARG;

    return guarded("ss_list_files", [&]() -> ss_error_t {
        auto result = engine->list_files(torrent_id);
        if (!result) return map_error(result.error());

        json arr = json::array();
        for (const auto& f : result.value()) {
            arr.push_back(json{
                {"index", f.index},
                {"path", f.path},
                {"size", f.size},
                {"first_piece", f.first_piece},
                {"end_piece", f.end_piece}
            });
        }

        json response = {{"files", arr}};
        *out_json = alloc_string(response.dump());
        return *out_json ? SS_OK : SS_ERR_IO;
    });
}

ss_error_t ss_select_file(SeekServeEngine* engine, const char* torrent_id,
                          int32_t file_index) {
    if (!engine || !torrent_id) return SS_ERR_INVALID_ARG;

    return guarded("ss_select_file", [&]() -> ss_error_t {
        auto result = engine->select_file(torrent_id, file_index);
        return result ? SS_OK : map_error(result.error());
    });
}

ss_error_t ss_set_wanted_files(SeekServeEngine* engine, const char* torrent_id,
                               int32_t reader_file, int32_t background_file) {
    if (!engine || !torrent_id) return SS_ERR_INVALID_ARG;

    return guarded("ss_set_wanted_files", [&]() -> ss_error_t {
        auto result = engine->set_wanted_files(torrent_id, reader_file, background_file);
        return result ? SS_OK : map_error(result.error());
    });
}

ss_error_t ss_get_stream_url(SeekServeEngine* engine, const char* torrent_id,
                             int32_t file_index, char** out_url) {
    if (!engine || !torrent_id || !out_url) return SS_ERR_INVALID_ARG;

    return guarded("ss_get_stream_url", [&]() -> ss_error_t {
        auto result = engine->get_stream_url(torrent_id, file_index);
        if (!result) return map_error(result.error());

        *out_url = alloc_string(result.value());
        return *out_url ? SS_OK : SS_ERR_IO;
    });
}

ss_error_t ss_open_share(SeekServeEngine* engine, const char* torrent_id,
                         int32_t file_index, const char* bind_address,
                         const char* peer_address, char** out_json) {
    if (!engine || !torrent_id || !bind_address || !peer_address || !out_json) {
        return SS_ERR_INVALID_ARG;
    }

    return guarded("ss_open_share", [&]() -> ss_error_t {
        auto result = engine->open_share(torrent_id, file_index, bind_address, peer_address);
        if (!result) return map_error(result.error());

        json out = {{"share_id", result.value().id}, {"url", result.value().url}};
        *out_json = alloc_string(out.dump());
        return *out_json ? SS_OK : SS_ERR_IO;
    });
}

ss_error_t ss_close_share(SeekServeEngine* engine, const char* share_id) {
    if (!engine || !share_id) return SS_ERR_INVALID_ARG;

    return guarded("ss_close_share", [&]() -> ss_error_t {
        auto result = engine->close_share(share_id);
        return result ? SS_OK : map_error(result.error());
    });
}

ss_error_t ss_get_status(SeekServeEngine* engine, const char* torrent_id,
                         char** out_json) {
    if (!engine || !torrent_id || !out_json) return SS_ERR_INVALID_ARG;

    return guarded("ss_get_status", [&]() -> ss_error_t {
        auto status_str = engine->get_status_json(torrent_id);
        *out_json = alloc_string(status_str);
        return *out_json ? SS_OK : SS_ERR_IO;
    });
}

ss_error_t ss_set_event_callback(SeekServeEngine* engine,
                                 ss_event_callback_t callback, void* user_data) {
    if (!engine) return SS_ERR_INVALID_ARG;

    return guarded("ss_set_event_callback", [&]() -> ss_error_t {
        if (!callback) {
            engine->set_event_callback(nullptr);
            return SS_OK;
        }

        // Heap-allocate the event string so it survives until the Dart side
        // processes it (NativeCallable.listener dispatches asynchronously).
        // The callee frees via ss_free_string().
        engine->set_event_callback([callback, user_data](const std::string& event_json) {
            char* buf = alloc_string(event_json);
            if (buf) {
                callback(buf, user_data);
            }
        });

        return SS_OK;
    });
}

ss_error_t ss_start_server(SeekServeEngine* engine, uint16_t port,
                           uint16_t* out_port) {
    if (!engine) return SS_ERR_INVALID_ARG;

    return guarded("ss_start_server", [&]() -> ss_error_t {
        auto result = engine->start_server(port);
        if (!result) return map_error(result.error());

        if (out_port) *out_port = result.value();
        return SS_OK;
    });
}

ss_error_t ss_stop_server(SeekServeEngine* engine) {
    if (!engine) return SS_ERR_INVALID_ARG;

    return guarded("ss_stop_server", [&]() -> ss_error_t {
        engine->stop_server();
        return SS_OK;
    });
}

ss_error_t ss_set_proxy(SeekServeEngine* engine, const char* proxy_json) {
    if (!engine) return SS_ERR_INVALID_ARG;

    seekserve::ProxyConfig proxy;
    if (proxy_json && proxy_json[0] != '\0') {
        try {
            auto j = json::parse(proxy_json);
            if (j.contains("enabled"))
                proxy.enabled = j["enabled"].get<bool>();
            if (j.contains("hostname"))
                proxy.hostname = j["hostname"].get<std::string>();
            if (j.contains("port"))
                proxy.port = j["port"].get<int>();
        } catch (const std::exception& e) {
            spdlog::warn("C API: failed to parse proxy JSON: {}", e.what());
            return SS_ERR_INVALID_ARG;
        }
    }

    return guarded("ss_set_proxy", [&]() -> ss_error_t {
        engine->set_proxy(proxy);
        return SS_OK;
    });
}

void ss_free_string(char* str) {
    delete[] str;
}

} // extern "C"
