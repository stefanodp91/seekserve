// The rebuilt Matroska file of a TV share with subtitles (app WORK-37 step
// 7, DEC-618, DEC-700): the EBML primitives, the checks of the subtitle
// lines, the structure read from the head and the Cues, and the layout read
// back as a whole file.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "mkv_test_support.hpp"
#include "seekserve/ebml.hpp"
#include "seekserve/mkv_subtitle_layout.hpp"
#include "seekserve/subtitle_track.hpp"

namespace seekserve {
namespace {

using namespace mkvtest;

SubtitleTrack lines(std::vector<std::pair<std::int64_t, std::string>> at) {
    SubtitleTrack t;
    t.language = "ita";
    t.name = "Italiano";
    for (auto& [ms, text] : at) t.cues.push_back({ms, ms + 1500, text});
    return checked_subtitle_track(t).value();
}

// The bytes of `layout`, from the original `file` and the new pieces.
Bytes assemble(const MkvSubtitleLayout& layout, const Bytes& file) {
    Bytes out;
    for (const auto& p : layout.pieces) {
        EXPECT_EQ(p.at, static_cast<std::int64_t>(out.size()));
        if (p.file_offset >= 0) {
            out.insert(out.end(), file.begin() + p.file_offset, file.begin() + p.file_offset + p.length);
        } else {
            out.insert(out.end(), p.bytes->begin() + p.bytes_offset,
                       p.bytes->begin() + p.bytes_offset + p.length);
        }
    }
    EXPECT_EQ(static_cast<std::int64_t>(out.size()), layout.size);
    return out;
}

struct Rebuilt {
    Bytes file;
    MkvStructure structure;
    std::vector<Cluster> clusters;
    std::uint64_t track;
};

Rebuilt rebuild(const Bytes& original, const SubtitleTrack& track) {
    std::string why;
    auto s = read_mkv_structure(reader_of(original), static_cast<std::int64_t>(original.size()), why);
    EXPECT_TRUE(s) << why;
    auto layout = build_mkv_subtitle_layout(s.value(), track);
    Rebuilt r{assemble(layout, original), {}, {}, layout.track_number};
    auto again = read_mkv_structure(reader_of(r.file), static_cast<std::int64_t>(r.file.size()), why);
    EXPECT_TRUE(again) << "the rebuilt file reads back: " << why;
    r.structure = again.value();
    r.clusters = clusters_of(r.file, r.structure);
    return r;
}

std::string fixture() { return std::string(SEEKSERVE_FIXTURE_DIR) + "/subtitles_base.mkv"; }

TEST(Ebml, HeadsAndNumbersReadBackAsWritten) {
    auto e = ebml::uint_element(ebml::kCueClusterPosition, 0x1234, 8);
    auto h = ebml::read_header(e.data(), e.size());
    ASSERT_TRUE(h);
    EXPECT_EQ(h->id, ebml::kCueClusterPosition);
    EXPECT_EQ(h->size, 8u);
    EXPECT_EQ(*ebml::read_uint(e.data() + h->length, 8), 0x1234u);

    std::vector<std::uint8_t> size;
    ebml::put_size(size, 127);  // 0x7F in one byte would read as "unknown"
    EXPECT_EQ(size.size(), 2u);
    std::vector<std::uint8_t> unknown = {0x1F, 0x43, 0xB6, 0x75, 0xFF};
    EXPECT_TRUE(ebml::read_header(unknown.data(), unknown.size())->unknown_size);
    EXPECT_FALSE(ebml::read_header(unknown.data(), 3)) << "an incomplete head";
    EXPECT_FALSE(ebml::read_uint(e.data(), 9)) << "longer than 8 bytes";
}

TEST(SubtitleTrackCheck, KeepsGoodLinesInOrderAndRefusesTheRest) {
    SubtitleTrack t;
    t.cues = {{5000, 6000, "seconda"}, {1000, 2000, "prima: àèé"}};
    auto ok = checked_subtitle_track(t);
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok.value().language, "und");
    EXPECT_EQ(ok.value().cues.front().text, "prima: àèé");

    auto refused = [](SubtitleTrack bad) { return !checked_subtitle_track(std::move(bad)); };
    EXPECT_TRUE(refused(SubtitleTrack{}));
    EXPECT_TRUE(refused(SubtitleTrack{"it", "", {{0, 1, "x"}}})) << "two letters";
    EXPECT_TRUE(refused(SubtitleTrack{"ita", "", {{1000, 1000, "x"}}})) << "no duration";
    EXPECT_TRUE(refused(SubtitleTrack{"ita", "", {{-1, 1000, "x"}}}));
    EXPECT_TRUE(refused(SubtitleTrack{"ita", "", {{0, 1000, ""}}}));
    EXPECT_TRUE(refused(SubtitleTrack{"ita", "", {{0, 1000, std::string("a\0b", 3)}}}));
    EXPECT_TRUE(refused(SubtitleTrack{"ita", "", {{0, 1000, "\xC3"}}})) << "cut UTF-8";
    EXPECT_TRUE(refused(SubtitleTrack{"ita", "", {{0, 1000, "\xC0\xAF"}}})) << "overlong";
    EXPECT_TRUE(refused(SubtitleTrack{"ita", "", {{0, 1000, std::string(3000, 'x')}}}));
}

TEST(MkvStructure, AFileOfFfmpegHasItsHeadItsClustersAndItsCuesAfterThem) {
    auto file = read_file(fixture());
    ASSERT_FALSE(file.empty());
    std::string why;
    auto s = read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why);
    ASSERT_TRUE(s) << why;
    EXPECT_GT(s.value().first_cluster, s.value().segment_data);
    EXPECT_GT(s.value().cues_start, s.value().first_cluster);
    EXPECT_EQ(s.value().clusters_end, s.value().cues_start);
    EXPECT_GE(s.value().cues.size(), 10u);
    EXPECT_EQ(s.value().timestamp_scale, 1000000u);
    for (const auto& cue : s.value().cues) {
        for (const auto& p : cue.positions) {
            auto h = ebml::read_header(file.data() + p.cluster, 12);
            ASSERT_TRUE(h);
            EXPECT_EQ(h->id, ebml::kCluster) << "a cue names a Cluster";
        }
    }
}

TEST(MkvStructure, FilesThatCannotBeRebuiltSaySo) {
    std::string why;
    Bytes text(5000, 'x');
    EXPECT_EQ(read_mkv_structure(reader_of(text), 5000, why).error(), make_error_code(errc::invalid_argument));
    EXPECT_EQ(why, "not a Matroska file");

    Synthetic no_cues;
    no_cues.with_cues = false;
    auto file = synthetic_mkv(no_cues);
    EXPECT_FALSE(read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why));
    EXPECT_EQ(why, "no Cues");
}

// A file whose Cues name no Cluster (a broken or hostile file of a torrent)
// has nowhere to put the lines: it is served as it is, never rebuilt.
TEST(MkvStructure, CuesThatNameNoClusterAreNoCues) {
    auto file = synthetic_mkv(Synthetic{});
    std::string why;
    auto s = read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why);
    ASSERT_TRUE(s) << why;
    // Every cue position points past the end of the Segment.
    auto cues = s.value().cues_start;
    auto h = ebml::read_header(file.data() + cues, 12);
    int patched = 0;
    for (std::size_t i = cues + h->length; i + 10 <= file.size(); ++i) {
        if (file[i] == ebml::kCueClusterPosition && file[i + 1] == 0x88) {
            for (int b = 2; b < 10; ++b) file[i + b] = 0x7F;
            ++patched;
        }
    }
    ASSERT_EQ(patched, 4);
    EXPECT_EQ(read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why).error(),
              make_error_code(errc::invalid_argument));
    EXPECT_EQ(why, "no Cues naming a Cluster");
}

// A file that makes the rebuild do much work is not rebuilt: a second Cues
// or Tracks, too many tracks (a hostile file of a torrent).
TEST(MkvStructure, FilesThatWouldCostTooMuchAreNotRebuilt) {
    std::string why;
    Synthetic many;
    many.extra_tracks = 130;
    auto file = synthetic_mkv(many);
    EXPECT_FALSE(read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why));
    EXPECT_EQ(why, "malformed Tracks");

    // The Cues once more at the end, inside the Segment.
    file = synthetic_mkv(Synthetic{});
    auto s = read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why).value();
    Bytes cues(file.begin() + s.cues_start, file.end());
    file.insert(file.end(), cues.begin(), cues.end());
    auto segment_size = static_cast<std::uint64_t>(file.size() - s.segment_data);
    for (int b = 0; b < 7; ++b) file[s.segment_data - 1 - b] = static_cast<std::uint8_t>(segment_size >> (8 * b));
    EXPECT_FALSE(read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why));
    EXPECT_EQ(why, "two Cues");
}

TEST(MkvStructure, AMissingPieceIsATimeoutNotAVerdict) {
    auto file = read_file(fixture());
    MkvRead slow = [&](std::int64_t offset, std::int64_t len) -> Result<std::vector<std::uint8_t>> {
        if (offset + len > static_cast<std::int64_t>(file.size()) - 1000) {
            return make_error_code(errc::timeout_waiting_for_piece);  // the end is not here yet
        }
        return reader_of(file)(offset, len);
    };
    std::string why;
    EXPECT_EQ(read_mkv_structure(slow, static_cast<std::int64_t>(file.size()), why).error(),
              make_error_code(errc::timeout_waiting_for_piece));
}

TEST(MkvSubtitleLayout, TheVideoIsCopiedAndTheLinesAreThereAtTheirTimes) {
    auto file = read_file(fixture());
    auto track = lines({{500, "Riga 1"}, {3100, "Riga 2: àèéìòù"}, {9900, "Riga 3"}, {19000, "Riga 4"}});
    auto r = rebuild(file, track);

    std::string why;
    auto original = read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why).value();
    auto before = clusters_of(file, original);
    std::vector<Cluster> video, subtitles;
    for (auto& c : r.clusters) {
        bool sub = !c.blocks.empty() && c.blocks.front().track == r.track;
        (sub ? subtitles : video).push_back(c);
    }
    ASSERT_EQ(video.size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(video[i].bytes, before[i].bytes) << "Cluster " << i << " copied byte for byte";
    }

    std::vector<Block> got;
    for (const auto& c : subtitles) got.insert(got.end(), c.blocks.begin(), c.blocks.end());
    ASSERT_EQ(got.size(), track.cues.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        EXPECT_EQ(got[i].time, track.cues[i].start_ms);
        EXPECT_EQ(got[i].duration, 1500);
        EXPECT_EQ(got[i].data, track.cues[i].text);
    }
}

TEST(MkvSubtitleLayout, TheNewTrackIsTheDefaultAndTheOldSubtitlesAreNot) {
    Synthetic with;
    with.with_subtitles = true;
    auto file = synthetic_mkv(with);
    auto r = rebuild(file, lines({{100, "x"}}));
    EXPECT_EQ(r.track, 3u);
    auto entries = ebml::children(r.structure.tracks.data(), r.structure.tracks.size());
    ASSERT_EQ(entries->size(), 3u);
    std::vector<std::uint64_t> defaults;
    for (const auto& e : *entries) {
        const auto* p = r.structure.tracks.data() + e.data;
        std::uint64_t type = 0, def = 1;
        auto fields = ebml::children(p, e.end - e.data);
        for (const auto& f : fields.value()) {
            if (f.id == ebml::kTrackType) type = *ebml::read_uint(p + f.data, f.end - f.data);
            if (f.id == ebml::kFlagDefault) def = *ebml::read_uint(p + f.data, f.end - f.data);
        }
        if (type == ebml::kSubtitleTrackType) defaults.push_back(def);
    }
    EXPECT_EQ(defaults, (std::vector<std::uint64_t>{0, 1}));
}

// A TV that finds, where a cue points, a Cluster timed after the cue drops
// the Cues and reads from the start at every jump (app DEC-700).
TEST(MkvSubtitleLayout, EveryCueNamesAClusterTimedNoLaterThanItself) {
    auto file = read_file(fixture());
    auto r = rebuild(file, lines({{700, "a"}, {2500, "b"}, {4100, "c"}, {12345, "d"}}));
    for (const auto& cue : r.structure.cues) {
        for (const auto& p : cue.positions) {
            auto it = std::find_if(r.clusters.begin(), r.clusters.end(),
                                   [&](const Cluster& c) { return c.start == p.cluster; });
            ASSERT_NE(it, r.clusters.end()) << "a cue names the start of a Cluster";
            EXPECT_LE(it->timestamp, static_cast<std::int64_t>(cue.time));
            if (!it->blocks.empty() && it->blocks.front().track == r.track) {
                auto next = it + 1;
                EXPECT_NE(next->blocks.front().track, r.track) << "the lines come just before the video";
            }
        }
    }
}

TEST(MkvSubtitleLayout, LinesFarFromAnyCueGetClustersOfTheirOwn) {
    Synthetic sparse;
    sparse.clusters = 2;
    sparse.cluster_ms = 60000;
    auto file = synthetic_mkv(sparse);
    auto r = rebuild(file, lines({{1000, "uno"}, {40000, "due"}, {61000, "tre"}}));
    std::vector<std::int64_t> times;
    for (const auto& c : r.clusters) {
        for (const auto& b : c.blocks) {
            if (b.track == r.track) times.push_back(b.time);
        }
    }
    EXPECT_EQ(times, (std::vector<std::int64_t>{1000, 40000, 61000}));
}

TEST(MkvSubtitleLayout, CuesBeforeTheClustersAndChecksumsAreRebuiltToo) {
    Synthetic front;
    front.cues_in_head = true;
    front.with_crc = true;
    auto file = synthetic_mkv(front);
    auto r = rebuild(file, lines({{100, "a"}, {2100, "b"}, {4100, "c"}}));
    EXPECT_LT(r.structure.cues_start, r.structure.first_cluster);
    int with_lines = 0;
    for (const auto& c : r.clusters) with_lines += c.blocks.front().track == r.track;
    EXPECT_EQ(with_lines, 3);
    for (const auto& cue : r.structure.cues) {
        auto h = ebml::read_header(r.file.data() + cue.positions.front().cluster, 12);
        EXPECT_EQ(h->id, ebml::kCluster);
    }
}

TEST(MkvSubtitleLayout, APassthroughIsTheFileAsItIs) {
    auto layout = passthrough_layout(1000);
    ASSERT_EQ(layout.pieces.size(), 1u);
    EXPECT_EQ(layout.pieces.front().file_offset, 0);
    EXPECT_EQ(layout.size, 1000);
    EXPECT_EQ(layout.piece_at(999), 0u);
    EXPECT_EQ(layout.piece_at(1000), 1u);
}

}  // namespace
}  // namespace seekserve
