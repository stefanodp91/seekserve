#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <vector>

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_info.hpp>

#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/config.hpp"
#include "seekserve/piece_availability.hpp"
#include "seekserve/startup_focus.hpp"

using namespace seekserve;
using namespace std::chrono_literals;

// The start of a stream with priority (app BUG-228, DEC-656): the pieces of
// the first bytes from where a reader reads come first, the others it wants
// last, until those bytes are there or the time limit passed.
namespace {

constexpr int kPieceLength = 256 * 1024;  // 256 KB
constexpr std::int64_t kMb = 1024 * 1024;

std::shared_ptr<const lt::torrent_info> make_torrent(
    const std::vector<std::int64_t>& sizes) {
    std::vector<lt::create_file_entry> files;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        files.emplace_back("pack/file" + std::to_string(i) + ".mkv", sizes[i]);
    }
    lt::create_torrent ct(std::move(files), kPieceLength, lt::create_torrent::v1_only);
    lt::sha1_hash dummy;
    std::memset(dummy.data(), 0x42, dummy.size());
    for (int i = 0; i < ct.num_pieces(); ++i) ct.set_hash(lt::piece_index_t{i}, dummy);
    auto buf = ct.generate_buf();
    return lt::load_torrent_buffer(buf).ti;
}

PieceAvailabilityIndex make_avail(const lt::torrent_info& ti) {
    const int np = ti.num_pieces();
    const int last = static_cast<int>(
        ti.total_size() - static_cast<std::int64_t>(np - 1) * kPieceLength);
    return PieceAvailabilityIndex(np, kPieceLength, last);
}

SchedulerConfig focus_config(std::int64_t bytes = 2 * kMb) {
    SchedulerConfig cfg;
    cfg.startup_focus_bytes = bytes;
    cfg.startup_focus_timeout = 30s;
    return cfg;
}

std::vector<lt::download_priority_t> all(int count, lt::download_priority_t p) {
    return std::vector<lt::download_priority_t>(static_cast<std::size_t>(count), p);
}

}  // namespace

TEST(StartupFocusTest, TheFirstBytesFromWhereItReadsComeFirstTheRestLast) {
    auto ti = make_torrent({10 * kMb});  // 40 pieces
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    StartupFocus focus(focus_config());

    auto wanted = focus.begin(0, mapper, avail, all(40, lt::default_priority),
                              StartupFocus::Clock::now());

    ASSERT_EQ(wanted.size(), 40u);
    for (int p = 0; p < 8; ++p) EXPECT_EQ(wanted[p], lt::top_priority) << p;
    for (int p = 8; p < 40; ++p) EXPECT_EQ(wanted[p], lt::low_priority) << p;
    EXPECT_TRUE(focus.active());
}

TEST(StartupFocusTest, TheWindowStartsWhereItReadsAndStopsAtTheEndOfTheFile) {
    auto ti = make_torrent({10 * kMb});
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    StartupFocus focus(focus_config());

    auto wanted = focus.begin(9 * kMb, mapper, avail, all(40, lt::default_priority),
                              StartupFocus::Clock::now());

    ASSERT_EQ(wanted.size(), 40u);
    for (int p = 0; p < 36; ++p) EXPECT_EQ(wanted[p], lt::low_priority) << p;
    for (int p = 36; p < 40; ++p) EXPECT_EQ(wanted[p], lt::top_priority) << p;
}

TEST(StartupFocusTest, APieceNobodyWantsStaysUnwanted) {
    // The reader is the second file: the first one is not wanted.
    auto ti = make_torrent({4 * kMb, 6 * kMb});  // 16 + 24 pieces
    ByteRangeMapper mapper(ti->layout(), 1);
    auto avail = make_avail(*ti);
    auto current = all(40, lt::default_priority);
    for (int p = 0; p < 16; ++p) current[p] = lt::dont_download;
    StartupFocus focus(focus_config());

    auto wanted = focus.begin(0, mapper, avail, current, StartupFocus::Clock::now());

    ASSERT_EQ(wanted.size(), 40u);
    for (int p = 0; p < 16; ++p) EXPECT_EQ(wanted[p], lt::dont_download) << p;
    for (int p = 16; p < 24; ++p) EXPECT_EQ(wanted[p], lt::top_priority) << p;
    for (int p = 24; p < 40; ++p) EXPECT_EQ(wanted[p], lt::low_priority) << p;
}

TEST(StartupFocusTest, NothingToDoWhenTheFirstBytesAreThere) {
    auto ti = make_torrent({10 * kMb});
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    for (int p = 0; p < 8; ++p) avail.mark_complete(p);
    StartupFocus focus(focus_config());

    auto wanted = focus.begin(0, mapper, avail, all(40, lt::default_priority),
                              StartupFocus::Clock::now());

    EXPECT_TRUE(wanted.empty());
    EXPECT_FALSE(focus.active());
}

TEST(StartupFocusTest, OverWhenTheFirstBytesAreThere) {
    auto ti = make_torrent({10 * kMb});
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    StartupFocus focus(focus_config());
    const auto start = StartupFocus::Clock::now();
    focus.begin(0, mapper, avail, all(40, lt::default_priority), start);

    for (int p = 0; p < 7; ++p) avail.mark_complete(p);
    EXPECT_FALSE(focus.over(avail, start + 1s));
    avail.mark_complete(7);
    EXPECT_TRUE(focus.over(avail, start + 1s));
}

TEST(StartupFocusTest, OverAfterTheTimeLimit) {
    auto ti = make_torrent({10 * kMb});
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    StartupFocus focus(focus_config());
    const auto start = StartupFocus::Clock::now();
    focus.begin(0, mapper, avail, all(40, lt::default_priority), start);

    EXPECT_FALSE(focus.over(avail, start + 29s));
    EXPECT_TRUE(focus.over(avail, start + 30s));
}

TEST(StartupFocusTest, EndedItIsNotActive) {
    auto ti = make_torrent({10 * kMb});
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    StartupFocus focus(focus_config());
    focus.begin(0, mapper, avail, all(40, lt::default_priority),
                StartupFocus::Clock::now());

    focus.end();

    EXPECT_FALSE(focus.active());
}

TEST(StartupFocusTest, OffWithoutBytes) {
    auto ti = make_torrent({10 * kMb});
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    StartupFocus focus(focus_config(0));

    auto wanted = focus.begin(0, mapper, avail, all(40, lt::default_priority),
                              StartupFocus::Clock::now());

    EXPECT_TRUE(wanted.empty());
    EXPECT_FALSE(focus.active());
}

// A file whose index may be at its end (an MP4): a reader that starts asks
// for the end too before it plays, so the last bytes come first with the
// first ones (app BUG-231).
TEST(StartupFocusTest, TheEndOfAFileWithItsIndexThereComesFirstToo) {
    auto ti = make_torrent({10 * kMb});  // 40 pieces
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    StartupFocus focus(focus_config(), true);

    auto wanted = focus.begin(0, mapper, avail, all(40, lt::default_priority),
                              StartupFocus::Clock::now());

    ASSERT_EQ(wanted.size(), 40u);
    for (int p = 0; p < 8; ++p) EXPECT_EQ(wanted[p], lt::top_priority) << p;
    for (int p = 8; p < 32; ++p) EXPECT_EQ(wanted[p], lt::low_priority) << p;
    for (int p = 32; p < 40; ++p) EXPECT_EQ(wanted[p], lt::top_priority) << p;
}

TEST(StartupFocusTest, TheEndIsTheEndOfTheReadersFile) {
    auto ti = make_torrent({6 * kMb, 4 * kMb});  // 24 + 16 pieces
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    StartupFocus focus(focus_config(), true);

    auto wanted = focus.begin(0, mapper, avail, all(40, lt::default_priority),
                              StartupFocus::Clock::now());

    ASSERT_EQ(wanted.size(), 40u);
    for (int p = 0; p < 8; ++p) EXPECT_EQ(wanted[p], lt::top_priority) << p;
    for (int p = 8; p < 16; ++p) EXPECT_EQ(wanted[p], lt::low_priority) << p;
    for (int p = 16; p < 24; ++p) EXPECT_EQ(wanted[p], lt::top_priority) << p;
    for (int p = 24; p < 40; ++p) EXPECT_EQ(wanted[p], lt::low_priority) << p;
}

TEST(StartupFocusTest, AMissingEndAloneBeginsIt) {
    auto ti = make_torrent({10 * kMb});
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    for (int p = 0; p < 8; ++p) avail.mark_complete(p);
    StartupFocus focus(focus_config(), true);

    auto wanted = focus.begin(0, mapper, avail, all(40, lt::default_priority),
                              StartupFocus::Clock::now());

    ASSERT_EQ(wanted.size(), 40u);
    for (int p = 32; p < 40; ++p) EXPECT_EQ(wanted[p], lt::top_priority) << p;
    EXPECT_TRUE(focus.active());
}

TEST(StartupFocusTest, NothingToDoWhenTheFirstAndLastBytesAreThere) {
    auto ti = make_torrent({10 * kMb});
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    for (int p = 0; p < 8; ++p) avail.mark_complete(p);
    for (int p = 32; p < 40; ++p) avail.mark_complete(p);
    StartupFocus focus(focus_config(), true);

    auto wanted = focus.begin(0, mapper, avail, all(40, lt::default_priority),
                              StartupFocus::Clock::now());

    EXPECT_TRUE(wanted.empty());
    EXPECT_FALSE(focus.active());
}

TEST(StartupFocusTest, OverOnlyWhenTheLastBytesAreThereToo) {
    auto ti = make_torrent({10 * kMb});
    ByteRangeMapper mapper(ti->layout(), 0);
    auto avail = make_avail(*ti);
    StartupFocus focus(focus_config(), true);
    const auto start = StartupFocus::Clock::now();
    focus.begin(0, mapper, avail, all(40, lt::default_priority), start);

    for (int p = 0; p < 8; ++p) avail.mark_complete(p);
    EXPECT_FALSE(focus.over(avail, start + 1s));
    for (int p = 32; p < 40; ++p) avail.mark_complete(p);
    EXPECT_TRUE(focus.over(avail, start + 1s));
}

TEST(StartupFocusTest, OnlyMp4AndMovFilesMayKeepTheirIndexAtTheEnd) {
    EXPECT_TRUE(keeps_index_at_end("Show/Show.S01E02.mp4"));
    EXPECT_TRUE(keeps_index_at_end("film.MP4"));
    EXPECT_TRUE(keeps_index_at_end("film.m4v"));
    EXPECT_TRUE(keeps_index_at_end("film.mov"));
    EXPECT_FALSE(keeps_index_at_end("film.mkv"));
    EXPECT_FALSE(keeps_index_at_end("film.mp4.part"));
    EXPECT_FALSE(keeps_index_at_end("mp4"));
    EXPECT_FALSE(keeps_index_at_end(""));
}
