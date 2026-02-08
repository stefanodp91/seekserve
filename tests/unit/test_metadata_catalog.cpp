#include <gtest/gtest.h>

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/load_torrent.hpp>

#include "seekserve/metadata_catalog.hpp"
#include "seekserve/types.hpp"
#include "seekserve/error.hpp"

namespace seekserve {
namespace {

class MetadataCatalogTest : public ::testing::Test {
protected:
    void SetUp() override {
        lt::error_code ec;
        auto atp = lt::load_torrent_file(
            std::string(SEEKSERVE_FIXTURE_DIR) + "/Sintel_archive.torrent", ec, lt::load_torrent_limits{});
        ASSERT_FALSE(ec) << "Failed to load Sintel torrent: " << ec.message();
        ti_ = atp.ti;
    }

    MetadataCatalog catalog_;
    std::shared_ptr<const lt::torrent_info> ti_;
    const TorrentId kTestId = "e4d37e62d14ba96d29b9e760148803b458aee5b6";
};

TEST_F(MetadataCatalogTest, HasMetadataBeforeAndAfter) {
    EXPECT_FALSE(catalog_.has_metadata(kTestId));
    catalog_.on_metadata_received(kTestId, ti_);
    EXPECT_TRUE(catalog_.has_metadata(kTestId));
}

TEST_F(MetadataCatalogTest, HasMetadataUnknownTorrent) {
    EXPECT_FALSE(catalog_.has_metadata("unknown_torrent_id"));
}

TEST_F(MetadataCatalogTest, ListFilesReturnsAllFiles) {
    catalog_.on_metadata_received(kTestId, ti_);

    auto result = catalog_.list_files(kTestId);
    ASSERT_TRUE(result.ok());

    const auto& files = result.value();
    EXPECT_GE(files.size(), 10u);
    EXPECT_EQ(static_cast<int>(files.size()), ti_->layout().num_files());
}

TEST_F(MetadataCatalogTest, ListFilesNotReady) {
    auto result = catalog_.list_files("nonexistent");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error(), make_error_code(errc::metadata_not_ready));
}

TEST_F(MetadataCatalogTest, FileInfoHasValidData) {
    catalog_.on_metadata_received(kTestId, ti_);

    auto result = catalog_.list_files(kTestId);
    ASSERT_TRUE(result.ok());

    for (const auto& f : result.value()) {
        EXPECT_GE(f.index, 0);
        EXPECT_GE(f.size, 0);
        EXPECT_GE(f.first_piece, 0);
        EXPECT_GE(f.end_piece, f.first_piece);
        EXPECT_FALSE(f.path.empty());
    }
}

TEST_F(MetadataCatalogTest, FileInfoPieceRangesAreConsistent) {
    catalog_.on_metadata_received(kTestId, ti_);
    auto result = catalog_.list_files(kTestId);
    ASSERT_TRUE(result.ok());

    const auto& files = result.value();
    for (const auto& f : files) {
        if (f.size > 0) {
            EXPECT_LT(f.first_piece, f.end_piece)
                << "File " << f.index << " (" << f.path << ") has invalid piece range";
        }
    }
}

TEST_F(MetadataCatalogTest, ContainsMp4Files) {
    catalog_.on_metadata_received(kTestId, ti_);
    auto result = catalog_.list_files(kTestId);
    ASSERT_TRUE(result.ok());

    int mp4_count = 0;
    for (const auto& f : result.value()) {
        if (f.path.size() >= 4 &&
            f.path.substr(f.path.size() - 4) == ".mp4") {
            mp4_count++;
        }
    }
    EXPECT_GE(mp4_count, 3);
}

TEST_F(MetadataCatalogTest, GetFileValidIndex) {
    catalog_.on_metadata_received(kTestId, ti_);

    auto result = catalog_.get_file(kTestId, 8);
    ASSERT_TRUE(result.ok());

    const auto& f = result.value();
    EXPECT_EQ(f.index, 8);
    EXPECT_GT(f.size, 0);
    // Should be the 512kb MP4 (~73.8 MB)
    EXPECT_GT(f.size, 70'000'000);
    EXPECT_LT(f.size, 80'000'000);
}

TEST_F(MetadataCatalogTest, GetFileInvalidIndex) {
    catalog_.on_metadata_received(kTestId, ti_);

    auto result = catalog_.get_file(kTestId, 999);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error(), make_error_code(errc::file_not_found));
}

TEST_F(MetadataCatalogTest, GetFileNegativeIndex) {
    catalog_.on_metadata_received(kTestId, ti_);

    auto result = catalog_.get_file(kTestId, -1);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error(), make_error_code(errc::file_not_found));
}

TEST_F(MetadataCatalogTest, GetFileNoMetadata) {
    auto result = catalog_.get_file("nonexistent", 0);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error(), make_error_code(errc::metadata_not_ready));
}

TEST_F(MetadataCatalogTest, SelectedFileInitiallyEmpty) {
    catalog_.on_metadata_received(kTestId, ti_);
    EXPECT_FALSE(catalog_.selected_file(kTestId).has_value());
}

TEST_F(MetadataCatalogTest, TorrentInfoRetrieval) {
    EXPECT_EQ(catalog_.torrent_info(kTestId), nullptr);
    catalog_.on_metadata_received(kTestId, ti_);
    EXPECT_EQ(catalog_.torrent_info(kTestId), ti_);
}

TEST_F(MetadataCatalogTest, DuplicateMetadataIgnored) {
    catalog_.on_metadata_received(kTestId, ti_);
    auto first_result = catalog_.list_files(kTestId);

    // Second registration should be ignored
    catalog_.on_metadata_received(kTestId, ti_);
    auto second_result = catalog_.list_files(kTestId);

    ASSERT_TRUE(first_result.ok());
    ASSERT_TRUE(second_result.ok());
    EXPECT_EQ(first_result.value().size(), second_result.value().size());
}

TEST_F(MetadataCatalogTest, FileOffsetsAreOrdered) {
    catalog_.on_metadata_received(kTestId, ti_);
    auto result = catalog_.list_files(kTestId);
    ASSERT_TRUE(result.ok());

    const auto& files = result.value();
    for (size_t i = 1; i < files.size(); ++i) {
        EXPECT_GE(files[i].offset_in_torrent, files[i - 1].offset_in_torrent)
            << "File offsets should be non-decreasing";
    }
}

}  // namespace
}  // namespace seekserve
