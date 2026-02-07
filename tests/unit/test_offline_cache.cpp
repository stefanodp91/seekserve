#include <gtest/gtest.h>
#include <filesystem>
#include <thread>
#include <chrono>

#include "seekserve/offline_cache.hpp"
#include "seekserve/types.hpp"
#include "seekserve/config.hpp"

namespace fs = std::filesystem;

class OfflineCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = "/tmp/seekserve_test_cache_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".db";
    }

    void TearDown() override {
        fs::remove(db_path_);
        fs::remove(db_path_ + "-wal");
        fs::remove(db_path_ + "-shm");
    }

    seekserve::CacheConfig make_config() {
        seekserve::CacheConfig cfg;
        cfg.db_path = db_path_;
        return cfg;
    }

    std::vector<seekserve::FileInfo> make_test_files() {
        return {
            {0, "Sintel/sintel-2048-stereo.mp4", 300'000'000, 0, 0, 600},
            {1, "Sintel/sintel-2048-stereo_512kb.mp4", 77'410'288, 300'000'000, 600, 750},
            {2, "Sintel/poster.jpg", 50'000, 380'000'000, 750, 751},
        };
    }

    std::string db_path_;
};

TEST_F(OfflineCacheTest, InitCreatesDatabase) {
    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    EXPECT_TRUE(fs::exists(db_path_));
}

TEST_F(OfflineCacheTest, OnTorrentAddedInsertsEntries) {
    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    auto files = make_test_files();
    cache.on_torrent_added("abc123", files);

    auto entries = cache.list_cached();
    ASSERT_EQ(entries.size(), 3u);

    bool found_mp4 = false, found_512kb = false, found_poster = false;
    for (const auto& e : entries) {
        EXPECT_EQ(e.torrent_id, "abc123");
        EXPECT_FLOAT_EQ(e.progress, 0.0f);
        EXPECT_FALSE(e.offline_ready);

        if (e.file_index == 0) found_mp4 = true;
        if (e.file_index == 1) found_512kb = true;
        if (e.file_index == 2) found_poster = true;
    }
    EXPECT_TRUE(found_mp4);
    EXPECT_TRUE(found_512kb);
    EXPECT_TRUE(found_poster);
}

TEST_F(OfflineCacheTest, OnTorrentAddedIdempotent) {
    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    auto files = make_test_files();
    cache.on_torrent_added("abc123", files);
    cache.on_torrent_added("abc123", files);  // duplicate call

    auto entries = cache.list_cached();
    EXPECT_EQ(entries.size(), 3u);
}

TEST_F(OfflineCacheTest, OnProgressUpdate) {
    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    auto files = make_test_files();
    cache.on_torrent_added("abc123", files);

    cache.on_progress_update("abc123", 1, 0.5f);

    auto entries = cache.list_cached();
    for (const auto& e : entries) {
        if (e.file_index == 1) {
            EXPECT_NEAR(e.progress, 0.5f, 0.01f);
        } else {
            EXPECT_FLOAT_EQ(e.progress, 0.0f);
        }
    }
}

TEST_F(OfflineCacheTest, OnFileCompleted) {
    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    auto files = make_test_files();
    cache.on_torrent_added("abc123", files);

    EXPECT_FALSE(cache.is_offline_ready("abc123", 1));

    cache.on_file_completed("abc123", 1);

    EXPECT_TRUE(cache.is_offline_ready("abc123", 1));
    EXPECT_FALSE(cache.is_offline_ready("abc123", 0));

    auto entries = cache.list_cached();
    for (const auto& e : entries) {
        if (e.file_index == 1) {
            EXPECT_FLOAT_EQ(e.progress, 1.0f);
            EXPECT_TRUE(e.offline_ready);
        }
    }
}

TEST_F(OfflineCacheTest, IsOfflineReadyNonexistent) {
    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    EXPECT_FALSE(cache.is_offline_ready("nonexistent", 0));
}

TEST_F(OfflineCacheTest, OnAccessUpdatesTimestamp) {
    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    auto files = make_test_files();
    cache.on_torrent_added("abc123", files);

    // Sleep to get a different epoch second
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    cache.on_access("abc123", 2);

    auto entries = cache.list_cached();
    // Entries ordered by last_access DESC; file 2 (just accessed) should be first
    ASSERT_GE(entries.size(), 1u);
    EXPECT_EQ(entries[0].file_index, 2);
}

TEST_F(OfflineCacheTest, ListCachedReturnsAllFields) {
    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    auto files = make_test_files();
    cache.on_torrent_added("abc123", files);

    auto entries = cache.list_cached();
    ASSERT_EQ(entries.size(), 3u);

    for (const auto& e : entries) {
        EXPECT_EQ(e.torrent_id, "abc123");
        EXPECT_FALSE(e.file_path.empty());
        EXPECT_GT(e.file_size, 0);
        EXPECT_GT(e.last_access.time_since_epoch().count(), 0);
        EXPECT_GT(e.added.time_since_epoch().count(), 0);
    }
}

TEST_F(OfflineCacheTest, MultipleTorrents) {
    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    auto files1 = make_test_files();
    cache.on_torrent_added("torrent1", files1);

    std::vector<seekserve::FileInfo> files2 = {
        {0, "Other/video.mp4", 100'000'000, 0, 0, 200},
    };
    cache.on_torrent_added("torrent2", files2);

    auto entries = cache.list_cached();
    EXPECT_EQ(entries.size(), 4u);

    cache.on_file_completed("torrent2", 0);
    EXPECT_TRUE(cache.is_offline_ready("torrent2", 0));
    EXPECT_FALSE(cache.is_offline_ready("torrent1", 0));
}

TEST_F(OfflineCacheTest, EnforceQuotaUnlimited) {
    auto cfg = make_config();
    cfg.max_storage_bytes = 0;  // unlimited
    seekserve::OfflineCacheManager cache(cfg);

    auto files = make_test_files();
    cache.on_torrent_added("abc123", files);

    cache.enforce_quota();

    auto entries = cache.list_cached();
    EXPECT_EQ(entries.size(), 3u);  // nothing evicted
}

TEST_F(OfflineCacheTest, EnforceQuotaEvictsLRU) {
    auto cfg = make_config();
    cfg.max_storage_bytes = 100'000'000;  // 100 MB
    seekserve::OfflineCacheManager cache(cfg);

    auto files = make_test_files();
    cache.on_torrent_added("abc123", files);

    // Access file 1 to make it more recent
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    cache.on_access("abc123", 1);

    cache.enforce_quota();

    auto entries = cache.list_cached();
    // Total was ~377MB, quota is 100MB. File 0 (300MB, oldest) should be evicted.
    // After evicting file 0: remaining = 77MB + 50KB < 100MB.
    EXPECT_LT(entries.size(), 3u);

    // File 1 (most recently accessed) should survive
    bool found_file1 = false;
    for (const auto& e : entries) {
        if (e.file_index == 1) found_file1 = true;
    }
    EXPECT_TRUE(found_file1);
}

TEST_F(OfflineCacheTest, PersistenceAcrossInstances) {
    auto cfg = make_config();

    {
        seekserve::OfflineCacheManager cache(cfg);
        auto files = make_test_files();
        cache.on_torrent_added("abc123", files);
        cache.on_file_completed("abc123", 1);
    }

    // Reopen database
    {
        seekserve::OfflineCacheManager cache(cfg);
        EXPECT_TRUE(cache.is_offline_ready("abc123", 1));
        EXPECT_FALSE(cache.is_offline_ready("abc123", 0));

        auto entries = cache.list_cached();
        EXPECT_EQ(entries.size(), 3u);
    }
}

// --- Sintel fixture test ---
TEST_F(OfflineCacheTest, SintelTorrentFiles) {
    std::vector<seekserve::FileInfo> sintel_files = {
        {0, "Sintel/Sintel.de.srt", 1652, 0, 0, 1},
        {1, "Sintel/Sintel.en.srt", 1514, 0, 0, 1},
        {2, "Sintel/Sintel.es.srt", 1554, 0, 0, 1},
        {3, "Sintel/Sintel.fr.srt", 1618, 0, 0, 1},
        {4, "Sintel/Sintel.it.srt", 1546, 0, 0, 1},
        {5, "Sintel/Sintel.nl.srt", 1537, 0, 0, 1},
        {6, "Sintel/Sintel.pl.srt", 1525, 0, 0, 1},
        {7, "Sintel/Sintel.pt.srt", 1504, 0, 0, 1},
        {8, "Sintel/sintel-2048-stereo_512kb.mp4", 77410288, 0, 1253, 1328},
        {9, "Sintel/sintel-2048-stereo.mp4", 311430449, 0, 0, 600},
        {10, "Sintel/sintel-2048-surround.mp4", 311587641, 0, 600, 1200},
        {11, "Sintel/sintel-2048-surround_512kb.mp4", 77416539, 0, 1328, 1403},
    };

    auto cfg = make_config();
    seekserve::OfflineCacheManager cache(cfg);

    cache.on_torrent_added("e4d37e62d14ba96d29b9e760148803b458aee5b6", sintel_files);

    auto entries = cache.list_cached();
    EXPECT_EQ(entries.size(), 12u);

    // Mark test target as completed
    cache.on_file_completed("e4d37e62d14ba96d29b9e760148803b458aee5b6", 8);
    EXPECT_TRUE(cache.is_offline_ready("e4d37e62d14ba96d29b9e760148803b458aee5b6", 8));

    // Other files not ready
    EXPECT_FALSE(cache.is_offline_ready("e4d37e62d14ba96d29b9e760148803b458aee5b6", 9));

    // Progress update
    cache.on_progress_update("e4d37e62d14ba96d29b9e760148803b458aee5b6", 9, 0.3f);
    entries = cache.list_cached();
    for (const auto& e : entries) {
        if (e.file_index == 9) {
            EXPECT_NEAR(e.progress, 0.3f, 0.01f);
        }
    }
}
