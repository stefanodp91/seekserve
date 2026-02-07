#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <atomic>
#include <functional>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "seekserve/types.hpp"
#include "seekserve/error.hpp"
#include "seekserve/config.hpp"
#include "seekserve/session_manager.hpp"
#include "seekserve/metadata_catalog.hpp"
#include "seekserve/http_range_server.hpp"
#include "seekserve/offline_cache.hpp"

namespace seekserve {

class ControlApiServer {
public:
    ControlApiServer(net::io_context& ioc,
                     TorrentSessionManager& sessions,
                     MetadataCatalog& catalog,
                     HttpRangeServer& range_server,
                     OfflineCacheManager& cache,
                     const std::string& auth_token);
    ~ControlApiServer();

    Result<std::uint16_t> start(std::uint16_t port = 0);
    void stop();
    std::uint16_t port() const;

    using ShutdownCallback = std::function<void()>;
    void set_shutdown_callback(ShutdownCallback cb);

private:
    void do_accept();
    void handle_connection(tcp::socket socket);

    net::io_context& ioc_;
    TorrentSessionManager& sessions_;
    MetadataCatalog& catalog_;
    HttpRangeServer& range_server_;
    OfflineCacheManager& cache_;
    std::string auth_token_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::atomic<std::uint16_t> port_{0};
    std::atomic<bool> running_{false};
    std::atomic<int> active_connections_{0};
    ShutdownCallback shutdown_cb_;
};

} // namespace seekserve
