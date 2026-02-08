#include <gtest/gtest.h>

#include <thread>
#include <chrono>

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/alert_types.hpp>

#include "seekserve/session_manager.hpp"
#include "seekserve/metadata_catalog.hpp"
#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/piece_availability.hpp"
#include "seekserve/types.hpp"
#include "seekserve/error.hpp"

namespace seekserve {
namespace {

static const std::string kSintelTorrent =
    std::string(SEEKSERVE_FIXTURE_DIR) + "/Sintel_archive.torrent";

class SintelIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        SessionConfig config;
        config.save_path = "/tmp/seekserve_test";
        config.listen_port_start = 16881;
        manager_ = std::make_unique<TorrentSessionManager>(config);

        auto result = manager_->add_torrent({kSintelTorrent});
        ASSERT_TRUE(result.ok()) << result.error().message();
        torrent_id_ = result.value();

        handle_ = manager_->get_handle(torrent_id_);
        ASSERT_TRUE(handle_.is_valid());

        auto ti = handle_.torrent_file();
        ASSERT_NE(ti, nullptr);
        catalog_.on_metadata_received(torrent_id_, ti);
    }

    void TearDown() override {
        if (manager_ && !torrent_id_.empty()) {
            manager_->remove_torrent(torrent_id_, true);
        }
        manager_.reset();
    }

    std::unique_ptr<TorrentSessionManager> manager_;
    MetadataCatalog catalog_;
    TorrentId torrent_id_;
    lt::torrent_handle handle_;
};

// --- MetadataCatalog integration tests ---

TEST_F(SintelIntegrationTest, MultifileMetadata) {
    EXPECT_TRUE(catalog_.has_metadata(torrent_id_));

    auto files_result = catalog_.list_files(torrent_id_);
    ASSERT_TRUE(files_result.ok());

    const auto& files = files_result.value();
    EXPECT_GE(files.size(), 10u);

    int mp4_count = 0;
    for (const auto& f : files) {
        if (f.path.size() >= 4 &&
            f.path.substr(f.path.size() - 4) == ".mp4") {
            mp4_count++;
        }
    }
    EXPECT_GE(mp4_count, 3);
}

TEST_F(SintelIntegrationTest, FileSelection) {
    // Initially no selection
    EXPECT_FALSE(catalog_.selected_file(torrent_id_).has_value());

    // Select file 8 (sintel-2048-stereo_512kb.mp4)
    auto select_result = catalog_.select_file(torrent_id_, 8, handle_);
    ASSERT_TRUE(select_result.ok()) << select_result.error().message();

    // Verify selected
    auto selected = catalog_.selected_file(torrent_id_);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value(), 8);

    // prioritize_files() is async; wait for it to be applied
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Verify file priorities on the handle
    auto priorities = handle_.get_file_priorities();
    auto ti = handle_.torrent_file();
    ASSERT_EQ(static_cast<int>(priorities.size()), ti->layout().num_files());

    for (int i = 0; i < static_cast<int>(priorities.size()); ++i) {
        if (i == 8) {
            EXPECT_EQ(priorities[i], lt::default_priority)
                << "Selected file " << i << " should have default priority";
        } else {
            EXPECT_EQ(priorities[i], lt::dont_download)
                << "Non-selected file " << i << " should have dont_download priority";
        }
    }
}

TEST_F(SintelIntegrationTest, SelectInvalidFile) {
    auto result = catalog_.select_file(torrent_id_, 999, handle_);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error(), make_error_code(errc::file_not_found));

    result = catalog_.select_file(torrent_id_, -1, handle_);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error(), make_error_code(errc::file_not_found));
}

// --- ByteRangeMapper with real torrent data ---

TEST_F(SintelIntegrationTest, RangeMappingMultifile) {
    auto file_result = catalog_.get_file(torrent_id_, 8);
    ASSERT_TRUE(file_result.ok());
    auto file_info = file_result.value();

    auto ti = handle_.torrent_file();
    ByteRangeMapper mapper(ti->layout(), 8);

    // Verify consistency with catalog
    EXPECT_EQ(mapper.file_size(), file_info.size);
    EXPECT_EQ(mapper.first_piece(), file_info.first_piece);
    EXPECT_EQ(mapper.end_piece(), file_info.end_piece);

    // Map first byte
    auto span_first = mapper.map(ByteRange{0, 0});
    EXPECT_EQ(span_first.first, file_info.first_piece);

    // Map last byte
    auto span_last = mapper.map(ByteRange{file_info.size - 1, file_info.size - 1});
    EXPECT_EQ(span_last.last, file_info.end_piece - 1);

    // Map 1 MB range at 1 MB offset
    if (file_info.size > 2 * 1024 * 1024) {
        auto span_mid = mapper.map(ByteRange{1024 * 1024, 2 * 1024 * 1024 - 1});
        EXPECT_GE(span_mid.first, file_info.first_piece);
        EXPECT_LE(span_mid.last, file_info.end_piece - 1);
        EXPECT_GT(span_mid.last, span_mid.first);
    }
}

// --- PieceAvailability with real torrent geometry ---

TEST_F(SintelIntegrationTest, PieceAvailabilityWithRealTorrent) {
    auto ti = handle_.torrent_file();
    int num_pieces = ti->num_pieces();
    int piece_length = ti->piece_length();
    int last_piece_size = static_cast<int>(
        ti->total_size() - static_cast<std::int64_t>(num_pieces - 1) * piece_length);

    PieceAvailabilityIndex avail(num_pieces, piece_length, last_piece_size);
    EXPECT_EQ(avail.num_pieces(), num_pieces);
    EXPECT_FLOAT_EQ(avail.progress(), 0.0f);

    // Simulate marking first 3 pieces
    avail.mark_complete(0);
    avail.mark_complete(1);
    avail.mark_complete(2);
    EXPECT_EQ(avail.contiguous_from(0), 3);
    EXPECT_TRUE(avail.is_complete(0));
    EXPECT_TRUE(avail.is_complete(2));
    EXPECT_FALSE(avail.is_complete(3));
}

// --- Alert wiring test ---

TEST_F(SintelIntegrationTest, PieceFinishedAlertWiring) {
    auto ti = handle_.torrent_file();
    int num_pieces = ti->num_pieces();
    int piece_length = ti->piece_length();
    int last_piece_size = static_cast<int>(
        ti->total_size() - static_cast<std::int64_t>(num_pieces - 1) * piece_length);

    PieceAvailabilityIndex avail(num_pieces, piece_length, last_piece_size);

    // Register piece_finished_alert handler
    manager_->alert_dispatcher().on<lt::piece_finished_alert>(
        [&avail](const lt::piece_finished_alert& a) {
            avail.mark_complete(static_cast<PieceIndex>(a.piece_index));
        });

    // We can't easily trigger a real piece_finished_alert without downloading,
    // but we verify the handler registration doesn't crash and the avail index
    // is properly initialized with the real torrent's geometry.
    EXPECT_EQ(avail.num_pieces(), num_pieces);
    EXPECT_GT(num_pieces, 0);
    EXPECT_GT(piece_length, 0);
}

}  // namespace
}  // namespace seekserve
