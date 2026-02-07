#include <gtest/gtest.h>

#include <thread>
#include <chrono>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <nlohmann/json.hpp>
#include <libtorrent/torrent_info.hpp>

#include "seekserve/session_manager.hpp"
#include "seekserve/metadata_catalog.hpp"
#include "seekserve/http_range_server.hpp"
#include "seekserve/control_api_server.hpp"
#include "seekserve/offline_cache.hpp"
#include "seekserve/config.hpp"
#include "seekserve/types.hpp"

namespace seekserve {
namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;
using json = nlohmann::json;

static const std::string kSintelTorrent =
    std::string(SEEKSERVE_FIXTURE_DIR) + "/Sintel_archive.torrent";

class ControlApiIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        SessionConfig config;
        config.save_path = "/tmp/seekserve_api_test";
        config.listen_port_start = 17881;
        session_ = std::make_unique<TorrentSessionManager>(config);

        // Add Sintel torrent and get metadata
        auto result = session_->add_torrent({kSintelTorrent});
        ASSERT_TRUE(result.ok());
        torrent_id_ = result.value();

        auto handle = session_->get_handle(torrent_id_);
        auto ti = handle.torrent_file();
        ASSERT_NE(ti, nullptr);
        catalog_.on_metadata_received(torrent_id_, ti);

        // Set up cache in /tmp
        CacheConfig cache_cfg;
        cache_cfg.db_path = "/tmp/seekserve_api_test_cache.db";
        cache_ = std::make_unique<OfflineCacheManager>(cache_cfg);

        // Set up HTTP servers
        srv_config_.bind_address = "127.0.0.1";
        range_server_ = std::make_unique<HttpRangeServer>(ioc_, srv_config_);
        range_server_->set_auth_token(auth_token_);

        auto range_result = range_server_->start(0);
        ASSERT_TRUE(range_result.ok());
        range_port_ = range_result.value();

        api_server_ = std::make_unique<ControlApiServer>(
            ioc_, *session_, catalog_, *range_server_, *cache_, auth_token_);

        auto api_result = api_server_->start(0);
        ASSERT_TRUE(api_result.ok());
        api_port_ = api_result.value();

        io_thread_ = std::thread([this] { ioc_.run(); });
    }

    void TearDown() override {
        api_server_->stop();
        range_server_->stop();
        ioc_.stop();
        if (io_thread_.joinable()) io_thread_.join();

        if (session_ && !torrent_id_.empty()) {
            session_->remove_torrent(torrent_id_, true);
        }
        session_.reset();

        std::filesystem::remove("/tmp/seekserve_api_test_cache.db");
        std::filesystem::remove("/tmp/seekserve_api_test_cache.db-wal");
        std::filesystem::remove("/tmp/seekserve_api_test_cache.db-shm");
    }

    http::response<http::string_body> api_request(http::verb method,
                                                    const std::string& target,
                                                    const std::string& body = "") {
        net::io_context client_ioc;
        tcp::resolver resolver(client_ioc);
        beast::tcp_stream stream(client_ioc);

        auto results = resolver.resolve("127.0.0.1", std::to_string(api_port_));
        stream.connect(results);

        http::request<http::string_body> req{method, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::authorization, "Bearer " + auth_token_);
        if (!body.empty()) {
            req.set(http::field::content_type, "application/json");
            req.body() = body;
            req.prepare_payload();
        }

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        boost::system::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return res;
    }

    std::unique_ptr<TorrentSessionManager> session_;
    MetadataCatalog catalog_;
    TorrentId torrent_id_;
    std::string auth_token_ = "testtoken123";

    net::io_context ioc_;
    ServerConfig srv_config_;
    std::unique_ptr<HttpRangeServer> range_server_;
    std::unique_ptr<ControlApiServer> api_server_;
    std::unique_ptr<OfflineCacheManager> cache_;
    std::uint16_t range_port_ = 0;
    std::uint16_t api_port_ = 0;
    std::thread io_thread_;
};

TEST_F(ControlApiIntegrationTest, ListTorrents) {
    auto res = api_request(http::verb::get, "/api/torrents");
    EXPECT_EQ(res.result(), http::status::ok);

    auto j = json::parse(res.body());
    EXPECT_TRUE(j.contains("torrents"));
    ASSERT_GE(j["torrents"].size(), 1u);
    EXPECT_EQ(j["torrents"][0]["torrent_id"], torrent_id_);
    EXPECT_TRUE(j["torrents"][0]["has_metadata"].get<bool>());
}

TEST_F(ControlApiIntegrationTest, GetTorrentFiles) {
    auto res = api_request(http::verb::get, "/api/torrents/" + torrent_id_ + "/files");
    EXPECT_EQ(res.result(), http::status::ok);

    auto j = json::parse(res.body());
    EXPECT_TRUE(j.contains("files"));
    EXPECT_GE(j["files"].size(), 10u);

    // Check file 8 is in the list
    bool found_file8 = false;
    for (const auto& f : j["files"]) {
        if (f["index"] == 8) {
            found_file8 = true;
            EXPECT_NE(f["path"].get<std::string>().find("512kb"), std::string::npos);
        }
    }
    EXPECT_TRUE(found_file8);
}

TEST_F(ControlApiIntegrationTest, SelectFileAndGetStreamUrl) {
    // Select file 8
    auto res = api_request(http::verb::post,
                           "/api/torrents/" + torrent_id_ + "/files/8/select");
    EXPECT_EQ(res.result(), http::status::ok);

    auto j = json::parse(res.body());
    EXPECT_EQ(j["status"], "selected");
    EXPECT_EQ(j["file_index"], 8);

    // Get stream URL
    res = api_request(http::verb::get,
                      "/api/torrents/" + torrent_id_ + "/stream-url");
    EXPECT_EQ(res.result(), http::status::ok);

    j = json::parse(res.body());
    EXPECT_TRUE(j.contains("stream_url"));
    EXPECT_EQ(j["file_index"], 8);

    auto url = j["stream_url"].get<std::string>();
    EXPECT_NE(url.find("/stream/"), std::string::npos);
    EXPECT_NE(url.find("token="), std::string::npos);
}

TEST_F(ControlApiIntegrationTest, GetTorrentStatus) {
    auto res = api_request(http::verb::get,
                           "/api/torrents/" + torrent_id_ + "/status");
    EXPECT_EQ(res.result(), http::status::ok);

    auto j = json::parse(res.body());
    EXPECT_EQ(j["torrent_id"], torrent_id_);
    EXPECT_TRUE(j["has_metadata"].get<bool>());
    EXPECT_TRUE(j.contains("download_rate"));
    EXPECT_TRUE(j.contains("num_peers"));
}

TEST_F(ControlApiIntegrationTest, AuthRequired) {
    // Request without token
    net::io_context client_ioc;
    tcp::resolver resolver(client_ioc);
    beast::tcp_stream stream(client_ioc);

    auto results = resolver.resolve("127.0.0.1", std::to_string(api_port_));
    stream.connect(results);

    http::request<http::string_body> req{http::verb::get, "/api/torrents", 11};
    req.set(http::field::host, "127.0.0.1");
    // No auth header

    http::write(stream, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);

    EXPECT_EQ(res.result(), http::status::forbidden);
}

TEST_F(ControlApiIntegrationTest, TorrentNotFound) {
    auto res = api_request(http::verb::get,
                           "/api/torrents/deadbeef00000000/status");
    EXPECT_EQ(res.result(), http::status::not_found);
}

TEST_F(ControlApiIntegrationTest, NotFoundEndpoint) {
    auto res = api_request(http::verb::get, "/api/nonexistent");
    EXPECT_EQ(res.result(), http::status::not_found);
}

TEST_F(ControlApiIntegrationTest, GetCacheEntries) {
    auto res = api_request(http::verb::get, "/api/cache");
    EXPECT_EQ(res.result(), http::status::ok);

    auto j = json::parse(res.body());
    EXPECT_TRUE(j.contains("cache"));
    // Cache starts empty
    EXPECT_EQ(j["cache"].size(), 0u);
}

TEST_F(ControlApiIntegrationTest, StreamUrlRequiresFileSelection) {
    // No file selected yet
    auto res = api_request(http::verb::get,
                           "/api/torrents/" + torrent_id_ + "/stream-url");
    EXPECT_EQ(res.result(), http::status::bad_request);
}

TEST_F(ControlApiIntegrationTest, SelectInvalidFile) {
    auto res = api_request(http::verb::post,
                           "/api/torrents/" + torrent_id_ + "/files/999/select");
    EXPECT_EQ(res.result(), http::status::bad_request);
}

}  // namespace
}  // namespace seekserve
