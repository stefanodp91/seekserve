// An MP4 served as a Matroska file with a subtitle track (app WORK-37 step
// 7d): read back as a whole file with the reader of the rebuilt MKVs, every
// sample of the MP4 is there as it is, at the time ffprobe gives it, the
// lines are at theirs, and every cue names a Cluster that starts with the
// keyframe it is for.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>

#include "mkv_test_support.hpp"
#include "seekserve/mp4_mkv_file.hpp"
#include "seekserve/mp4_structure.hpp"
#include "seekserve/subtitle_track.hpp"

namespace seekserve {
namespace {

using namespace mkvtest;

std::string fixture(const std::string& name) { return std::string(SEEKSERVE_FIXTURE_DIR) + "/" + name; }

SubtitleTrack lines(std::vector<std::pair<std::int64_t, std::string>> at) {
    SubtitleTrack t;
    t.language = "ita";
    t.name = "Italiano";
    for (auto& [ms, text] : at) t.cues.push_back({ms, ms + 1500, text});
    return checked_subtitle_track(t).value();
}

SubtitleTrack every_second() {
    std::vector<std::pair<std::int64_t, std::string>> at;
    for (int s = 0; s < 6; ++s) at.emplace_back(s * 1000 + 250, "RIGA " + std::to_string(s) + " àèé");
    return lines(at);
}

struct Rebuilt {
    Bytes mp4;
    Mp4Structure structure;
    std::shared_ptr<const Mp4MkvFile> file;
    Bytes mkv;
    MkvStructure mkv_structure;
    std::vector<Cluster> clusters;
};

// The bytes of `file`, piece after piece, from the MP4 and the new bytes.
Bytes assemble(const RebuiltFile& file, const Bytes& mp4) {
    Bytes out;
    while (static_cast<std::int64_t>(out.size()) < file.size()) {
        auto p = file.piece_at(static_cast<std::int64_t>(out.size()));
        EXPECT_TRUE(p);
        if (!p) break;
        EXPECT_EQ(p->at, static_cast<std::int64_t>(out.size()));
        EXPECT_GT(p->length, 0);
        if (p->file_offset >= 0) {
            out.insert(out.end(), mp4.begin() + p->file_offset, mp4.begin() + p->file_offset + p->length);
        } else {
            out.insert(out.end(), p->bytes->begin() + p->bytes_offset,
                       p->bytes->begin() + p->bytes_offset + p->length);
        }
    }
    EXPECT_FALSE(file.piece_at(file.size()));
    return out;
}

Rebuilt rebuild(const std::string& name, const SubtitleTrack& track) {
    Rebuilt r;
    r.mp4 = read_file(fixture(name));
    std::string why;
    auto s = read_mp4_structure(reader_of(r.mp4), static_cast<std::int64_t>(r.mp4.size()), why);
    EXPECT_TRUE(s) << why;
    r.structure = s.value();
    r.file = Mp4MkvFile::build(s.value(), track, why);
    EXPECT_TRUE(r.file) << why;
    if (!r.file) return r;
    r.mkv = assemble(*r.file, r.mp4);
    auto m = read_mkv_structure(reader_of(r.mkv), static_cast<std::int64_t>(r.mkv.size()), why);
    EXPECT_TRUE(m) << "the MKV reads back: " << why;
    if (!m) return r;
    r.mkv_structure = m.value();
    r.clusters = clusters_of(r.mkv, r.mkv_structure);
    return r;
}

std::vector<Block> blocks_of(const Rebuilt& r, std::uint64_t track) {
    std::vector<Block> out;
    for (const auto& c : r.clusters) {
        for (const auto& b : c.blocks) {
            if (b.track == track) out.push_back(b);
        }
    }
    return out;
}

// ffprobe's pts of a stream (the edit list applied), in its time base.
std::vector<std::int64_t> ffprobe_pts(const std::string& name, int stream) {
    std::ifstream in(fixture(name));
    std::vector<std::int64_t> out;
    std::string line;
    while (std::getline(in, line)) {
        std::stringstream fields(line);
        std::string f[2];
        for (auto& x : f) std::getline(fields, x, ',');
        if (std::stoi(f[0]) == stream) out.push_back(std::stoll(f[1]));
    }
    return out;
}

std::int64_t round_ms(double ms) { return static_cast<std::int64_t>(std::floor(ms + 0.5)); }

TEST(Mp4MkvFile, EverySampleIsThereAsItIsAtItsTime) {
    for (const auto* name : {"mp4_avc_aac.mp4", "mp4_hevc_ac3.mp4"}) {
        SCOPED_TRACE(name);
        auto r = rebuild(name, every_second());
        ASSERT_TRUE(r.file);
        std::string csv = std::string(name).substr(0, std::string(name).size() - 4) + ".packets.csv";
        // Matroska has no time before zero: everything moves by the earliest.
        double earliest = 0;
        for (std::size_t t = 0; t < r.structure.tracks.size(); ++t) {
            for (auto pts : ffprobe_pts(csv, static_cast<int>(t))) {
                earliest = std::min(earliest, 1000.0 * pts / r.structure.tracks[t].timescale);
            }
        }
        for (std::size_t t = 0; t < r.structure.tracks.size(); ++t) {
            const auto& track = r.structure.tracks[t];
            auto blocks = blocks_of(r, t + 1);
            auto pts = ffprobe_pts(csv, static_cast<int>(t));
            ASSERT_EQ(blocks.size(), track.sample_count);
            Mp4SampleCursor c(track);
            Mp4Sample s;
            std::map<std::int64_t, const Block*> by_time;
            for (const auto& b : blocks) by_time[b.time] = &b;
            for (std::size_t i = 0; c.next(s); ++i) {
                // Within a millisecond: the shift is rounded to the ns before
                // the times are, as ffmpeg rounds it before them (DEC-702).
                auto want = round_ms(1000.0 * pts[i] / track.timescale - earliest);
                auto it = by_time.find(want);
                if (it == by_time.end()) it = by_time.find(want - 1);
                if (it == by_time.end()) it = by_time.find(want + 1);
                ASSERT_NE(it, by_time.end()) << "track " << t << " sample " << i << " at " << want;
                std::string bytes(r.mp4.begin() + s.offset, r.mp4.begin() + s.offset + s.size);
                EXPECT_EQ(it->second->data, bytes) << "track " << t << " sample " << i;
                if (track.video) EXPECT_EQ(it->second->key, s.key) << "sample " << i;
            }
        }
    }
}

TEST(Mp4MkvFile, TheLinesAreAtTheirTimesMovedLikeTheSamples) {
    auto r = rebuild("mp4_avc_aac.mp4", every_second());
    ASSERT_TRUE(r.file);
    EXPECT_EQ(r.file->subtitle_track(), 3u);
    auto text = blocks_of(r, 3);
    ASSERT_EQ(text.size(), 6u);
    // The AAC's priming (1024 of 48000) comes before zero: 21 ms.
    for (std::size_t i = 0; i < text.size(); ++i) {
        EXPECT_EQ(text[i].time, static_cast<std::int64_t>(i) * 1000 + 250 + 21);
        EXPECT_EQ(text[i].duration, 1500);
        EXPECT_EQ(text[i].data, "RIGA " + std::to_string(i) + " àèé");
    }
}

TEST(Mp4MkvFile, TheSubtitleTrackIsTheDefaultTextTrack) {
    auto r = rebuild("mp4_avc_aac.mp4", every_second());
    ASSERT_TRUE(r.file);
    auto entries = ebml::children(r.mkv_structure.tracks.data(), r.mkv_structure.tracks.size());
    ASSERT_EQ(entries->size(), 3u);
    std::vector<std::uint64_t> types;
    std::uint64_t def = 0;
    for (const auto& e : *entries) {
        const auto* p = r.mkv_structure.tracks.data() + e.data;
        auto fields = ebml::children(p, e.end - e.data);
        std::uint64_t type = 0, d = 1;
        for (const auto& f : fields.value()) {
            if (f.id == ebml::kTrackType) type = *ebml::read_uint(p + f.data, f.end - f.data);
            if (f.id == ebml::kFlagDefault) d = *ebml::read_uint(p + f.data, f.end - f.data);
        }
        types.push_back(type);
        if (type == ebml::kSubtitleTrackType) def = d;
    }
    EXPECT_EQ(types, (std::vector<std::uint64_t>{1, 2, ebml::kSubtitleTrackType}));
    EXPECT_EQ(def, 1u);
}

// A TV that finds, where a cue points, a Cluster timed after the cue drops
// the Cues and reads from the start at every jump (app DEC-700).
TEST(Mp4MkvFile, EveryCueNamesAClusterStartingWithItsKeyframe) {
    auto r = rebuild("mp4_avc_aac.mp4", every_second());
    ASSERT_TRUE(r.file);
    ASSERT_GE(r.mkv_structure.cues.size(), 3u);  // a keyframe every 2 s
    for (const auto& cue : r.mkv_structure.cues) {
        ASSERT_EQ(cue.positions.size(), 1u);
        EXPECT_EQ(cue.positions[0].track, 1u);
        auto c = std::find_if(r.clusters.begin(), r.clusters.end(),
                              [&](const Cluster& x) { return x.start == cue.positions[0].cluster; });
        ASSERT_NE(c, r.clusters.end());
        EXPECT_LE(c->timestamp, static_cast<std::int64_t>(cue.time));
        auto first_video = std::find_if(c->blocks.begin(), c->blocks.end(),
                                        [](const Block& b) { return b.track == 1; });
        ASSERT_NE(first_video, c->blocks.end());
        EXPECT_TRUE(first_video->key);
        EXPECT_EQ(first_video->time, static_cast<std::int64_t>(cue.time));
    }
}

TEST(Mp4MkvFile, TheIndexAtTheHeadOrAtTheEndMakesTheSameFile) {
    auto end = rebuild("mp4_avc_aac.mp4", every_second());
    auto head = rebuild("mp4_avc_aac_faststart.mp4", every_second());
    ASSERT_TRUE(end.file && head.file);
    EXPECT_EQ(end.mkv, head.mkv);
}

// The pieces read in any order, as a TV jumping around asks for them, are
// the same as read in a row (the Clusters laid out again when read).
TEST(Mp4MkvFile, PiecesReadInAnyOrderAreTheSame) {
    auto r = rebuild("mp4_avc_aac.mp4", every_second());
    ASSERT_TRUE(r.file);
    EXPECT_GT(r.file->cluster_count(), 2u);
    std::mt19937 rng(5);
    for (int i = 0; i < 2000; ++i) {
        auto offset = static_cast<std::int64_t>(rng() % r.mkv.size());
        auto p = r.file->piece_at(offset);
        ASSERT_TRUE(p);
        ASSERT_LE(p->at, offset);
        ASSERT_GT(p->at + p->length, offset);
        auto from = offset - p->at;
        std::uint8_t got = p->file_offset >= 0 ? r.mp4[p->file_offset + from] : (*p->bytes)[p->bytes_offset + from];
        ASSERT_EQ(got, r.mkv[offset]) << "byte " << offset;
    }
}

// For the scheduler, new bytes stand for the sample that follows them; the
// head for the first one, the Cues for none.
TEST(Mp4MkvFile, NewBytesStandForTheSampleAfterThem) {
    auto r = rebuild("mp4_avc_aac.mp4", every_second());
    ASSERT_TRUE(r.file);
    Mp4SampleCursor c(r.structure.tracks[0]);
    Mp4Sample first;
    ASSERT_TRUE(c.next(first));
    std::int64_t lowest = first.offset;
    Mp4SampleCursor a(r.structure.tracks[1]);
    Mp4Sample s;
    if (a.next(s)) lowest = std::min(lowest, s.offset);
    EXPECT_EQ(r.file->file_offset(0), lowest);
    EXPECT_EQ(r.file->file_offset(r.file->size() - 1), -1);
    for (std::int64_t at = 0; at < r.file->size();) {
        auto p = r.file->piece_at(at);
        if (p->file_offset >= 0) {
            EXPECT_EQ(r.file->file_offset(at + p->length / 2), p->file_offset + p->length / 2);
        }
        at += p->length;
    }
}

// A Cluster holds at most so many blocks, whatever the times say: a hostile
// file whose samples share one time cannot make one Cluster of them all.
TEST(Mp4MkvFile, AClusterHoldsAtMostSoManyBlocks) {
    auto mp4 = read_file(fixture("mp4_avc_aac.mp4"));
    std::string why;
    auto s = read_mp4_structure(reader_of(mp4), static_cast<std::int64_t>(mp4.size()), why).value();
    auto file = Mp4MkvFile::build(s, every_second(), why, 16);
    ASSERT_TRUE(file) << why;
    auto mkv = assemble(*file, mp4);
    auto m = read_mkv_structure(reader_of(mkv), static_cast<std::int64_t>(mkv.size()), why);
    ASSERT_TRUE(m) << why;
    auto clusters = clusters_of(mkv, m.value());
    std::size_t blocks = 0;
    for (const auto& c : clusters) {
        EXPECT_LE(c.blocks.size(), 16u);
        blocks += c.blocks.size();
    }
    EXPECT_EQ(blocks, s.tracks[0].sample_count + s.tracks[1].sample_count + 6);
    EXPECT_GE(file->cluster_count(), blocks / 16);
}

TEST(Mp4MkvFile, AnMp4WithoutLinesAfterTheVideoKeepsAllItsSamples) {
    auto r = rebuild("mp4_avc_aac.mp4", lines({{3600000, "after the end"}}));
    ASSERT_TRUE(r.file);
    EXPECT_EQ(blocks_of(r, 1).size(), r.structure.tracks[0].sample_count);
    EXPECT_EQ(blocks_of(r, 3).size(), 1u);
}

}  // namespace
}  // namespace seekserve
