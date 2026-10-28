#include "seekserve/mkv_subtitle_layout.hpp"

#include <algorithm>
#include <map>
#include <unordered_set>

#include "seekserve/ebml.hpp"

namespace seekserve {

namespace {

// Positions in the head are written this wide, so the head's length does not
// depend on them; also the Segment size.
constexpr int kWideNumber = 8;
// A Block's time is a signed 16-bit offset from its Cluster's.
constexpr std::int64_t kMaxBlockOffset = 32767;
constexpr std::uint64_t kTrackUidBase = 0x5EC0DE000000ULL;

std::int64_t to_units(std::int64_t ms, std::uint64_t scale) {
    auto s = static_cast<std::int64_t>(scale);
    return (ms * 1000000 + s / 2) / s;
}

// The Tracks payload with the subtitle track last and the only default
// subtitle track: an existing one is no longer the default.
std::vector<std::uint8_t> rebuilt_tracks(const std::vector<std::uint8_t>& tracks,
                                         const SubtitleTrack& track, std::uint64_t& number) {
    std::vector<std::uint8_t> out;
    std::unordered_set<std::uint64_t> uids;
    number = 0;
    auto entries = ebml::children(tracks.data(), tracks.size());  // read, so never empty
    for (const auto& entry : entries.value_or(std::vector<ebml::Child>{})) {
        if (entry.id != ebml::kTrackEntry) continue;  // CRC-32, Void
        const auto* p = tracks.data() + entry.data;
        auto fields = ebml::children(p, entry.end - entry.data);
        if (!fields) continue;
        std::uint64_t type = 0;
        for (const auto& f : *fields) {
            auto v = ebml::read_uint(p + f.data, f.end - f.data).value_or(0);
            if (f.id == ebml::kTrackNumber) number = std::max(number, v);
            if (f.id == ebml::kTrackUid) uids.insert(v);
            if (f.id == ebml::kTrackType) type = v;
        }
        if (type != ebml::kSubtitleTrackType) {
            out.insert(out.end(), tracks.begin() + entry.start, tracks.begin() + entry.end);
            continue;
        }
        std::vector<std::uint8_t> payload;
        for (const auto& f : *fields) {
            if (f.id != ebml::kFlagDefault) payload.insert(payload.end(), p + f.start, p + f.end);
        }
        ebml::append(payload, ebml::uint_element(ebml::kFlagDefault, 0));
        ebml::append(out, ebml::element(ebml::kTrackEntry, payload));
    }
    ++number;
    auto uid = kTrackUidBase + number;
    while (uids.count(uid)) ++uid;

    std::vector<std::uint8_t> entry;
    ebml::append(entry, ebml::uint_element(ebml::kTrackNumber, number));
    ebml::append(entry, ebml::uint_element(ebml::kTrackUid, uid));
    ebml::append(entry, ebml::uint_element(ebml::kTrackType, ebml::kSubtitleTrackType));
    ebml::append(entry, ebml::uint_element(ebml::kFlagDefault, 1));
    ebml::append(entry, ebml::uint_element(ebml::kFlagLacing, 0));
    ebml::append(entry, ebml::string_element(ebml::kCodecId, "S_TEXT/UTF8"));
    ebml::append(entry, ebml::string_element(ebml::kLanguage, track.language));
    if (!track.name.empty()) ebml::append(entry, ebml::string_element(ebml::kName, track.name));
    ebml::append(out, ebml::element(ebml::kTrackEntry, entry));
    return out;
}

// The Cluster of lines `lines` timed from `tc`.
std::vector<std::uint8_t> subtitle_cluster(std::int64_t tc, std::uint64_t track,
                                           const std::vector<std::pair<std::int64_t, const SubtitleCue*>>& lines,
                                           std::uint64_t scale) {
    std::vector<std::uint8_t> payload = ebml::uint_element(ebml::kTimestamp, static_cast<std::uint64_t>(tc));
    for (const auto& [start, cue] : lines) {
        std::vector<std::uint8_t> block;
        ebml::put_size(block, track);  // the track number is a variable-length number
        auto offset = static_cast<std::int16_t>(start - tc);
        block.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(offset) >> 8));
        block.push_back(static_cast<std::uint8_t>(offset & 0xFF));
        block.push_back(0x00);  // no lacing, no flags
        block.insert(block.end(), cue->text.begin(), cue->text.end());
        auto duration = std::max<std::int64_t>(1, to_units(cue->end_ms, scale) - start);
        std::vector<std::uint8_t> group = ebml::element(ebml::kBlock, block);
        ebml::append(group, ebml::uint_element(ebml::kBlockDuration, static_cast<std::uint64_t>(duration)));
        ebml::append(payload, ebml::element(ebml::kBlockGroup, group));
    }
    return ebml::element(ebml::kCluster, payload);
}

// The subtitle Clusters to put before each Cluster the Cues name, by the
// file position of that Cluster. The first one of a place is timed no later
// than its cue: a TV that finds a Cluster timed after the cue it followed
// drops the Cues and reads from the start at every jump (app DEC-700).
std::map<std::int64_t, std::vector<std::uint8_t>> subtitle_clusters(const MkvStructure& s,
                                                                    const SubtitleTrack& track,
                                                                    std::uint64_t number) {
    std::map<std::int64_t, std::int64_t> anchors;  // Cluster position -> first cue time
    for (const auto& cue : s.cues) {
        for (const auto& p : cue.positions) {
            auto t = static_cast<std::int64_t>(cue.time);
            auto [it, added] = anchors.emplace(p.cluster, t);
            if (!added) it->second = std::min(it->second, t);
        }
    }
    std::vector<std::pair<std::int64_t, std::int64_t>> places(anchors.begin(), anchors.end());
    if (places.empty()) return {};  // read_mkv_structure lets no such file through
    std::vector<std::int64_t> reached;  // never back in time, for the search below
    for (const auto& [pos, t] : places) reached.push_back(reached.empty() ? t : std::max(reached.back(), t));

    std::vector<std::vector<std::pair<std::int64_t, const SubtitleCue*>>> lines(places.size());
    for (const auto& cue : track.cues) {
        auto start = to_units(cue.start_ms, s.timestamp_scale);
        auto k = std::upper_bound(reached.begin(), reached.end(), start) - reached.begin();
        lines[k == 0 ? 0 : k - 1].emplace_back(start, &cue);
    }

    std::map<std::int64_t, std::vector<std::uint8_t>> out;
    for (std::size_t k = 0; k < places.size(); ++k) {
        if (lines[k].empty()) continue;
        std::vector<std::uint8_t> bytes;
        std::vector<std::pair<std::int64_t, const SubtitleCue*>> chunk;
        std::int64_t tc = std::min(places[k].second, lines[k].front().first);
        for (const auto& line : lines[k]) {
            if (line.first - tc > kMaxBlockOffset) {
                ebml::append(bytes, subtitle_cluster(tc, number, chunk, s.timestamp_scale));
                chunk.clear();
                tc = line.first;
            }
            chunk.push_back(line);
        }
        ebml::append(bytes, subtitle_cluster(tc, number, chunk, s.timestamp_scale));
        out[places[k].first] = std::move(bytes);
    }
    return out;
}

class Builder {
public:
    Builder(const MkvStructure& s, const SubtitleTrack& track) : s_(s), track_(track) {}

    MkvSubtitleLayout build();

private:
    std::int64_t new_position(std::int64_t old) const;
    std::vector<std::uint8_t> seek_head(const std::vector<MkvSeekEntry>& entries, bool real) const;
    // With `real` false only its length is right, and only with wide positions.
    std::vector<std::uint8_t> cues(bool real) const;
    std::int64_t new_size(const MkvElement& e, const std::vector<std::uint8_t>& tracks) const;
    void copy(std::int64_t from, std::int64_t len);
    void add(std::vector<std::uint8_t> bytes);

    const MkvStructure& s_;
    const SubtitleTrack& track_;
    MkvSubtitleLayout out_;
    std::map<std::int64_t, std::vector<std::uint8_t>> inserts_;
    // Per insert, in order of place: the bytes added up to it, included.
    std::vector<std::pair<std::int64_t, std::int64_t>> added_;
    std::map<std::int64_t, std::int64_t> starts_;  // head and tail element: old -> new start
    std::int64_t segment_data_ = 0;
    std::int64_t region_start_ = 0;
    int cue_width_ = 0;
};

std::int64_t Builder::new_position(std::int64_t old) const {
    if (auto it = starts_.find(old); it != starts_.end()) return it->second;
    if (old >= s_.first_cluster && old <= s_.clusters_end) {
        auto it = std::upper_bound(added_.begin(), added_.end(), old,
                                   [](std::int64_t o, const auto& a) { return o < a.first; });
        auto added = it == added_.begin() ? 0 : std::prev(it)->second;
        return region_start_ + (old - s_.first_cluster) + added;
    }
    return old;  // before the Segment: copied where it was
}

std::vector<std::uint8_t> Builder::seek_head(const std::vector<MkvSeekEntry>& entries, bool real) const {
    std::vector<std::uint8_t> payload;
    for (const auto& e : entries) {
        auto pos = real ? new_position(e.position) - segment_data_ : 0;
        std::vector<std::uint8_t> seek;
        std::vector<std::uint8_t> id;
        ebml::put_id(id, e.id);
        ebml::append(seek, ebml::element(ebml::kSeekId, id));
        ebml::append(seek, ebml::uint_element(ebml::kSeekPosition, static_cast<std::uint64_t>(pos), kWideNumber));
        ebml::append(payload, ebml::element(ebml::kSeek, seek));
    }
    return ebml::element(ebml::kSeekHead, payload);
}

std::vector<std::uint8_t> Builder::cues(bool real) const {
    std::vector<std::uint8_t> payload;
    for (const auto& cue : s_.cues) {
        std::vector<std::uint8_t> point = ebml::uint_element(ebml::kCueTime, cue.time);
        for (const auto& p : cue.positions) {
            // A cue whose Cluster has lines before it names their Cluster, so
            // a jump reads them too; a place inside the old Cluster no longer
            // fits it.
            auto insert = inserts_.find(p.cluster);
            bool moved = insert != inserts_.end();
            std::int64_t at = 0;
            if (real) {
                at = new_position(p.cluster) - segment_data_;
                if (moved) at -= static_cast<std::int64_t>(insert->second.size());
            }
            std::vector<std::uint8_t> tp = ebml::uint_element(ebml::kCueTrack, p.track);
            ebml::append(tp, ebml::uint_element(ebml::kCueClusterPosition,
                                                static_cast<std::uint64_t>(at), cue_width_));
            if (!moved) ebml::append(tp, p.relative);
            ebml::append(tp, p.rest);
            ebml::append(point, ebml::element(ebml::kCueTrackPositions, tp));
        }
        ebml::append(payload, ebml::element(ebml::kCuePoint, point));
    }
    return ebml::element(ebml::kCues, payload);
}

std::int64_t Builder::new_size(const MkvElement& e, const std::vector<std::uint8_t>& tracks) const {
    if (e.id == ebml::kSeekHead) return static_cast<std::int64_t>(seek_head(s_.seek_heads.at(e.start), false).size());
    if (e.id == ebml::kTracks) return static_cast<std::int64_t>(ebml::element(ebml::kTracks, tracks).size());
    if (e.id == ebml::kCues) return static_cast<std::int64_t>(cues(false).size());
    return e.end - e.start;
}

void Builder::copy(std::int64_t from, std::int64_t len) {
    if (len <= 0) return;
    out_.pieces.push_back(MkvPiece{out_.size, len, from, nullptr, 0});
    out_.size += len;
}

void Builder::add(std::vector<std::uint8_t> bytes) {
    if (bytes.empty()) return;
    auto len = static_cast<std::int64_t>(bytes.size());
    out_.pieces.push_back(MkvPiece{out_.size, len, -1,
                                   std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes)), 0});
    out_.size += len;
}

MkvSubtitleLayout Builder::build() {
    auto tracks = rebuilt_tracks(s_.tracks, track_, out_.track_number);
    inserts_ = subtitle_clusters(s_, track_, out_.track_number);
    std::int64_t total = 0;
    for (const auto& [at, bytes] : inserts_) {
        total += static_cast<std::int64_t>(bytes.size());
        added_.emplace_back(at, total);
    }
    // Cues in the head take wide positions, like the SeekHead; after the
    // Clusters nothing before them depends on their length.
    bool cues_in_head = s_.cues_start < s_.first_cluster;
    cue_width_ = cues_in_head ? kWideNumber : 0;

    // Where everything goes: the head, the Clusters with the lines, the tail.
    segment_data_ = s_.segment_start + 4 + kWideNumber;
    auto at = segment_data_;
    for (const auto& e : s_.head) { starts_[e.start] = at; at += new_size(e, tracks); }
    region_start_ = at;
    at = new_position(s_.clusters_end);
    for (const auto& e : s_.tail) {
        starts_[e.start] = at;
        at += e.id == ebml::kCues ? static_cast<std::int64_t>(cues(true).size()) : new_size(e, tracks);
    }
    auto segment_size = at - segment_data_;

    copy(0, s_.segment_start);
    std::vector<std::uint8_t> segment;
    ebml::put_id(segment, ebml::kSegment);
    ebml::put_size(segment, static_cast<std::uint64_t>(segment_size), kWideNumber);
    add(std::move(segment));
    auto emit = [&](const MkvElement& e) {
        if (e.id == ebml::kSeekHead) add(seek_head(s_.seek_heads.at(e.start), true));
        else if (e.id == ebml::kTracks) add(ebml::element(ebml::kTracks, tracks));
        else if (e.id == ebml::kCues) add(cues(true));
        else copy(e.start, e.end - e.start);
    };
    for (const auto& e : s_.head) emit(e);
    auto from = s_.first_cluster;
    for (auto& [pos, bytes] : inserts_) {
        copy(from, pos - from);
        add(bytes);
        from = pos;
    }
    copy(from, s_.clusters_end - from);
    for (const auto& e : s_.tail) emit(e);
    return out_;
}

}  // namespace

std::size_t MkvSubtitleLayout::piece_at(std::int64_t offset) const {
    auto it = std::upper_bound(pieces.begin(), pieces.end(), offset,
                               [](std::int64_t o, const MkvPiece& p) { return o < p.at; });
    if (it == pieces.begin()) return pieces.size();
    --it;
    return offset < it->at + it->length ? static_cast<std::size_t>(it - pieces.begin()) : pieces.size();
}

MkvSubtitleLayout build_mkv_subtitle_layout(const MkvStructure& structure, const SubtitleTrack& track) {
    return Builder(structure, track).build();
}

MkvSubtitleLayout passthrough_layout(std::int64_t file_size) {
    MkvSubtitleLayout layout;
    if (file_size > 0) layout.pieces.push_back(MkvPiece{0, file_size, 0, nullptr, 0});
    layout.size = file_size;
    return layout;
}

}  // namespace seekserve
