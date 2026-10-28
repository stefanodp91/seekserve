// The MP4 an MKV is rebuilt from for a TV share with subtitles (app WORK-37
// step 7d): its tracks and sample tables, read from the moov wherever it is,
// checked sample by sample against what ffprobe says of the same files
// (fixtures/*.packets.csv: stream, pts, dts, size, pos, flags).

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "mkv_test_support.hpp"
#include "seekserve/mp4_structure.hpp"

namespace seekserve {
namespace {

using namespace mkvtest;

std::string fixture(const std::string& name) { return std::string(SEEKSERVE_FIXTURE_DIR) + "/" + name; }

Mp4Structure read_fixture(const std::string& name) {
    auto file = read_file(fixture(name));
    EXPECT_FALSE(file.empty()) << name;
    std::string why;
    auto s = read_mp4_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why);
    EXPECT_TRUE(s) << name << ": " << why;
    return s ? s.value() : Mp4Structure{};
}

struct Packet {
    int stream = 0;
    std::int64_t pts = 0, dts = 0, pos = 0;
    std::uint32_t size = 0;
    bool key = false;
};

// ffprobe's packets of a stream, in the file's order: for one track, the
// order of its samples.
std::vector<Packet> packets(const std::string& name, int stream) {
    std::ifstream in(fixture(name));
    std::vector<Packet> out;
    std::string line;
    while (std::getline(in, line)) {
        std::stringstream fields(line);
        std::string f[6];
        for (auto& x : f) std::getline(fields, x, ',');
        Packet p{std::stoi(f[0]), std::stoll(f[1]), std::stoll(f[2]), std::stoll(f[4]),
                 static_cast<std::uint32_t>(std::stoul(f[3])), f[5].find('K') != std::string::npos};
        if (p.stream == stream) out.push_back(p);
    }
    return out;
}

std::vector<Mp4Sample> samples(const Mp4Track& t) {
    std::vector<Mp4Sample> out;
    Mp4SampleCursor c(t);
    Mp4Sample s;
    while (c.next(s)) out.push_back(s);
    return out;
}

// ffprobe applies the edit list: its times are the file's, less where the
// media starts.
void expect_like_ffprobe(const Mp4Track& t, const std::vector<Packet>& expected) {
    auto got = samples(t);
    ASSERT_EQ(got.size(), expected.size());
    ASSERT_EQ(t.sample_count, expected.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        EXPECT_EQ(got[i].index, i);
        EXPECT_EQ(got[i].offset, expected[i].pos) << "sample " << i;
        EXPECT_EQ(got[i].size, expected[i].size) << "sample " << i;
        EXPECT_EQ(got[i].pts - t.media_start, expected[i].pts) << "sample " << i;
        EXPECT_EQ(got[i].dts - t.media_start, expected[i].dts) << "sample " << i;
        if (t.video) EXPECT_EQ(got[i].key, expected[i].key) << "sample " << i;
    }
}

TEST(Mp4Structure, AnH264AndAacFileWithItsIndexAtTheEnd) {
    auto s = read_fixture("mp4_avc_aac.mp4");
    ASSERT_EQ(s.tracks.size(), 2u);
    EXPECT_GT(s.moov_start, 0);
    EXPECT_EQ(s.moov_end, s.file_size);

    const auto& v = s.tracks[0];
    EXPECT_TRUE(v.video);
    EXPECT_EQ(v.codec, Mp4Codec::avc);
    EXPECT_EQ(v.width, 160u);
    EXPECT_EQ(v.height, 90u);
    EXPECT_EQ(v.timescale, 24000u);
    EXPECT_EQ(v.media_start, 2002);  // the B-frames' delay
    ASSERT_FALSE(v.codec_private.empty());
    EXPECT_EQ(v.codec_private[0], 1);  // avcC version
    EXPECT_FALSE(v.all_sync);
    expect_like_ffprobe(v, packets("mp4_avc_aac.packets.csv", 0));

    const auto& a = s.tracks[1];
    EXPECT_FALSE(a.video);
    EXPECT_EQ(a.codec, Mp4Codec::aac);
    EXPECT_EQ(a.sample_rate, 48000u);
    EXPECT_EQ(a.channels, 1u);
    EXPECT_EQ(a.media_start, 1024);  // the AAC's priming
    EXPECT_EQ(a.codec_private.size(), 5u);  // the AudioSpecificConfig, as ffprobe says
    EXPECT_TRUE(a.all_sync);
    expect_like_ffprobe(a, packets("mp4_avc_aac.packets.csv", 1));
}

TEST(Mp4Structure, TheIndexAtTheHeadGivesTheSameSamplesMovedByTheIndex) {
    auto end = read_fixture("mp4_avc_aac.mp4");
    auto head = read_fixture("mp4_avc_aac_faststart.mp4");
    ASSERT_EQ(head.tracks.size(), 2u);
    EXPECT_LT(head.moov_start, 100);
    auto moved = head.moov_end - head.moov_start;  // the moov, now before the samples
    for (std::size_t t = 0; t < 2; ++t) {
        auto a = samples(end.tracks[t]);
        auto b = samples(head.tracks[t]);
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(b[i].size, a[i].size);
            EXPECT_EQ(b[i].pts, a[i].pts);
            EXPECT_EQ(b[i].dts, a[i].dts);
            EXPECT_EQ(b[i].key, a[i].key);
            EXPECT_EQ(b[i].offset - a[i].offset, moved) << "sample " << i;
        }
    }
}

TEST(Mp4Structure, AnHevcAndAc3File) {
    auto s = read_fixture("mp4_hevc_ac3.mp4");
    ASSERT_EQ(s.tracks.size(), 2u);
    EXPECT_EQ(s.tracks[0].codec, Mp4Codec::hevc);
    ASSERT_FALSE(s.tracks[0].codec_private.empty());
    EXPECT_EQ(s.tracks[0].codec_private[0], 1);  // hvcC version
    expect_like_ffprobe(s.tracks[0], packets("mp4_hevc_ac3.packets.csv", 0));
    EXPECT_EQ(s.tracks[1].codec, Mp4Codec::ac3);
    EXPECT_TRUE(s.tracks[1].codec_private.empty());
    expect_like_ffprobe(s.tracks[1], packets("mp4_hevc_ac3.packets.csv", 1));
}

TEST(Mp4Structure, TimesOnTheFilmsLineFollowTheEditList) {
    auto s = read_fixture("mp4_avc_aac.mp4");
    const auto& v = s.tracks[0];
    EXPECT_EQ(v.presentation_ns(2002), 0);
    EXPECT_EQ(v.presentation_ns(2002 + 24000), 1000000000);
    EXPECT_EQ(v.presentation_ns(0), -83416667);  // before the edit, rounded to the nearest ns
    Mp4Track delayed = v;
    delayed.delay_ns = 500000000;
    EXPECT_EQ(delayed.presentation_ns(2002), 500000000);
}

TEST(Mp4Structure, ACopiedCursorGoesOnFromWhereItWas) {
    auto s = read_fixture("mp4_avc_aac.mp4");
    for (const auto& t : s.tracks) {
        auto all = samples(t);
        Mp4SampleCursor c(t);
        Mp4Sample x;
        for (int i = 0; i < 37; ++i) ASSERT_TRUE(c.next(x));
        Mp4SampleCursor copy = c;
        EXPECT_EQ(copy.index(), 37u);
        std::size_t i = 37;
        while (copy.next(x)) {
            EXPECT_EQ(x.offset, all[i].offset);
            EXPECT_EQ(x.pts, all[i].pts);
            ++i;
        }
        EXPECT_EQ(i, all.size());
    }
}

TEST(Mp4Structure, FilesThatCannotBeRebuiltSaySo) {
    std::string why;
    auto frag = read_file(fixture("mp4_fragmented.mp4"));
    EXPECT_EQ(read_mp4_structure(reader_of(frag), static_cast<std::int64_t>(frag.size()), why).error(),
              make_error_code(errc::invalid_argument));
    EXPECT_EQ(why, "a fragmented MP4");

    auto mkv = read_file(fixture("subtitles_base.mkv"));
    EXPECT_FALSE(read_mp4_structure(reader_of(mkv), static_cast<std::int64_t>(mkv.size()), why));
    EXPECT_EQ(why, "not an MP4 file");

    // The moov is cut off: the file is not all there, or broken.
    auto file = read_file(fixture("mp4_avc_aac.mp4"));
    file.resize(file.size() - 500);
    EXPECT_FALSE(read_mp4_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why));
    EXPECT_EQ(why, "a box past the end of the file");
}

// A moov that says it is huge is refused before it is read (a hostile file
// of a torrent).
TEST(Mp4Structure, AnIndexTooLargeIsNotRead) {
    Bytes file = {0, 0, 0, 16, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 2, 0,
                  0x10, 0, 0, 0, 'm', 'o', 'o', 'v'};
    std::int64_t size = 16 + 0x10000000;
    std::int64_t asked = 0;
    Mp4Read read = [&](std::int64_t offset, std::int64_t len) -> Result<std::vector<std::uint8_t>> {
        asked += len;
        Bytes out;
        for (std::int64_t i = offset; i < offset + len && i < size; ++i) {
            out.push_back(i < static_cast<std::int64_t>(file.size()) ? file[i] : 0);
        }
        return out;
    };
    std::string why;
    EXPECT_FALSE(read_mp4_structure(read, size, why));
    EXPECT_EQ(why, "a moov too large to rebuild");
    EXPECT_LT(asked, 1 << 20);
}

// Sample tables that point outside the file, or disagree on the number of
// samples, are refused before any layout uses them.
TEST(Mp4Structure, TablesThatDisagreeAreRefused) {
    auto file = read_file(fixture("mp4_avc_aac.mp4"));
    std::string why;
    auto s = read_mp4_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why).value();
    // The first chunk offset of the video, moved past the end of the file.
    auto pattern = [&](const char* box) {
        for (std::size_t i = s.moov_start; i + 4 < file.size(); ++i) {
            if (std::equal(box, box + 4, file.begin() + i)) return i;
        }
        return std::size_t{0};
    };
    auto stco = pattern("stco");
    ASSERT_GT(stco, 0u);
    for (int b = 0; b < 4; ++b) file[stco + 12 + b] = 0x7F;
    EXPECT_FALSE(read_mp4_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why));
    EXPECT_EQ(why, "a sample outside the file");
}

// A 64-bit chunk offset near the top of int64 would wrap the check of the
// sample's end: it is refused as a chunk past the file.
TEST(Mp4Structure, AChunkOffsetNearTheTopOfInt64IsRefused) {
    auto file = read_file(fixture("mp4_avc_aac.mp4"));
    std::string why;
    auto s = read_mp4_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why).value();
    std::size_t stco = 0;
    for (std::size_t i = s.moov_start; i + 4 < file.size() && stco == 0; ++i) {
        if (std::equal("stco", "stco" + 4, file.begin() + i)) stco = i;
    }
    ASSERT_GT(stco, 0u);
    // The video's stco (count n) as a co64 with the same count, its first
    // entry 0x7FFFFFFFFFFFFF00, the rest dropped to keep the box's length.
    auto box = stco - 4;
    std::uint32_t box_size = file[box] << 24 | file[box + 1] << 16 | file[box + 2] << 8 | file[box + 3];
    std::uint32_t fits = (box_size - 16) / 8;
    file[stco + 2] = '6';
    file[stco + 3] = '4';
    file[stco] = 'c';
    file[stco + 1] = 'o';
    for (int b = 0; b < 4; ++b) file[stco + 8 + b] = static_cast<std::uint8_t>(fits >> (24 - 8 * b));
    const std::uint8_t top[8] = {0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    std::copy(top, top + 8, file.begin() + stco + 12);
    EXPECT_FALSE(read_mp4_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why));
    EXPECT_EQ(why, "a sample outside the file");
}

TEST(Mp4Structure, AMissingPieceIsATimeoutNotAVerdict) {
    auto file = read_file(fixture("mp4_avc_aac.mp4"));
    Mp4Read slow = [&](std::int64_t offset, std::int64_t len) -> Result<std::vector<std::uint8_t>> {
        if (offset + len > static_cast<std::int64_t>(file.size()) - 1000) {
            return make_error_code(errc::timeout_waiting_for_piece);  // the moov is not here yet
        }
        return reader_of(file)(offset, len);
    };
    std::string why;
    EXPECT_EQ(read_mp4_structure(slow, static_cast<std::int64_t>(file.size()), why).error(),
              make_error_code(errc::timeout_waiting_for_piece));
}

}  // namespace
}  // namespace seekserve
