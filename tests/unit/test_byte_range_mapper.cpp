#include <gtest/gtest.h>

#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/load_torrent.hpp>

#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/types.hpp"

namespace seekserve {
namespace {

// Multi-file test torrent layout (piece_length = 1000):
//   file 0: "test/video.mp4"  2500 bytes  offset [0, 2499]
//   file 1: "test/subs.srt"    300 bytes  offset [2500, 2799]
//   file 2: "test/extra.mp4"  1700 bytes  offset [2800, 4499]
//   Total: 4500 bytes, 5 pieces (last piece 500 bytes)
//
// Piece layout:
//   piece 0: [0, 999]     → all file 0
//   piece 1: [1000, 1999] → all file 0
//   piece 2: [2000, 2999] → file 0 [2000-2499], file 1 [2500-2799], file 2 [2800-2999]
//   piece 3: [3000, 3999] → all file 2
//   piece 4: [4000, 4499] → file 2 (last piece, 500 bytes)

static lt::file_storage make_test_fs() {
    lt::file_storage fs;
    fs.set_piece_length(1000);
    fs.add_file("test/video.mp4", 2500);
    fs.add_file("test/subs.srt", 300);
    fs.add_file("test/extra.mp4", 1700);
    fs.set_num_pieces((4500 + 999) / 1000);  // 5
    return fs;
}

class ByteRangeMapperTest : public ::testing::Test {
protected:
    lt::file_storage fs_ = make_test_fs();
};

// --- File 0 (video.mp4, offset=0, size=2500) ---

TEST_F(ByteRangeMapperTest, MapFirstByte) {
    ByteRangeMapper mapper(fs_, 0);
    auto span = mapper.map(ByteRange{0, 0});
    EXPECT_EQ(span.first, 0);
    EXPECT_EQ(span.last, 0);
    EXPECT_EQ(span.first_offset, 0);
    EXPECT_EQ(span.last_length, 1);
}

TEST_F(ByteRangeMapperTest, MapLastByte) {
    ByteRangeMapper mapper(fs_, 0);
    auto span = mapper.map(ByteRange{2499, 2499});
    EXPECT_EQ(span.first, 2);
    EXPECT_EQ(span.last, 2);
    EXPECT_EQ(span.first_offset, 499);
    EXPECT_EQ(span.last_length, 500);  // offset 499 + length 1
}

TEST_F(ByteRangeMapperTest, MapCrossPieceBoundary) {
    ByteRangeMapper mapper(fs_, 0);
    // Byte 999 is last byte of piece 0, byte 1000 is first byte of piece 1
    auto span = mapper.map(ByteRange{999, 1000});
    EXPECT_EQ(span.first, 0);
    EXPECT_EQ(span.last, 1);
    EXPECT_EQ(span.first_offset, 999);
    EXPECT_EQ(span.last_length, 1);  // offset 0 + length 1
}

TEST_F(ByteRangeMapperTest, MapSingleByteMiddle) {
    ByteRangeMapper mapper(fs_, 0);
    auto span = mapper.map(ByteRange{500, 500});
    EXPECT_EQ(span.first, 0);
    EXPECT_EQ(span.last, 0);
    EXPECT_EQ(span.first_offset, 500);
    EXPECT_EQ(span.last_length, 501);  // offset 500 + length 1
}

TEST_F(ByteRangeMapperTest, MapEntireFile) {
    ByteRangeMapper mapper(fs_, 0);
    auto span = mapper.map(ByteRange{0, 2499});
    EXPECT_EQ(span.first, 0);
    EXPECT_EQ(span.last, 2);
    EXPECT_EQ(span.first_offset, 0);
    EXPECT_EQ(span.last_length, 500);  // offset 499 + length 1
}

TEST_F(ByteRangeMapperTest, MapFullPiece) {
    ByteRangeMapper mapper(fs_, 0);
    // Range covering exactly piece 1
    auto span = mapper.map(ByteRange{1000, 1999});
    EXPECT_EQ(span.first, 1);
    EXPECT_EQ(span.last, 1);
    EXPECT_EQ(span.first_offset, 0);
    EXPECT_EQ(span.last_length, 1000);  // offset 999 + length 1
}

// --- File 2 (extra.mp4, offset_in_torrent=2800, size=1700) ---

TEST_F(ByteRangeMapperTest, MapFileWithOffsetFirstByte) {
    ByteRangeMapper mapper(fs_, 2);
    auto span = mapper.map(ByteRange{0, 0});
    EXPECT_EQ(span.first, 2);   // torrent byte 2800 → piece 2
    EXPECT_EQ(span.last, 2);
    EXPECT_EQ(span.first_offset, 800);  // 2800 - 2*1000 = 800
    EXPECT_EQ(span.last_length, 801);   // offset 800 + length 1
}

TEST_F(ByteRangeMapperTest, MapFileWithOffsetLastByte) {
    ByteRangeMapper mapper(fs_, 2);
    auto span = mapper.map(ByteRange{1699, 1699});
    EXPECT_EQ(span.first, 4);   // torrent byte 4499 → piece 4
    EXPECT_EQ(span.last, 4);
    EXPECT_EQ(span.first_offset, 499);
    EXPECT_EQ(span.last_length, 500);  // piece_size(4) = 500
}

TEST_F(ByteRangeMapperTest, MapEntireLastFile) {
    ByteRangeMapper mapper(fs_, 2);
    auto span = mapper.map(ByteRange{0, 1699});
    EXPECT_EQ(span.first, 2);
    EXPECT_EQ(span.last, 4);
    EXPECT_EQ(span.first_offset, 800);
    EXPECT_EQ(span.last_length, 500);
}

// --- File 1 (subs.srt, offset_in_torrent=2500, size=300, entirely in piece 2) ---

TEST_F(ByteRangeMapperTest, MapSmallFileInSharedPiece) {
    ByteRangeMapper mapper(fs_, 1);
    auto span = mapper.map(ByteRange{0, 299});
    EXPECT_EQ(span.first, 2);   // torrent byte 2500 → piece 2
    EXPECT_EQ(span.last, 2);    // torrent byte 2799 → still piece 2
    EXPECT_EQ(span.first_offset, 500);  // 2500 - 2*1000 = 500
    EXPECT_EQ(span.last_length, 800);   // offset 799 + length 1
}

// --- first_piece() / end_piece() ---

TEST_F(ByteRangeMapperTest, FirstPieceAndEndPieceFileZero) {
    ByteRangeMapper mapper(fs_, 0);
    EXPECT_EQ(mapper.first_piece(), 0);
    EXPECT_EQ(mapper.end_piece(), 3);  // byte 2499 → piece 2, end = 3
}

TEST_F(ByteRangeMapperTest, FirstPieceAndEndPieceSmallFile) {
    ByteRangeMapper mapper(fs_, 1);
    EXPECT_EQ(mapper.first_piece(), 2);
    EXPECT_EQ(mapper.end_piece(), 3);  // entirely in piece 2, end = 3
}

TEST_F(ByteRangeMapperTest, FirstPieceAndEndPieceLastFile) {
    ByteRangeMapper mapper(fs_, 2);
    EXPECT_EQ(mapper.first_piece(), 2);  // starts in piece 2
    EXPECT_EQ(mapper.end_piece(), 5);    // ends in piece 4, end = 5
}

// --- piece_length() / file_size() ---

TEST_F(ByteRangeMapperTest, PieceLengthAndFileSize) {
    ByteRangeMapper mapper0(fs_, 0);
    EXPECT_EQ(mapper0.piece_length(), 1000);
    EXPECT_EQ(mapper0.file_size(), 2500);

    ByteRangeMapper mapper1(fs_, 1);
    EXPECT_EQ(mapper1.piece_length(), 1000);
    EXPECT_EQ(mapper1.file_size(), 300);

    ByteRangeMapper mapper2(fs_, 2);
    EXPECT_EQ(mapper2.piece_length(), 1000);
    EXPECT_EQ(mapper2.file_size(), 1700);
}

// --- Sintel torrent real-world tests ---

class ByteRangeMapperSintelTest : public ::testing::Test {
protected:
    void SetUp() override {
        lt::error_code ec;
        auto atp = lt::load_torrent_file(
            std::string(SEEKSERVE_FIXTURE_DIR) + "/Sintel_archive.torrent", ec, lt::load_torrent_limits{});
        ASSERT_FALSE(ec) << "Failed to load Sintel torrent: " << ec.message();
        ti_ = atp.ti;
    }

    std::shared_ptr<const lt::torrent_info> ti_;
};

TEST_F(ByteRangeMapperSintelTest, SintelMp4FileSize) {
    const auto& fs = ti_->layout();
    ASSERT_GT(fs.num_files(), 8);

    ByteRangeMapper mapper(fs, 8);

    // sintel-2048-stereo_512kb.mp4 ≈ 73.8 MB
    EXPECT_GT(mapper.file_size(), 70'000'000);
    EXPECT_LT(mapper.file_size(), 80'000'000);
}

TEST_F(ByteRangeMapperSintelTest, SintelFirstAndLastByte) {
    ByteRangeMapper mapper(ti_->layout(), 8);

    auto span_first = mapper.map(ByteRange{0, 0});
    EXPECT_EQ(span_first.first, mapper.first_piece());
    EXPECT_EQ(span_first.last, mapper.first_piece());

    auto span_last = mapper.map(ByteRange{mapper.file_size() - 1, mapper.file_size() - 1});
    EXPECT_EQ(span_last.last, mapper.end_piece() - 1);
}

TEST_F(ByteRangeMapperSintelTest, SintelEntireFileSpansPieces) {
    ByteRangeMapper mapper(ti_->layout(), 8);

    auto span = mapper.map(ByteRange{0, mapper.file_size() - 1});
    EXPECT_EQ(span.first, mapper.first_piece());
    EXPECT_EQ(span.last, mapper.end_piece() - 1);
    EXPECT_EQ(span.first_offset, mapper.map(ByteRange{0, 0}).first_offset);

    // File should span many pieces
    int piece_count = mapper.end_piece() - mapper.first_piece();
    EXPECT_GT(piece_count, 10);
}

TEST_F(ByteRangeMapperSintelTest, SintelCrossPieceBoundary) {
    ByteRangeMapper mapper(ti_->layout(), 8);
    int pl = mapper.piece_length();

    // Range crossing from first piece into second piece of this file
    int first_piece_local_end = pl - mapper.map(ByteRange{0, 0}).first_offset - 1;
    if (first_piece_local_end + 1 < mapper.file_size()) {
        auto span = mapper.map(ByteRange{first_piece_local_end, first_piece_local_end + 1});
        EXPECT_EQ(span.last - span.first, 1);  // spans exactly 2 pieces
    }
}

TEST_F(ByteRangeMapperSintelTest, SintelMidFileMegabyteRange) {
    ByteRangeMapper mapper(ti_->layout(), 8);

    // 1 MB range starting at 1 MB
    std::int64_t start = 1024 * 1024;
    std::int64_t end = 2 * 1024 * 1024 - 1;
    ASSERT_LT(end, mapper.file_size());

    auto span = mapper.map(ByteRange{start, end});
    EXPECT_GE(span.first, mapper.first_piece());
    EXPECT_LE(span.last, mapper.end_piece() - 1);
    EXPECT_GT(span.last, span.first);  // 1 MB spans multiple pieces
}

}  // namespace
}  // namespace seekserve
