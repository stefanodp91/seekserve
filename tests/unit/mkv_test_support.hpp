// Helpers for the tests of the rebuilt Matroska file (app WORK-37 step 7):
// a small Matroska file written here, and a reader of the Clusters and
// Blocks of a whole file, built only on the EBML primitives.
#pragma once

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "seekserve/ebml.hpp"
#include "seekserve/mkv_structure.hpp"

namespace seekserve::mkvtest {

using Bytes = std::vector<std::uint8_t>;

inline Bytes read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

inline MkvRead reader_of(const Bytes& file) {
    return [&file](std::int64_t offset, std::int64_t len) -> Result<std::vector<std::uint8_t>> {
        auto end = std::min<std::int64_t>(offset + len, static_cast<std::int64_t>(file.size()));
        return Bytes(file.begin() + offset, file.begin() + std::max(offset, end));
    };
}

struct Block {
    std::uint64_t track;
    std::int64_t time;  // absolute, in TimestampScale units
    std::int64_t duration;
    std::string data;
};

struct Cluster {
    std::int64_t start;  // in the file
    std::int64_t timestamp;
    Bytes bytes;         // the whole element
    std::vector<Block> blocks;
};

// Every Cluster of the Segment of `file`, in order.
inline std::vector<Cluster> clusters_of(const Bytes& file, const MkvStructure& s) {
    std::vector<Cluster> out;
    auto top = ebml::children(file.data() + s.segment_data, s.segment_end - s.segment_data);
    if (!top) return out;
    for (const auto& e : *top) {
        if (e.id != ebml::kCluster) continue;
        const auto* p = file.data() + s.segment_data;
        Cluster c{s.segment_data + static_cast<std::int64_t>(e.start), 0,
                  Bytes(p + e.start, p + e.end), {}};
        auto parts = ebml::children(p + e.data, e.end - e.data);
        for (const auto& part : *parts) {
            const auto* d = p + e.data + part.data;
            auto n = part.end - part.data;
            if (part.id == ebml::kTimestamp) c.timestamp = static_cast<std::int64_t>(*ebml::read_uint(d, n));
            const std::uint8_t* block = nullptr;
            std::size_t block_len = 0;
            std::int64_t duration = 0;
            if (part.id == 0xA3) { block = d; block_len = n; }  // SimpleBlock
            if (part.id == ebml::kBlockGroup) {
                auto group = ebml::children(d, n);
                for (const auto& g : group.value()) {
                    if (g.id == ebml::kBlock) { block = d + g.data; block_len = g.end - g.data; }
                    if (g.id == ebml::kBlockDuration)
                        duration = static_cast<std::int64_t>(*ebml::read_uint(d + g.data, g.end - g.data));
                }
            }
            if (!block) continue;
            std::uint64_t track = block[0] & 0x7F;  // tracks below 127
            auto rel = static_cast<std::int16_t>((block[1] << 8) | block[2]);
            c.blocks.push_back(Block{track, c.timestamp + rel, duration,
                                     std::string(block + 4, block + block_len)});
        }
        out.push_back(std::move(c));
    }
    return out;
}

// A small Matroska file: one video track (and a subtitle track when
// `with_subtitles`), a Cluster every `cluster_ms` with one SimpleBlock, a
// Cues point for each Cluster, and the Cues after the Clusters or before
// them. Timestamps in milliseconds.
struct Synthetic {
    int clusters = 4;
    std::int64_t cluster_ms = 2000;
    bool with_subtitles = false;
    bool cues_in_head = false;
    bool with_cues = true;
    bool with_crc = false;
    int extra_tracks = 0;  // more audio tracks
};

inline Bytes synthetic_mkv(const Synthetic& o) {
    using namespace ebml;
    Bytes ebml_head;
    append(ebml_head, string_element(0x4282, "matroska"));
    Bytes file = element(kEbml, ebml_head);

    Bytes info = uint_element(kTimestampScale, 1000000);
    Bytes tracks;
    Bytes video = uint_element(kTrackNumber, 1);
    append(video, uint_element(kTrackUid, 11));
    append(video, uint_element(kTrackType, 1));
    append(video, string_element(kCodecId, "V_TEST"));
    append(tracks, element(kTrackEntry, video));
    if (o.with_subtitles) {
        Bytes sub = uint_element(kTrackNumber, 2);
        append(sub, uint_element(kTrackUid, 12));
        append(sub, uint_element(kTrackType, kSubtitleTrackType));
        append(sub, uint_element(kFlagDefault, 1));
        append(sub, string_element(kCodecId, "S_TEXT/UTF8"));
        append(tracks, element(kTrackEntry, sub));
    }
    for (int i = 0; i < o.extra_tracks; ++i) {
        Bytes audio = uint_element(kTrackNumber, static_cast<std::uint64_t>(10 + i));
        append(audio, uint_element(kTrackUid, static_cast<std::uint64_t>(100 + i)));
        append(audio, uint_element(kTrackType, 2));
        append(tracks, element(kTrackEntry, audio));
    }
    if (o.with_crc) tracks.insert(tracks.begin(), {0xBF, 0x84, 1, 2, 3, 4});

    std::vector<Bytes> clusters;
    for (int i = 0; i < o.clusters; ++i) {
        Bytes c = uint_element(kTimestamp, static_cast<std::uint64_t>(i * o.cluster_ms));
        Bytes block = {0x81, 0x00, 0x00, 0x80};
        for (int j = 0; j < 300; ++j) block.push_back(static_cast<std::uint8_t>(i * 31 + j));
        append(c, element(0xA3, block));
        clusters.push_back(element(kCluster, c));
    }
    // Every position is 8 bytes wide, so the lengths are known first.
    auto cues_with = [&](std::int64_t first_cluster_rel) {
        Bytes points;
        auto at = first_cluster_rel;
        for (int i = 0; i < o.clusters; ++i) {
            Bytes tp = uint_element(kCueTrack, 1);
            append(tp, uint_element(kCueClusterPosition, static_cast<std::uint64_t>(at), 8));
            Bytes point = uint_element(kCueTime, static_cast<std::uint64_t>(i * o.cluster_ms));
            append(point, element(kCueTrackPositions, tp));
            append(points, element(kCuePoint, point));
            at += static_cast<std::int64_t>(clusters[i].size());
        }
        return element(kCues, points);
    };
    auto seek_head_with = [&](std::int64_t info_at, std::int64_t tracks_at, std::int64_t cues_at) {
        Bytes p;
        auto seek = [&](std::uint32_t id, std::int64_t at) {
            Bytes sid;
            put_id(sid, id);
            Bytes s = element(kSeekId, sid);
            append(s, uint_element(kSeekPosition, static_cast<std::uint64_t>(at), 8));
            append(p, element(kSeek, s));
        };
        seek(kInfo, info_at);
        seek(kTracks, tracks_at);
        if (o.with_cues) seek(kCues, cues_at);
        if (o.with_crc) p.insert(p.begin(), {0xBF, 0x84, 9, 9, 9, 9});
        return element(kSeekHead, p);
    };

    Bytes info_el = element(kInfo, info), tracks_el = element(kTracks, tracks);
    auto seek_len = static_cast<std::int64_t>(seek_head_with(0, 0, 0).size());
    auto cues_len = static_cast<std::int64_t>(cues_with(0).size());
    std::int64_t info_at = seek_len, tracks_at = info_at + static_cast<std::int64_t>(info_el.size());
    std::int64_t head_end = tracks_at + static_cast<std::int64_t>(tracks_el.size());
    std::int64_t clusters_len = 0;
    for (const auto& c : clusters) clusters_len += static_cast<std::int64_t>(c.size());
    std::int64_t cues_at = o.cues_in_head ? head_end : head_end + clusters_len;
    std::int64_t first_cluster = o.cues_in_head && o.with_cues ? head_end + cues_len : head_end;

    Bytes segment = seek_head_with(info_at, tracks_at, cues_at);
    append(segment, info_el);
    append(segment, tracks_el);
    if (o.with_cues && o.cues_in_head) append(segment, cues_with(first_cluster));
    for (const auto& c : clusters) append(segment, c);
    if (o.with_cues && !o.cues_in_head) append(segment, cues_with(first_cluster));
    append(file, element(kSegment, segment, 8));
    return file;
}

}  // namespace seekserve::mkvtest
