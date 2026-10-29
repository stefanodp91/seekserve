#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>


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

    // Offline: the fixture torrent has trackers and web seeds, and an engine
    // restores and starts the torrents of its cache database. So every engine
    // of these tests is behind a proxy on a closed port (no DHT either) and
    // keeps its database in the test's own folder, not in the working one.
    std::string make_config(const std::string& extra = "") {
        return R"({"save_path":")" + tmp_dir_ + R"(","auth_token":"testtoken","log_level":"warn",)" +
               R"("proxy_enabled":true,"proxy_hostname":"127.0.0.1","proxy_port":1,)" +
               R"("cache_db_path":")" + db_path() + R"(")" +
               (extra.empty() ? "" : "," + extra) + "}";
    }

    std::string db_path() { return tmp_dir_ + "/cache.db"; }

    // A copy of the fixture inside the test's folder, to take away later.
    std::string torrent_copy() {
        auto copy = tmp_dir_ + "/copy.torrent";
        fs::copy_file(torrent_path(), copy, fs::copy_options::overwrite_existing);
        return copy;
    }

    // The resume data the cache database holds for a torrent; empty if none.
    std::vector<char> stored_resume_data(const std::string& id) {
        std::vector<char> out;
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(db_path().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            return out;
        }
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT resume_data FROM torrents WHERE torrent_id = ?;", -1,
                               &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                auto* bytes = static_cast<const char*>(sqlite3_column_blob(stmt, 0));
                int size = sqlite3_column_bytes(stmt, 0);
                if (bytes && size > 0) out.assign(bytes, bytes + size);
            }
        }
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return out;
    }

    static std::string list_torrents(SeekServeEngine* engine) {
        char* json = nullptr;
        EXPECT_EQ(ss_list_torrents(engine, &json), SS_OK);
        std::string out = json ? json : "";
        ss_free_string(json);
        return out;
    }

    static std::string status(SeekServeEngine* engine, const std::string& id) {
        char* json = nullptr;
        ss_get_status(engine, id.c_str(), &json);
        std::string out = json ? json : "";
        ss_free_string(json);
        return out;
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

// The engine's warnings in the system log too, when the app asks (its
// debug builds, app BUG-266): on Android only; elsewhere the key changes
// nothing, and any engine created after the first one works the same.
TEST_F(CApiTest, LogToSystemKeepsTheEngineWorking) {
    auto config = make_config(R"("log_to_system":true)");
    SeekServeEngine* first = ss_engine_create(config.c_str());
    ASSERT_NE(first, nullptr);
    SeekServeEngine* second = ss_engine_create(config.c_str());
    ASSERT_NE(second, nullptr);
    uint16_t port = 0;
    EXPECT_EQ(ss_start_server(first, 0, &port), SS_OK);
    EXPECT_NE(port, 0);
    ss_engine_destroy(second);
    ss_engine_destroy(first);
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

TEST_F(CApiTest, StartTorrentNullEngine) {
    EXPECT_EQ(ss_start_torrent(nullptr, "abc"), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_start_torrent(reinterpret_cast<SeekServeEngine*>(0x1), nullptr),
              SS_ERR_INVALID_ARG);
}

TEST_F(CApiTest, StartTorrentNotFound) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(ss_start_torrent(engine, "nonexistent"), SS_ERR_NOT_FOUND);

    ss_engine_destroy(engine);
}

TEST_F(CApiTest, RecheckTorrentNullArgs) {
    EXPECT_EQ(ss_recheck_torrent(nullptr, "abc"), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_recheck_torrent(reinterpret_cast<SeekServeEngine*>(0x1), nullptr),
              SS_ERR_INVALID_ARG);
}

TEST_F(CApiTest, RecheckTorrentNotFound) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(ss_recheck_torrent(engine, "nonexistent"), SS_ERR_NOT_FOUND);

    ss_engine_destroy(engine);
}

// A paused torrent started outside the queue runs even when the only queue
// slot is set, and no network is used (proxy on port 0).
TEST_F(CApiTest, PauseAndStartTorrentOutsideQueue) {
    auto config = make_config(
        R"("max_concurrent_torrents":1,"proxy_enabled":true,)"
        R"("proxy_hostname":"127.0.0.1","proxy_port":0)");
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);
    ASSERT_EQ(ss_pause_torrent(engine, id_buf), SS_OK);

    EXPECT_EQ(ss_start_torrent(engine, id_buf), SS_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    char* json = nullptr;
    ASSERT_EQ(ss_get_status(engine, id_buf, &json), SS_OK);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(std::string(json).find("\"paused\":false"), std::string::npos);
    ss_free_string(json);

    ss_engine_destroy(engine);
}

// Removed and added again in the same session, a torrent gets its files
// again (app BUG-68): the guard against late alerts of the removed torrent
// skipped the new one's alerts too. No network: proxy on port 0, and the
// fixture is a .torrent file with its metadata.
TEST_F(CApiTest, AddedAgainAfterRemovalGetsItsMetadata) {
    auto config = make_config(
        R"("proxy_enabled":true,"proxy_hostname":"127.0.0.1","proxy_port":0)");
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    auto has_metadata = [&](const char* id) {
        for (int i = 0; i < 50; ++i) {
            char* json = nullptr;
            if (ss_get_status(engine, id, &json) == SS_OK && json) {
                bool yes = std::string(json).find("\"has_metadata\":true") != std::string::npos;
                ss_free_string(json);
                if (yes) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    };

    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);
    ASSERT_TRUE(has_metadata(id_buf));

    ASSERT_EQ(ss_remove_torrent(engine, id_buf, false), SS_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    char again[128] = {};
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), again, sizeof(again)), SS_OK);
    EXPECT_STREQ(again, id_buf);
    EXPECT_TRUE(has_metadata(again));

    char* files = nullptr;
    EXPECT_EQ(ss_list_files(engine, again, &files), SS_OK);
    if (files) ss_free_string(files);

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

// --- Resume data keys (app WORK-29, wave P2b) ---

// "resume_data": the engine keeps what a torrent needs to come back and
// starts again from it. Here the .torrent file is gone before the second
// engine: only the resume data can bring the torrent back, and it is paused.
TEST_F(CApiTest, ResumeDataKeyBringsATorrentBackWithoutItsFile) {
    auto config = make_config(R"("resume_data":true)");
    auto copy = torrent_copy();
    char id_buf[128] = {};
    {
        SeekServeEngine* engine = ss_engine_create(config.c_str());
        ASSERT_NE(engine, nullptr);
        ASSERT_EQ(ss_add_torrent(engine, copy.c_str(), id_buf, sizeof(id_buf)), SS_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ss_engine_destroy(engine);
    }
    std::string id(id_buf);
    EXPECT_FALSE(stored_resume_data(id).empty());
    fs::remove(copy);

    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_NE(list_torrents(engine).find(id), std::string::npos) << list_torrents(engine);
    auto st = status(engine, id);
    EXPECT_NE(st.find("\"has_metadata\":true"), std::string::npos) << st;
    EXPECT_NE(st.find("\"paused\":true"), std::string::npos) << st;

    ss_engine_destroy(engine);
}

// "close_redundant_connections" is read after the other keys (app BUG-198):
// a value of the wrong type costs none of them. Here the resume data shows
// it, without network; the proxy keys are read before the resume ones.
TEST_F(CApiTest, ABadSeedersChoiceCostsNoOtherKey) {
    auto config = make_config(R"("resume_data":true,"close_redundant_connections":"no")");
    auto copy = torrent_copy();
    char id_buf[128] = {};
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(ss_add_torrent(engine, copy.c_str(), id_buf, sizeof(id_buf)), SS_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ss_engine_destroy(engine);

    EXPECT_FALSE(stored_resume_data(std::string(id_buf)).empty());
}

// Without the key the engine is the one from before: nothing is kept, and a
// torrent whose .torrent file is gone does not come back.
TEST_F(CApiTest, WithoutTheResumeDataKeyNothingIsKept) {
    auto config = make_config();
    auto copy = torrent_copy();
    char id_buf[128] = {};
    {
        SeekServeEngine* engine = ss_engine_create(config.c_str());
        ASSERT_NE(engine, nullptr);
        ASSERT_EQ(ss_add_torrent(engine, copy.c_str(), id_buf, sizeof(id_buf)), SS_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ss_engine_destroy(engine);
    }
    std::string id(id_buf);
    EXPECT_TRUE(stored_resume_data(id).empty());
    fs::remove(copy);

    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(list_torrents(engine).find(id), std::string::npos) << list_torrents(engine);

    ss_engine_destroy(engine);
}

// "resume_save_interval" (seconds): a torrent that changed is saved again by
// the timer, with no pause and no shutdown. Choosing a file changes it (its
// file priorities) and no event saves that.
TEST_F(CApiTest, ResumeSaveIntervalKeySavesAChangedTorrentAgain) {
    auto config = make_config(R"("resume_data":true,"resume_save_interval":1)");
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    uint16_t port = 0;
    ASSERT_EQ(ss_start_server(engine, 0, &port), SS_OK);  // the timer runs with the servers
    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);
    std::string id(id_buf);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    auto before = stored_resume_data(id);
    ASSERT_FALSE(before.empty());

    ASSERT_EQ(ss_select_file(engine, id.c_str(), 0), SS_OK);

    bool changed = false;
    for (int i = 0; i < 50 && !changed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        changed = stored_resume_data(id) != before;
    }
    EXPECT_TRUE(changed) << "the timer never saved the new file priorities";

    ss_engine_destroy(engine);
}

// Without an interval only events save: choosing a file is not one.
TEST_F(CApiTest, WithoutAnIntervalAChangedTorrentWaitsForAnEvent) {
    auto config = make_config(R"("resume_data":true)");
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    uint16_t port = 0;
    ASSERT_EQ(ss_start_server(engine, 0, &port), SS_OK);
    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);
    std::string id(id_buf);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto before = stored_resume_data(id);
    ASSERT_FALSE(before.empty());

    ASSERT_EQ(ss_select_file(engine, id.c_str(), 0), SS_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    EXPECT_EQ(stored_resume_data(id), before);

    ss_engine_destroy(engine);
}

// --- The two files a torrent may want at once (app WORK-29, phase P4) ---

namespace {

std::string status_json(SeekServeEngine* engine, const std::string& id) {
    char* json = nullptr;
    if (ss_get_status(engine, id.c_str(), &json) != SS_OK || !json) return "";
    std::string out(json);
    ss_free_string(json);
    return out;
}

// Waits for a status that has every text of `with` and none of `without`.
bool status_becomes(SeekServeEngine* engine, const std::string& id,
                    const std::vector<std::string>& with,
                    const std::vector<std::string>& without = {}) {
    for (int i = 0; i < 100; ++i) {
        const auto status = status_json(engine, id);
        bool ok = !status.empty();
        for (const auto& text : with) ok = ok && status.find(text) != std::string::npos;
        for (const auto& text : without) ok = ok && status.find(text) == std::string::npos;
        if (ok) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

}  // namespace

TEST_F(CApiTest, SetWantedFilesNullArgs) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(ss_set_wanted_files(nullptr, "abc", 0, 1), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_set_wanted_files(engine, nullptr, 0, 1), SS_ERR_INVALID_ARG);

    ss_engine_destroy(engine);
}

// The file a player reads and the one that downloads behind it, each said
// in the status; the player leaves; nothing is wanted; and ss_select_file
// after it is that file alone.
TEST_F(CApiTest, SetWantedFilesSaysWhichFileIsWhose) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);
    std::string id(id_buf);
    ASSERT_TRUE(status_becomes(engine, id, {"\"has_metadata\":true"})) << status_json(engine, id);

    // Files 2 and 3 of the fixture: neither is a subtitle file.
    ASSERT_EQ(ss_set_wanted_files(engine, id.c_str(), 2, 3), SS_OK);
    EXPECT_TRUE(status_becomes(engine, id,
                               {"\"index\":2,\"role\":\"reader\"",
                                "\"index\":3,\"role\":\"background\"",
                                "\"selected_file\":2"}))
        << status_json(engine, id);

    ASSERT_EQ(ss_set_wanted_files(engine, id.c_str(), -1, 3), SS_OK);
    EXPECT_TRUE(status_becomes(engine, id,
                               {"\"index\":3,\"role\":\"background\"", "\"selected_file\":3"},
                               {"\"role\":\"reader\""}))
        << status_json(engine, id);

    ASSERT_EQ(ss_set_wanted_files(engine, id.c_str(), -1, -1), SS_OK);
    EXPECT_TRUE(status_becomes(engine, id, {"\"files\":[]", "\"selected_file\":null"}))
        << status_json(engine, id);

    ASSERT_EQ(ss_set_wanted_files(engine, id.c_str(), 2, 3), SS_OK);
    ASSERT_EQ(ss_select_file(engine, id.c_str(), 3), SS_OK);
    EXPECT_TRUE(status_becomes(engine, id,
                               {"\"index\":3,\"role\":\"reader\"", "\"selected_file\":3"},
                               {"\"role\":\"background\""}))
        << status_json(engine, id);

    ss_engine_destroy(engine);
}

// The start and the end of the next episode's file, fetched while another
// file plays (app BUG-231, DEC-678): said in the status; a torrent or a file
// that is not there, or files not known yet, an error code.
TEST_F(CApiTest, FetchEndsAheadSaysTheFileInTheStatus) {
    auto config = make_config(R"("startup_focus_bytes":65536)");
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(ss_fetch_ends_ahead(nullptr, "abc", 0), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_fetch_ends_ahead(engine, nullptr, 0), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_fetch_ends_ahead(engine, "nonexistent_torrent_id", 0), SS_ERR_NOT_FOUND);

    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);
    std::string id(id_buf);
    ASSERT_TRUE(status_becomes(engine, id, {"\"has_metadata\":true"})) << status_json(engine, id);

    EXPECT_EQ(ss_fetch_ends_ahead(engine, id.c_str(), 99), SS_ERR_NOT_FOUND);
    ASSERT_EQ(ss_select_file(engine, id.c_str(), 2), SS_OK);
    ASSERT_EQ(ss_fetch_ends_ahead(engine, id.c_str(), 3), SS_OK);
    EXPECT_TRUE(status_becomes(engine, id, {"\"ends_ahead_file\":3", "\"ends_ahead_left\":"}))
        << status_json(engine, id);

    // The next episode is the reader's now: nothing comes ahead any more.
    ASSERT_EQ(ss_select_file(engine, id.c_str(), 3), SS_OK);
    EXPECT_TRUE(status_becomes(engine, id, {"\"selected_file\":3"}, {"ends_ahead_file"}))
        << status_json(engine, id);

    ss_engine_destroy(engine);
}

// A torrent or a file that is not there, or files not known yet: an error
// code, and what was wanted stays wanted.
TEST_F(CApiTest, SetWantedFilesThatAreNotThereAreErrors) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(ss_set_wanted_files(engine, "nonexistent_torrent_id", 0, -1), SS_ERR_NOT_FOUND);

    // A magnet nobody answers: its files are never known.
    char bare[128] = {};
    const std::string magnet = "magnet:?xt=urn:btih:" + std::string(40, 'a');
    ASSERT_EQ(ss_add_torrent(engine, magnet.c_str(), bare, sizeof(bare)), SS_OK);
    EXPECT_EQ(ss_set_wanted_files(engine, bare, 0, -1), SS_ERR_METADATA_PENDING);

    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);
    std::string id(id_buf);
    ASSERT_TRUE(status_becomes(engine, id, {"\"has_metadata\":true"})) << status_json(engine, id);
    ASSERT_EQ(ss_set_wanted_files(engine, id.c_str(), 2, 3), SS_OK);

    EXPECT_EQ(ss_set_wanted_files(engine, id.c_str(), 2, 99999), SS_ERR_NOT_FOUND);
    EXPECT_EQ(ss_set_wanted_files(engine, id.c_str(), 99999, 3), SS_ERR_NOT_FOUND);
    EXPECT_EQ(ss_set_wanted_files(engine, id.c_str(), -2, 3), SS_ERR_NOT_FOUND);
    EXPECT_TRUE(status_becomes(engine, id,
                               {"\"index\":2,\"role\":\"reader\"",
                                "\"index\":3,\"role\":\"background\"",
                                "\"selected_file\":2"}))
        << status_json(engine, id);

    ss_engine_destroy(engine);
}

// No call lets an exception out of the library: nothing beyond it (the
// app's JNI, Dart's FFI) can catch one, and the process would end.
// libtorrent throws on a handle whose torrent another thread removed between
// the engine's check and its call (app BUG-199, BUG-200); to the caller that
// torrent is not there. The moment is short: this goes round many times.
TEST_F(CApiTest, CallsOnATorrentBeingRemovedAreErrors) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    char id_buf[128] = {};
    auto path = torrent_path();
    ASSERT_EQ(ss_add_torrent(engine, path.c_str(), id_buf, sizeof(id_buf)), SS_OK);
    const std::string id(id_buf);

    std::atomic<bool> stop{false};
    std::atomic<int> thrown{0};
    std::atomic<int> unexpected{0};
    std::atomic<long> calls{0};
    std::vector<std::thread> callers;
    for (int i = 0; i < 4; ++i) {
        callers.emplace_back([&, i] {
            while (!stop) {
                try {
                    ss_error_t err = SS_OK;
                    switch (i) {
                        case 0: err = ss_pause_torrent(engine, id.c_str()); break;
                        case 1: err = ss_start_torrent(engine, id.c_str()); break;
                        case 2: err = ss_resume_torrent(engine, id.c_str()); break;
                        default: err = ss_force_reannounce(engine, id.c_str()); break;
                    }
                    // There, or not there: nothing else.
                    if (err != SS_OK && err != SS_ERR_NOT_FOUND) ++unexpected;
                } catch (...) {
                    ++thrown;
                }
                ++calls;
                // Often, not flat out: every call is a message to
                // libtorrent's thread, which has the removals to do too.
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        });
    }

    int rounds = 0;
    for (; rounds < 200; ++rounds) {
        if (ss_remove_torrent(engine, id.c_str(), false) != SS_OK) break;
        char again[128] = {};
        if (ss_add_torrent(engine, path.c_str(), again, sizeof(again)) != SS_OK) break;
        if (id != again) break;
    }
    stop = true;
    for (auto& caller : callers) caller.join();

    EXPECT_EQ(rounds, 200);
    EXPECT_GT(calls.load(), 200);
    EXPECT_EQ(thrown.load(), 0);
    EXPECT_EQ(unexpected.load(), 0);

    ss_engine_destroy(engine);
}

// --- Shares (app WORK-37) ---

TEST_F(CApiTest, ShareNullArgs) {
    char* json = nullptr;
    auto* engine = reinterpret_cast<SeekServeEngine*>(0x1);
    EXPECT_EQ(ss_open_share(nullptr, "abc", 0, "127.0.0.1", "127.0.0.1", &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_share(engine, nullptr, 0, "127.0.0.1", "127.0.0.1", &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_share(engine, "abc", 0, nullptr, "127.0.0.1", &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_share(engine, "abc", 0, "127.0.0.1", nullptr, &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_share(engine, "abc", 0, "127.0.0.1", "127.0.0.1", nullptr), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_close_share(nullptr, "abc"), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_close_share(engine, nullptr), SS_ERR_INVALID_ARG);
}

TEST_F(CApiTest, ShareNotFound) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    uint16_t port = 0;
    ASSERT_EQ(ss_start_server(engine, 0, &port), SS_OK);

    char* json = nullptr;
    EXPECT_EQ(ss_open_share(engine, "nonexistent", 0, "127.0.0.1", "127.0.0.1", &json),
              SS_ERR_NOT_FOUND);
    EXPECT_EQ(json, nullptr);
    EXPECT_EQ(ss_close_share(engine, "nonexistent"), SS_ERR_NOT_FOUND);

    ss_engine_destroy(engine);
}

// The share of the fixture's first file, with "read_timeout" from the
// config: a connection that sends no request is closed by then. Offline:
// the proxy is on a closed port, and the share is on 127.0.0.1.
TEST_F(CApiTest, AShareOpensClosesAndTakesTheReadTimeoutKey) {
    auto config = make_config(R"("read_timeout":1,"connection_timeout":0,"stale_reader_timeout":5)");
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    uint16_t port = 0;
    ASSERT_EQ(ss_start_server(engine, 0, &port), SS_OK);
    char id_buf[128] = {};
    ASSERT_EQ(ss_add_torrent(engine, torrent_path().c_str(), id_buf, sizeof(id_buf)), SS_OK);
    ASSERT_EQ(ss_select_file(engine, id_buf, 0), SS_OK);

    char* out = nullptr;
    ASSERT_EQ(ss_open_share(engine, id_buf, 0, "0.0.0.0", "127.0.0.1", &out), SS_ERR_INVALID_ARG);
    ASSERT_EQ(ss_open_share(engine, id_buf, 0, "127.0.0.1", "127.0.0.1", &out), SS_OK);
    ASSERT_NE(out, nullptr);
    // {"share_id":"…","url":"…"}: neither value has a quote in it.
    auto field = [json = std::string(out)](const std::string& key) {
        auto start = json.find("\"" + key + "\":\"");
        if (start == std::string::npos) return std::string();
        start += key.size() + 4;
        return json.substr(start, json.find('"', start) - start);
    };
    auto url = field("url");
    auto share_id = field("share_id");
    ss_free_string(out);
    ASSERT_FALSE(share_id.empty());
    EXPECT_EQ(url.find(id_buf), std::string::npos) << url;
    ASSERT_EQ(url.rfind("http://127.0.0.1:", 0), 0u) << url;
    int share_port = std::stoi(url.substr(std::string("http://127.0.0.1:").size()));
    EXPECT_NE(share_port, port);

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(share_port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    pollfd p{fd, POLLIN, 0};
    ASSERT_EQ(::poll(&p, 1, 4000), 1) << "the share kept a connection without a request";
    char byte;
    EXPECT_EQ(::recv(fd, &byte, 1, 0), 0);
    ::close(fd);

    EXPECT_EQ(ss_close_share(engine, share_id.c_str()), SS_OK);
    EXPECT_EQ(ss_close_share(engine, share_id.c_str()), SS_ERR_NOT_FOUND);
    ss_engine_destroy(engine);
}

TEST_F(CApiTest, FileShareNullArgs) {
    char* json = nullptr;
    auto* engine = reinterpret_cast<SeekServeEngine*>(0x1);
    EXPECT_EQ(ss_open_file_share(nullptr, "/a", "127.0.0.1", "127.0.0.1", &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_file_share(engine, nullptr, "127.0.0.1", "127.0.0.1", &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_file_share(engine, "/a", nullptr, "127.0.0.1", &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_file_share(engine, "/a", "127.0.0.1", nullptr, &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_file_share(engine, "/a", "127.0.0.1", "127.0.0.1", nullptr), SS_ERR_INVALID_ARG);
}

// A file in the save path, with no torrent in the engine: opened, refused
// outside the save path, closed with ss_close_share. Offline: no torrent.
TEST_F(CApiTest, AFileShareOpensAFileOfTheSavePathOnly) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    uint16_t port = 0;
    ASSERT_EQ(ss_start_server(engine, 0, &port), SS_OK);
    auto film = tmp_dir_ + "/film.mkv";
    { std::ofstream(film, std::ios::binary) << std::string(1000, 'f'); }
    auto outside = fs::path(tmp_dir_).parent_path() / "seekserve_capi_outside.mkv";
    { std::ofstream(outside, std::ios::binary) << "x"; }

    char* out = nullptr;
    EXPECT_EQ(ss_open_file_share(engine, outside.c_str(), "127.0.0.1", "127.0.0.1", &out),
              SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_file_share(engine, (tmp_dir_ + "/missing.mkv").c_str(), "127.0.0.1",
                                 "127.0.0.1", &out), SS_ERR_NOT_FOUND);
    ASSERT_EQ(ss_open_file_share(engine, film.c_str(), "127.0.0.1", "127.0.0.1", &out), SS_OK);
    ASSERT_NE(out, nullptr);
    std::string json(out);
    ss_free_string(out);
    EXPECT_EQ(json.find("film"), std::string::npos) << json;
    auto start = json.find("\"share_id\":\"") + 12;
    auto share_id = json.substr(start, json.find('"', start) - start);
    EXPECT_EQ(ss_close_share(engine, share_id.c_str()), SS_OK);

    ss_engine_destroy(engine);
    fs::remove(outside);
}

// Subtitles put into the shared file (app WORK-37 step 7): the lines as JSON,
// checked before anything opens.
TEST_F(CApiTest, FileShareWithSubtitlesRefusesMissingArgumentsAndBadLines) {
    char* json = nullptr;
    auto* engine = reinterpret_cast<SeekServeEngine*>(0x1);
    const char* good = R"({"language":"ita","cues":[{"start_ms":0,"end_ms":1000,"text":"a"}]})";
    EXPECT_EQ(ss_open_file_share_with_subtitles(nullptr, "/a", "127.0.0.1", "127.0.0.1", good, &json),
              SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_file_share_with_subtitles(engine, "/a", "127.0.0.1", "127.0.0.1", nullptr, &json),
              SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_open_share_with_subtitles(engine, "abc", 0, "127.0.0.1", "127.0.0.1", nullptr, &json),
              SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_get_share_subtitles(nullptr, "x", &json), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_get_share_subtitles(engine, nullptr, &json), SS_ERR_INVALID_ARG);

    auto config = make_config();
    SeekServeEngine* real = ss_engine_create(config.c_str());
    ASSERT_NE(real, nullptr);
    uint16_t port = 0;
    ASSERT_EQ(ss_start_server(real, 0, &port), SS_OK);
    auto film = tmp_dir_ + "/film.mkv";
    { std::ofstream(film, std::ios::binary) << std::string(1000, 'f'); }
    for (const char* bad : {"{", "[]", R"({"cues":[]})", R"({"cues":[{"start_ms":0,"end_ms":1000}]})",
                            R"({"cues":[{"start_ms":"0","end_ms":1000,"text":"a"}]})",
                            R"({"cues":[{"start_ms":900,"end_ms":100,"text":"a"}]})",
                            R"({"language":"italiano","cues":[{"start_ms":0,"end_ms":1000,"text":"a"}]})"}) {
        EXPECT_EQ(ss_open_file_share_with_subtitles(real, film.c_str(), "127.0.0.1", "127.0.0.1", bad, &json),
                  SS_ERR_INVALID_ARG) << bad;
    }
    ss_engine_destroy(real);
}

// A file in the save path that is no Matroska: the share opens and, asked,
// tries the file at once: before any request it says that the file is
// served as it is, and why (app WORK-37 step 7d3: the app tells the TV the
// type of the file before the TV asks for it). Offline: no torrent.
TEST_F(CApiTest, AFileShareWithSubtitlesSaysWhereTheyAre) {
    auto config = make_config();
    SeekServeEngine* engine = ss_engine_create(config.c_str());
    ASSERT_NE(engine, nullptr);
    uint16_t port = 0;
    ASSERT_EQ(ss_start_server(engine, 0, &port), SS_OK);
    auto film = tmp_dir_ + "/film.mkv";
    { std::ofstream(film, std::ios::binary) << std::string(1000, 'f'); }

    const char* lines = R"({"language":"ita","name":"Italiano","cues":[{"start_ms":500,"end_ms":2000,"text":"Riga àè"}]})";
    char* out = nullptr;
    ASSERT_EQ(ss_open_file_share_with_subtitles(engine, film.c_str(), "127.0.0.1", "127.0.0.1", lines, &out),
              SS_OK);
    auto json = nlohmann::json::parse(out);
    ss_free_string(out);
    auto share_id = json["share_id"].get<std::string>();
    auto url = json["url"].get<std::string>();

    ASSERT_EQ(ss_get_share_subtitles(engine, share_id.c_str(), &out), SS_OK);
    EXPECT_EQ(nlohmann::json::parse(out)["state"], "unsupported") << "known before any request";
    ss_free_string(out);

    int share_port = std::stoi(url.substr(std::string("http://127.0.0.1:").size()));
    auto path = url.substr(url.find('/', 7));
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(share_port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: x\r\nRange: bytes=0-\r\nConnection: close\r\n\r\n";
    ::send(fd, req.data(), req.size(), 0);
    std::string reply;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) reply.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    EXPECT_NE(reply.find("bytes 0-999/1000"), std::string::npos) << reply.substr(0, 300);

    ASSERT_EQ(ss_get_share_subtitles(engine, share_id.c_str(), &out), SS_OK);
    auto state = nlohmann::json::parse(out);
    ss_free_string(out);
    EXPECT_EQ(state["state"], "unsupported");
    EXPECT_EQ(state["reason"], "neither a Matroska nor an MP4 file");

    ASSERT_EQ(ss_open_file_share(engine, film.c_str(), "127.0.0.1", "127.0.0.1", &out), SS_OK);
    auto plain = nlohmann::json::parse(out)["share_id"].get<std::string>();
    ss_free_string(out);
    EXPECT_EQ(ss_get_share_subtitles(engine, plain.c_str(), &out), SS_ERR_INVALID_ARG);
    EXPECT_EQ(ss_get_share_subtitles(engine, "nobody", &out), SS_ERR_NOT_FOUND);
    ss_engine_destroy(engine);
}

