#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <filesystem>

#include "seekserve_c.h"

namespace fs = std::filesystem;

class CApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = "/tmp/seekserve_capi_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
    }

    std::string make_config(const std::string& extra = "") {
        return R"({"save_path":")" + tmp_dir_ + R"(","auth_token":"testtoken","log_level":"warn")" +
               (extra.empty() ? "" : "," + extra) + "}";
    }

    std::string torrent_path() {
        return std::string(SEEKSERVE_FIXTURE_DIR) + "/Sintel_archive.torrent";
    }

    std::string tmp_dir_;
};

// --- Engine lifecycle tests ---

TEST_F(CApiTest, CreateAndDestroyEngine) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    ss_engine_destroy(engine);
}

TEST_F(CApiTest, CreateWithNullConfig) {
    SeekServeEngine* engine = ss_engine_create(nullptr);
    ASSERT_NE(engine, nullptr);  // should use defaults
    ss_engine_destroy(engine);
}

TEST_F(CApiTest, CreateWithEmptyConfig) {
    SeekServeEngine* engine = ss_engine_create("");
    ASSERT_NE(engine, nullptr);
    ss_engine_destroy(engine);
}

TEST_F(CApiTest, CreateWithInvalidJson) {
    SeekServeEngine* engine = ss_engine_create("{not valid json");
    ASSERT_NE(engine, nullptr);  // should use defaults, not crash
    ss_engine_destroy(engine);
}

TEST_F(CApiTest, DestroyNull) {
    ss_engine_destroy(nullptr);  // should not crash
}

// --- Null engine guard tests ---

TEST_F(CApiTest, NullEngineReturnsError) {
    EXPECT_EQ(ss_add_torrent(nullptr, "magnet:?xt=urn:btih:abc", nullptr, 0), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_remove_torrent(nullptr, "abc", false), SS_ERR_INVALID_ARG);

    char* json = nullptr;
    EXPECT_EQ(ss_list_files(nullptr, "abc", &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_select_file(nullptr, "abc", 0), SS_ERR_INVALID_ARG);

    char* url = nullptr;
    EXPECT_EQ(ss_get_stream_url(nullptr, "abc", 0, &url), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_get_status(nullptr, "abc", &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_set_event_callback(nullptr, nullptr, nullptr), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_start_server(nullptr, 0, nullptr), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_stop_server(nullptr), SS_ERR_INVALID_ARG);
}

// --- Free string ---

TEST_F(CApiTest, FreeStringNull) {
    ss_free_string(nullptr);  // should not crash
}

// --- Add torrent with .torrent file ---

TEST_F(CApiTest, AddTorrentFromFile) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    char id_buf[128] = {};
    auto path = torrent_path();
    ss_error_t err = ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf));
    EXPECT_EQ(err, SS_OK);
    EXPECT_GT(std::strlen(id_buf), 0u);

    // The torrent ID should be a hex string (40 chars for v1 infohash)
    EXPECT_EQ(std::strlen(id_buf), 40u);

    ss_engine_destroy(engine);
}

TEST_F(CApiTest, AddTorrentNullUri) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(ss_add_torrent(engine, nullptr, nullptr, 0), SS_ERR_INVALID_ARG);

    ss_engine_destroy(engine);
}

TEST_F(CApiTest, AddTorrentSmallBuffer) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    // Buffer too small for full hash — should truncate
    char id_buf[10] = {};
    auto path = torrent_path();
    ss_error_t err = ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf));
    EXPECT_EQ(err, SS_OK);
    EXPECT_EQ(std::strlen(id_buf), 9u);  // 10-1 for null terminator

    ss_engine_destroy(engine);
}

// --- List files (requires metadata) ---

TEST_F(CApiTest, ListFilesAfterAddTorrent) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);

    // Wait briefly for metadata (from .torrent file it should be near-instant)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    char* json = nullptr;
    ss_error_t err = ss_list_files(engine, id_buf, &json);
    EXPECT_EQ(err, SS_OK);
    ASSERT_NE(json, nullptr);

    // Verify JSON contains files array
    std::string json_str(json);
    EXPECT_NE(json_str.find("\"files\""), std::string::npos);
    EXPECT_NE(json_str.find("sintel-2048-stereo_512kb.mp4"), std::string::npos);

    ss_free_string(json);
    ss_engine_destroy(engine);
}

TEST_F(CApiTest, ListFilesNonexistentTorrent) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    char* json = nullptr;
    ss_error_t err = ss_list_files(engine, "nonexistent_id", &json);
    EXPECT_NE(err, SS_OK);

    ss_engine_destroy(engine);
}

// --- Server lifecycle ---

TEST_F(CApiTest, StartAndStopServer) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    uint16_t port = 0;
    ss_error_t err = ss_start_server(engine, 0, &port);
    EXPECT_EQ(err, SS_OK);
    EXPECT_GT(port, 0);

    // Starting again should fail
    uint16_t port2 = 0;
    err = ss_start_server(engine, 0, &port2);
    EXPECT_EQ(err, SS_ERR_ALREADY_RUNNING);

    err = ss_stop_server(engine);
    EXPECT_EQ(err, SS_OK);

    ss_engine_destroy(engine);
}

// --- Event callback ---

TEST_F(CApiTest, SetEventCallback) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    std::atomic<int> call_count{0};
    std::string last_event;

    auto cb = [](const char* event_json, void* user_data) {
        auto* count = static_cast<std::atomic<int>*>(user_data);
        count->fetch_add(1);
    };

    EXPECT_EQ(ss_set_event_callback(engine, cb, &call_count), SS_OK);

    // Clear callback
    EXPECT_EQ(ss_set_event_callback(engine, nullptr, nullptr), SS_OK);

    ss_engine_destroy(engine);
}

// --- Get status ---

TEST_F(CApiTest, GetStatusForAddedTorrent) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    char* json = nullptr;
    ss_error_t err = ss_get_status(engine, id_buf, &json);
    EXPECT_EQ(err, SS_OK);
    ASSERT_NE(json, nullptr);

    std::string json_str(json);
    EXPECT_NE(json_str.find("\"torrent_id\""), std::string::npos);
    EXPECT_NE(json_str.find("\"progress\""), std::string::npos);
    EXPECT_NE(json_str.find("\"has_metadata\""), std::string::npos);

    ss_free_string(json);
    ss_engine_destroy(engine);
}

TEST_F(CApiTest, GetStatusNonexistentTorrent) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    char* json = nullptr;
    ss_error_t err = ss_get_status(engine, "nonexistent", &json);
    // get_status_json returns JSON with error field, not an error code
    EXPECT_EQ(err, SS_OK);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(std::string(json).find("error"), std::string::npos);

    ss_free_string(json);
    ss_engine_destroy(engine);
}

// --- Remove torrent ---

TEST_F(CApiTest, RemoveTorrent) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);

    ss_error_t err = ss_remove_torrent(engine, id_buf, false);
    EXPECT_EQ(err, SS_OK);

    // Listing files should fail after removal
    char* json = nullptr;
    err = ss_list_files(engine, id_buf, &json);
    EXPECT_NE(err, SS_OK);

    ss_engine_destroy(engine);
}

TEST_F(CApiTest, RemoveNonexistentTorrent) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    ss_error_t err = ss_remove_torrent(engine, "nonexistent", false);
    EXPECT_EQ(err, SS_ERR_NOT_FOUND);

    ss_engine_destroy(engine);
}

// --- Full lifecycle test ---

TEST_F(CApiTest, FullLifecycle) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    // Start server
    uint16_t port = 0;
    ASSERT_EQ(ss_start_server(engine, 0, &port), SS_OK);
    EXPECT_GT(port, 0);

    // Add torrent
    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);

    // Wait for metadata
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // List files
    char* files_json = nullptr;
    ASSERT_EQ(ss_list_files(engine, id_buf, &files_json), SS_OK);
    ASSERT_NE(files_json, nullptr);
    EXPECT_NE(std::string(files_json).find("sintel"), std::string::npos);
    ss_free_string(files_json);

    // Get status
    char* status_json = nullptr;
    ASSERT_EQ(ss_get_status(engine, id_buf, &status_json), SS_OK);
    ASSERT_NE(status_json, nullptr);
    ss_free_string(status_json);

    // Stop server
    ASSERT_EQ(ss_stop_server(engine), SS_OK);

    // Cleanup
    ss_engine_destroy(engine);
}

// --- Pause / Resume tests ---

TEST_F(CApiTest, PauseTorrentNullEngine) {
    EXPECT_EQ(ss_pause_torrent(nullptr, "abc"), SS_ERR_INVALID_ARG);
}

TEST_F(CApiTest, PauseTorrentNullId) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(ss_pause_torrent(engine, nullptr), SS_ERR_INVALID_ARG);

    ss_engine_destroy(engine);
}

TEST_F(CApiTest, PauseTorrentNotFound) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(ss_pause_torrent(engine, "nonexistent"), SS_ERR_NOT_FOUND);

    ss_engine_destroy(engine);
}

TEST_F(CApiTest, ResumeTorrentNullEngine) {
    EXPECT_EQ(ss_resume_torrent(nullptr, "abc"), SS_ERR_INVALID_ARG);
}

TEST_F(CApiTest, ResumeTorrentNotFound) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(ss_resume_torrent(engine, "nonexistent"), SS_ERR_NOT_FOUND);

    ss_engine_destroy(engine);
}

TEST_F(CApiTest, PauseAndResumeTorrent) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);

    // Pause
    EXPECT_EQ(ss_pause_torrent(engine, id_buf), SS_OK);

    // Verify status shows paused
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    char* json = nullptr;
    ASSERT_EQ(ss_get_status(engine, id_buf, &json), SS_OK);
    ASSERT_NE(json, nullptr);
    std::string status_str(json);
    EXPECT_NE(status_str.find("\"paused\":true"), std::string::npos);
    ss_free_string(json);

    // Resume
    EXPECT_EQ(ss_resume_torrent(engine, id_buf), SS_OK);

    // Verify status shows not paused
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    json = nullptr;
    ASSERT_EQ(ss_get_status(engine, id_buf, &json), SS_OK);
    ASSERT_NE(json, nullptr);
    status_str = std::string(json);
    EXPECT_NE(status_str.find("\"paused\":false"), std::string::npos);
    ss_free_string(json);

    ss_engine_destroy(engine);
}
