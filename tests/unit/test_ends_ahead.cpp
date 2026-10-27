#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <set>
#include <vector>

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_info.hpp>

#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/ends_ahead.hpp"

using namespace seekserve;

// The start and the end of the next episode's file, fetched while another
// file of the pack plays (app BUG-231, DEC-678).
namespace {

constexpr int kPieceLength = 256 * 1024;  // 256 KB
constexpr std::int64_t kMb = 1024 * 1024;

std::shared_ptr<const lt::torrent_info> make_torrent(
    const std::vector<std::int64_t>& sizes) {
    std::vector<lt::create_file_entry> files;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        files.emplace_back("pack/file" + std::to_string(i) + ".mp4", sizes[i]);
    }
    lt::create_torrent ct(std::move(files), kPieceLength, lt::create_torrent::v1_only);
    lt::sha1_hash dummy;
    std::memset(dummy.data(), 0x42, dummy.size());
    for (int i = 0; i < ct.num_pieces(); ++i) ct.set_hash(lt::piece_index_t{i}, dummy);
    auto buf = ct.generate_buf();
    return lt::load_torrent_buffer(buf).ti;
}

std::vector<lt::download_priority_t> all(int count, lt::download_priority_t p) {
    return std::vector<lt::download_priority_t>(static_cast<std::size_t>(count), p);
}

}  // namespace

TEST(EndsAheadTest, TheFirstAndLastBytesOfTheFile) {
    // The reader is file 0; the next episode is file 1: pieces 16..39.
    auto ti = make_torrent({4 * kMb, 6 * kMb});
    ByteRangeMapper mapper(ti->layout(), 1);

    const auto pieces = file_end_pieces(mapper, 1 * kMb);

    const std::vector<PieceIndex> expected{16, 17, 18, 19, 36, 37, 38, 39};
    EXPECT_EQ(pieces, expected);
}

TEST(EndsAheadTest, ASmallFileIsOneSpanWithoutRepeats) {
    auto ti = make_torrent({4 * kMb, 1 * kMb});  // file 1: pieces 16..19
    ByteRangeMapper mapper(ti->layout(), 1);

    const auto pieces = file_end_pieces(mapper, 8 * kMb);

    const std::vector<PieceIndex> expected{16, 17, 18, 19};
    EXPECT_EQ(pieces, expected);
}

TEST(EndsAheadTest, NoBytesNoPieces) {
    auto ti = make_torrent({4 * kMb, 6 * kMb});
    ByteRangeMapper mapper(ti->layout(), 1);

    EXPECT_TRUE(file_end_pieces(mapper, 0).empty());
}

TEST(EndsAheadTest, TheMissingOnesGoFirstAndTheRestStaysAsItWas) {
    auto current = all(8, lt::dont_download);
    current[0] = lt::default_priority;  // the reader's
    const std::set<PieceIndex> had{5};

    const auto wanted = with_pieces_first(current, {4, 5, 7},
                                          [&](PieceIndex p) { return had.count(p) > 0; });

    ASSERT_EQ(wanted.size(), 8u);
    EXPECT_EQ(wanted[0], lt::default_priority);
    EXPECT_EQ(wanted[4], lt::top_priority);
    EXPECT_EQ(wanted[5], lt::dont_download);  // already there
    EXPECT_EQ(wanted[7], lt::top_priority);
    for (int p : {1, 2, 3, 6}) EXPECT_EQ(wanted[p], lt::dont_download) << p;
}

TEST(EndsAheadTest, APieceOutsideTheTorrentIsLeftOut) {
    const auto wanted = with_pieces_first(all(4, lt::dont_download), {2, 9},
                                          [](PieceIndex) { return false; });

    ASSERT_EQ(wanted.size(), 4u);
    EXPECT_EQ(wanted[2], lt::top_priority);
}
