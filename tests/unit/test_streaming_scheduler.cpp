#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include <cstring>
#include <string>

#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_status.hpp>

#include "seekserve/streaming_scheduler.hpp"
#include "seekserve/piece_availability.hpp"
#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/config.hpp"

using namespace seekserve;

// Shared libtorrent session and torrent handle for all scheduler tests.
// A real session is needed because set_piece_deadline() requires a valid handle.
class StreamingSchedulerTest : public ::testing::Test {
protected:
    static constexpr int kPieceLength = 256 * 1024;               // 256 KB
    static constexpr std::int64_t kFileSize = 10 * 1024 * 1024;   // 10 MB → 40 pieces

    static std::shared_ptr<const lt::torrent_info> s_ti;
    static std::unique_ptr<lt::session> s_session;
    static lt::torrent_handle s_handle;

    static void SetUpTestSuite() {
        std::vector<lt::create_file_entry> files;
        files.emplace_back("test.mp4", kFileSize);

        lt::create_torrent ct(std::move(files), kPieceLength, lt::create_torrent::v1_only);
        int np = ct.num_pieces();
        lt::sha1_hash dummy;
        std::memset(dummy.data(), 0x42, dummy.size());
        for (int i = 0; i < np; ++i) {
            ct.set_hash(lt::piece_index_t{i}, dummy);
        }

        auto buf = ct.generate_buf();
        auto atp = lt::load_torrent_buffer(buf);
        s_ti = atp.ti;
        ASSERT_TRUE(s_ti) << "Failed to create torrent_info";

        lt::settings_pack sp;
        sp.set_int(lt::settings_pack::alert_mask, 0);
        sp.set_bool(lt::settings_pack::enable_dht, false);
        sp.set_bool(lt::settings_pack::enable_lsd, false);
        sp.set_bool(lt::settings_pack::enable_natpmp, false);
        sp.set_bool(lt::settings_pack::enable_upnp, false);
        s_session = std::make_unique<lt::session>(sp);

        atp.save_path = "/tmp/seekserve_test";
        s_handle = s_session->add_torrent(atp);
    }

    static void TearDownTestSuite() {
        if (s_session) {
            s_session->remove_torrent(s_handle);
            s_session.reset();
        }
        s_ti.reset();
    }

    void SetUp() override {
        int np = s_ti->num_pieces();
        int last = static_cast<int>(
            kFileSize - static_cast<std::int64_t>(np - 1) * kPieceLength);
        avail_ = std::make_unique<PieceAvailabilityIndex>(np, kPieceLength, last);
        mapper_ = std::make_unique<ByteRangeMapper>(s_ti->layout(), 0);
    }

    StreamingScheduler make_scheduler(SchedulerConfig cfg = {}) {
        return StreamingScheduler(cfg, *avail_, *mapper_);
    }

    static PieceIndex piece_for_byte(std::int64_t byte_offset) {
        return static_cast<PieceIndex>(byte_offset / kPieceLength);
    }

    std::unique_ptr<PieceAvailabilityIndex> avail_;
    std::unique_ptr<ByteRangeMapper> mapper_;
};

std::shared_ptr<const lt::torrent_info> StreamingSchedulerTest::s_ti;
std::unique_ptr<lt::session> StreamingSchedulerTest::s_session;
lt::torrent_handle StreamingSchedulerTest::s_handle;

// ─── Initial state ──────────────────────────────────────────────────────────

TEST_F(StreamingSchedulerTest, InitialState) {
    auto sched = make_scheduler();

    EXPECT_EQ(sched.current_mode(), StreamMode::StreamingFirst);
    EXPECT_EQ(sched.playhead_piece(), mapper_->first_piece());
    EXPECT_EQ(sched.active_deadlines(), 0);
    EXPECT_FALSE(sched.is_seek_boosting());
    EXPECT_EQ(sched.stall_count(), 0);
}

// ─── Playhead tracking ─────────────────────────────────────────────────────

TEST_F(StreamingSchedulerTest, PlayheadTracksRangeRequest) {
    auto sched = make_scheduler();

    // Request at byte 1 MB → piece 4
    ByteRange range{1024 * 1024, 1024 * 1024 + 1000};
    sched.on_range_request(range, s_handle);

    EXPECT_EQ(sched.playhead_piece(), piece_for_byte(1024 * 1024));
}

TEST_F(StreamingSchedulerTest, PlayheadUpdatesOnSubsequentRequests) {
    auto sched = make_scheduler();

    sched.on_range_request(ByteRange{0, 1000}, s_handle);
    EXPECT_EQ(sched.playhead_piece(), 0);

    // Move to byte 512 KB → piece 2
    sched.on_range_request(ByteRange{512 * 1024, 512 * 1024 + 1000}, s_handle);
    EXPECT_EQ(sched.playhead_piece(), 2);
}

// ─── Seek detection ─────────────────────────────────────────────────────────

TEST_F(StreamingSchedulerTest, SmallMoveNoSeekBoost) {
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 5;
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);
    EXPECT_FALSE(sched.is_seek_boosting());

    // Move forward by 2 pieces (< hot_window=5) → no seek
    std::int64_t offset = 2LL * kPieceLength;
    sched.on_range_request(ByteRange{offset, offset + 100}, s_handle);
    EXPECT_FALSE(sched.is_seek_boosting());
}

TEST_F(StreamingSchedulerTest, LargeForwardJumpTriggersSeekBoost) {
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 5;
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);

    // Jump forward by 20 pieces (> hot_window=5) → seek boost
    std::int64_t offset = 20LL * kPieceLength;
    sched.on_range_request(ByteRange{offset, offset + 100}, s_handle);
    EXPECT_TRUE(sched.is_seek_boosting());
}

TEST_F(StreamingSchedulerTest, BackwardSeekTriggersBoost) {
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 5;
    auto sched = make_scheduler(cfg);

    // Start at piece 20
    std::int64_t start = 20LL * kPieceLength;
    sched.on_range_request(ByteRange{start, start + 100}, s_handle);

    // Jump back to piece 0 (distance 20 > 5) → seek boost
    sched.on_range_request(ByteRange{0, 100}, s_handle);
    EXPECT_TRUE(sched.is_seek_boosting());
}

TEST_F(StreamingSchedulerTest, SeekBoostExpires) {
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 5;
    cfg.seek_boost_duration_ms = 10;  // very short for testing
    auto sched = make_scheduler(cfg);

    // Trigger seek boost
    sched.on_range_request(ByteRange{0, 100}, s_handle);
    std::int64_t far = 20LL * kPieceLength;
    sched.on_range_request(ByteRange{far, far + 100}, s_handle);
    EXPECT_TRUE(sched.is_seek_boosting());

    // Wait for boost to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    lt::torrent_status status{};
    status.download_rate = 1'000'000;
    sched.tick(s_handle, status);

    EXPECT_FALSE(sched.is_seek_boosting());
}

// ─── Deadlines ──────────────────────────────────────────────────────────────

TEST_F(StreamingSchedulerTest, OnRangeRequestSetsDeadlines) {
    auto sched = make_scheduler();

    sched.on_range_request(ByteRange{0, 100}, s_handle);
    EXPECT_GT(sched.active_deadlines(), 0);
}

TEST_F(StreamingSchedulerTest, DeadlinesRespectBudget) {
    SchedulerConfig cfg;
    cfg.deadline_budget = 5;
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);
    EXPECT_LE(sched.active_deadlines(), cfg.deadline_budget);
}

TEST_F(StreamingSchedulerTest, NoPendingDeadlinesWhenAllComplete) {
    auto sched = make_scheduler();

    int np = s_ti->num_pieces();
    for (int i = 0; i < np; ++i) {
        avail_->mark_complete(i);
    }

    sched.on_range_request(ByteRange{0, 100}, s_handle);
    EXPECT_EQ(sched.active_deadlines(), 0);
}

TEST_F(StreamingSchedulerTest, OnPieceCompleteDecrementsActiveDeadlines) {
    auto sched = make_scheduler();

    sched.on_range_request(ByteRange{0, 100}, s_handle);
    int before = sched.active_deadlines();
    ASSERT_GT(before, 0);

    PieceIndex playhead = sched.playhead_piece();
    sched.on_piece_complete(playhead);

    EXPECT_EQ(sched.active_deadlines(), before - 1);
}

TEST_F(StreamingSchedulerTest, OnPieceCompleteBeforePlayheadNoEffect) {
    auto sched = make_scheduler();

    // Move playhead to piece 10
    std::int64_t offset = 10LL * kPieceLength;
    sched.on_range_request(ByteRange{offset, offset + 100}, s_handle);
    int deadlines = sched.active_deadlines();

    // Complete a piece before playhead → no change
    sched.on_piece_complete(0);
    EXPECT_EQ(sched.active_deadlines(), deadlines);
}

TEST_F(StreamingSchedulerTest, DeadlinesNearEndOfFile) {
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 10;
    cfg.lookahead_pieces = 20;
    auto sched = make_scheduler(cfg);

    // Place playhead at last 2 pieces
    int np = s_ti->num_pieces();
    std::int64_t offset = static_cast<std::int64_t>(np - 2) * kPieceLength;
    sched.on_range_request(ByteRange{offset, offset + 100}, s_handle);

    // Deadlines can't exceed remaining incomplete pieces
    EXPECT_LE(sched.active_deadlines(), 2);
}

// ─── Stall detection ────────────────────────────────────────────────────────

TEST_F(StreamingSchedulerTest, StallIncreasesWhenPlayheadIncomplete) {
    auto sched = make_scheduler();
    sched.on_range_request(ByteRange{0, 100}, s_handle);

    lt::torrent_status status{};
    status.download_rate = 1'000'000;

    sched.tick(s_handle, status);
    EXPECT_EQ(sched.stall_count(), 1);

    sched.tick(s_handle, status);
    EXPECT_EQ(sched.stall_count(), 2);
}

TEST_F(StreamingSchedulerTest, StallResetsWhenPlayheadComplete) {
    auto sched = make_scheduler();
    sched.on_range_request(ByteRange{0, 100}, s_handle);

    lt::torrent_status status{};
    status.download_rate = 1'000'000;

    sched.tick(s_handle, status);
    sched.tick(s_handle, status);
    EXPECT_EQ(sched.stall_count(), 2);

    avail_->mark_complete(sched.playhead_piece());
    sched.tick(s_handle, status);
    EXPECT_EQ(sched.stall_count(), 0);
}

// ─── Mode switching ─────────────────────────────────────────────────────────

TEST_F(StreamingSchedulerTest, StreamingFirstToDownloadAssist) {
    SchedulerConfig cfg;
    cfg.stall_count_threshold = 3;
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);
    EXPECT_EQ(sched.current_mode(), StreamMode::StreamingFirst);

    lt::torrent_status status{};
    status.download_rate = 1'000'000;

    for (int i = 0; i < cfg.stall_count_threshold; ++i) {
        sched.tick(s_handle, status);
    }

    EXPECT_EQ(sched.current_mode(), StreamMode::DownloadAssist);
}

TEST_F(StreamingSchedulerTest, DownloadAssistToDownloadFirst) {
    SchedulerConfig cfg;
    cfg.stall_count_threshold = 2;
    cfg.min_sustained_rate = 500'000;
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);

    lt::torrent_status status{};
    status.download_rate = 100'000;  // below min_sustained_rate

    // Reach DownloadAssist
    for (int i = 0; i < cfg.stall_count_threshold; ++i) {
        sched.tick(s_handle, status);
    }
    ASSERT_EQ(sched.current_mode(), StreamMode::DownloadAssist);

    // Mode switch resets stall_count. Need 2× threshold more ticks.
    for (int i = 0; i < cfg.stall_count_threshold * 2; ++i) {
        sched.tick(s_handle, status);
    }

    EXPECT_EQ(sched.current_mode(), StreamMode::DownloadFirst);
}

TEST_F(StreamingSchedulerTest, DownloadAssistBackToStreamingFirst) {
    SchedulerConfig cfg;
    cfg.stall_count_threshold = 2;
    cfg.min_contiguous_bytes = 2 * kPieceLength;  // 2 pieces worth
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);

    lt::torrent_status status{};
    status.download_rate = 1'000'000;

    // Get to DownloadAssist
    for (int i = 0; i < cfg.stall_count_threshold; ++i) {
        sched.tick(s_handle, status);
    }
    ASSERT_EQ(sched.current_mode(), StreamMode::DownloadAssist);

    // Mark contiguous pieces from playhead to satisfy min_contiguous_bytes
    PieceIndex playhead = sched.playhead_piece();
    int needed = static_cast<int>(cfg.min_contiguous_bytes / kPieceLength) + 1;
    for (int i = 0; i < needed; ++i) {
        avail_->mark_complete(playhead + i);
    }

    // Tick: playhead complete → stall resets, contiguous >= threshold → StreamingFirst
    sched.tick(s_handle, status);
    EXPECT_EQ(sched.current_mode(), StreamMode::StreamingFirst);
}

TEST_F(StreamingSchedulerTest, DownloadFirstToDownloadAssist) {
    SchedulerConfig cfg;
    cfg.stall_count_threshold = 2;
    cfg.min_sustained_rate = 500'000;
    cfg.min_contiguous_bytes = 2 * kPieceLength;
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);

    lt::torrent_status status{};
    status.download_rate = 100'000;  // low rate

    // StreamingFirst → DownloadAssist
    for (int i = 0; i < cfg.stall_count_threshold; ++i) {
        sched.tick(s_handle, status);
    }
    ASSERT_EQ(sched.current_mode(), StreamMode::DownloadAssist);

    // DownloadAssist → DownloadFirst
    for (int i = 0; i < cfg.stall_count_threshold * 2; ++i) {
        sched.tick(s_handle, status);
    }
    ASSERT_EQ(sched.current_mode(), StreamMode::DownloadFirst);

    // Mark contiguous pieces to satisfy threshold
    PieceIndex playhead = sched.playhead_piece();
    int needed = static_cast<int>(cfg.min_contiguous_bytes / kPieceLength) + 1;
    for (int i = 0; i < needed; ++i) {
        avail_->mark_complete(playhead + i);
    }

    sched.tick(s_handle, status);
    EXPECT_EQ(sched.current_mode(), StreamMode::DownloadAssist);
}

// ─── Edge cases ─────────────────────────────────────────────────────────────

TEST_F(StreamingSchedulerTest, MultipleSeeksResetBoost) {
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 5;
    cfg.seek_boost_duration_ms = 5000;  // long so it doesn't expire
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);
    std::int64_t far1 = 20LL * kPieceLength;
    sched.on_range_request(ByteRange{far1, far1 + 100}, s_handle);
    EXPECT_TRUE(sched.is_seek_boosting());
    EXPECT_EQ(sched.playhead_piece(), piece_for_byte(far1));

    // Second seek back to beginning — still boosting
    sched.on_range_request(ByteRange{0, 100}, s_handle);
    EXPECT_TRUE(sched.is_seek_boosting());
    EXPECT_EQ(sched.playhead_piece(), 0);
}

TEST_F(StreamingSchedulerTest, ModeSwitchResetsStallCount) {
    SchedulerConfig cfg;
    cfg.stall_count_threshold = 3;
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);

    lt::torrent_status status{};
    status.download_rate = 1'000'000;

    // Build stalls to trigger mode switch
    for (int i = 0; i < cfg.stall_count_threshold; ++i) {
        sched.tick(s_handle, status);
    }
    ASSERT_EQ(sched.current_mode(), StreamMode::DownloadAssist);

    // Stall count should have been reset on mode switch
    // The tick that caused the switch incremented stall first, then switched and reset.
    // Next tick: stall starts fresh at 0, then increments to 1.
    sched.tick(s_handle, status);
    EXPECT_EQ(sched.stall_count(), 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Sintel fixture tests — real torrent, file 8 (sintel-2048-stereo_512kb.mp4)
// ═══════════════════════════════════════════════════════════════════════════

class StreamingSchedulerSintelTest : public ::testing::Test {
protected:
    static constexpr FileIndex kFileIdx = 8;
    static constexpr std::int64_t kExpectedFileSize = 77410288;   // 73.8 MB
    static constexpr PieceIndex kExpectedFirstPiece = 1253;
    static constexpr PieceIndex kExpectedEndPiece = 1328;         // 75 pieces

    static std::shared_ptr<const lt::torrent_info> s_ti;
    static std::unique_ptr<lt::session> s_session;
    static lt::torrent_handle s_handle;

    static void SetUpTestSuite() {
        std::string torrent_path = std::string(SEEKSERVE_FIXTURE_DIR)
                                   + "/Sintel_archive.torrent";
        auto atp = lt::load_torrent_file(torrent_path);
        s_ti = atp.ti;
        ASSERT_TRUE(s_ti) << "Failed to load " << torrent_path;

        lt::settings_pack sp;
        sp.set_int(lt::settings_pack::alert_mask, 0);
        sp.set_bool(lt::settings_pack::enable_dht, false);
        sp.set_bool(lt::settings_pack::enable_lsd, false);
        sp.set_bool(lt::settings_pack::enable_natpmp, false);
        sp.set_bool(lt::settings_pack::enable_upnp, false);
        s_session = std::make_unique<lt::session>(sp);

        atp.save_path = "/tmp/seekserve_test_sintel";
        s_handle = s_session->add_torrent(atp);
    }

    static void TearDownTestSuite() {
        if (s_session) {
            s_session->remove_torrent(s_handle);
            s_session.reset();
        }
        s_ti.reset();
    }

    void SetUp() override {
        int np = s_ti->num_pieces();
        int pl = s_ti->piece_length();
        int last = static_cast<int>(
            s_ti->total_size() - static_cast<std::int64_t>(np - 1) * pl);
        avail_ = std::make_unique<PieceAvailabilityIndex>(np, pl, last);
        mapper_ = std::make_unique<ByteRangeMapper>(s_ti->layout(), kFileIdx);
    }

    StreamingScheduler make_scheduler(SchedulerConfig cfg = {}) {
        return StreamingScheduler(cfg, *avail_, *mapper_);
    }

    std::unique_ptr<PieceAvailabilityIndex> avail_;
    std::unique_ptr<ByteRangeMapper> mapper_;
};

std::shared_ptr<const lt::torrent_info> StreamingSchedulerSintelTest::s_ti;
std::unique_ptr<lt::session> StreamingSchedulerSintelTest::s_session;
lt::torrent_handle StreamingSchedulerSintelTest::s_handle;

TEST_F(StreamingSchedulerSintelTest, FixtureSanity) {
    // Verify the torrent fixture matches expectations
    EXPECT_EQ(mapper_->file_size(), kExpectedFileSize);
    EXPECT_EQ(mapper_->first_piece(), kExpectedFirstPiece);
    EXPECT_EQ(mapper_->end_piece(), kExpectedEndPiece);
}

TEST_F(StreamingSchedulerSintelTest, InitialPlayheadAtFileStart) {
    auto sched = make_scheduler();

    // Playhead must start at the first piece of file 8, not piece 0
    EXPECT_EQ(sched.playhead_piece(), kExpectedFirstPiece);
    EXPECT_EQ(sched.current_mode(), StreamMode::StreamingFirst);
}

TEST_F(StreamingSchedulerSintelTest, PlayheadMapsToCorrectPiece) {
    auto sched = make_scheduler();

    // Request at byte 1 MB into the file
    ByteRange range{1024 * 1024, 1024 * 1024 + 1000};
    sched.on_range_request(range, s_handle);

    // Playhead should be within the file's piece range
    PieceIndex ph = sched.playhead_piece();
    EXPECT_GE(ph, kExpectedFirstPiece);
    EXPECT_LT(ph, kExpectedEndPiece);
}

TEST_F(StreamingSchedulerSintelTest, SeekAcrossFileTriggersBoost) {
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 5;
    auto sched = make_scheduler(cfg);

    // Start at beginning of file
    sched.on_range_request(ByteRange{0, 100}, s_handle);
    EXPECT_FALSE(sched.is_seek_boosting());

    // Seek to ~50 MB into the file (well beyond hot window)
    std::int64_t far = 50LL * 1024 * 1024;
    sched.on_range_request(ByteRange{far, far + 100}, s_handle);
    EXPECT_TRUE(sched.is_seek_boosting());

    PieceIndex ph = sched.playhead_piece();
    EXPECT_GE(ph, kExpectedFirstPiece);
    EXPECT_LT(ph, kExpectedEndPiece);
}

TEST_F(StreamingSchedulerSintelTest, DeadlinesStayWithinFilePieceRange) {
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 5;
    cfg.lookahead_pieces = 20;
    cfg.deadline_budget = 30;
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);

    // With 75 pieces in file and budget=30, we should get up to 25 deadlines
    // (hot=5 + lookahead=20)
    int d = sched.active_deadlines();
    EXPECT_GT(d, 0);
    EXPECT_LE(d, cfg.deadline_budget);
}

TEST_F(StreamingSchedulerSintelTest, NearEndOfFileClampsDeadlines) {
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 10;
    cfg.lookahead_pieces = 50;
    cfg.deadline_budget = 60;
    auto sched = make_scheduler(cfg);

    // Seek close to end of file — only a few pieces left
    std::int64_t near_end = kExpectedFileSize - 500'000;  // ~500 KB from end
    sched.on_range_request(ByteRange{near_end, near_end + 100}, s_handle);

    // Should have at most 1-2 deadlines (near end of file)
    EXPECT_LE(sched.active_deadlines(), 3);
}

TEST_F(StreamingSchedulerSintelTest, AllFilePiecesCompleteNoDeadlines) {
    auto sched = make_scheduler();

    // Mark only file 8's pieces complete (not all torrent pieces)
    for (PieceIndex p = kExpectedFirstPiece; p < kExpectedEndPiece; ++p) {
        avail_->mark_complete(p);
    }

    sched.on_range_request(ByteRange{0, 100}, s_handle);
    EXPECT_EQ(sched.active_deadlines(), 0);
}

TEST_F(StreamingSchedulerSintelTest, ModeTransitionWithContiguousBuffer) {
    SchedulerConfig cfg;
    cfg.stall_count_threshold = 2;
    // 10 pieces (~10 MB) of contiguous buffer to switch back
    cfg.min_contiguous_bytes = 10LL * s_ti->piece_length();
    auto sched = make_scheduler(cfg);

    sched.on_range_request(ByteRange{0, 100}, s_handle);

    lt::torrent_status status{};
    status.download_rate = 1'000'000;

    // Drive to DownloadAssist
    for (int i = 0; i < cfg.stall_count_threshold; ++i) {
        sched.tick(s_handle, status);
    }
    ASSERT_EQ(sched.current_mode(), StreamMode::DownloadAssist);

    // Mark 12 contiguous pieces from playhead (more than 10 needed)
    PieceIndex ph = sched.playhead_piece();
    for (int i = 0; i < 12; ++i) {
        avail_->mark_complete(ph + i);
    }

    // Tick: playhead complete → stall=0, contiguous >= threshold → back to StreamingFirst
    sched.tick(s_handle, status);
    EXPECT_EQ(sched.current_mode(), StreamMode::StreamingFirst);
}

TEST_F(StreamingSchedulerSintelTest, SimulateVlcAccessPattern) {
    // VLC typical pattern: request byte 0, then EOF probe, then seek to moov atom
    SchedulerConfig cfg;
    cfg.hot_window_pieces = 5;
    auto sched = make_scheduler(cfg);

    // 1) Initial request at byte 0
    sched.on_range_request(ByteRange{0, 65535}, s_handle);
    EXPECT_EQ(sched.playhead_piece(), kExpectedFirstPiece);
    EXPECT_FALSE(sched.is_seek_boosting());

    // 2) EOF probe — last byte of file
    sched.on_range_request(ByteRange{kExpectedFileSize - 1, kExpectedFileSize - 1}, s_handle);
    // Jump from first to last piece → seek boost
    EXPECT_TRUE(sched.is_seek_boosting());
    EXPECT_EQ(sched.playhead_piece(), kExpectedEndPiece - 1);

    // 3) Seek to moov atom (~505 KB into file, similar to real VLC behavior)
    sched.on_range_request(ByteRange{505725, 505725 + 65535}, s_handle);
    // This is back near the beginning, another large jump → boost stays on
    EXPECT_TRUE(sched.is_seek_boosting());

    PieceIndex ph = sched.playhead_piece();
    EXPECT_GE(ph, kExpectedFirstPiece);
    EXPECT_LT(ph, kExpectedEndPiece);
    EXPECT_GT(sched.active_deadlines(), 0);
}
