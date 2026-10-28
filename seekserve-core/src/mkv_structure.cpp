#include "seekserve/mkv_structure.hpp"

#include <algorithm>
#include <optional>

#include "seekserve/ebml.hpp"

namespace seekserve {

namespace {

constexpr std::int64_t kWindow = 64 * 1024;
constexpr std::int64_t kMaxSmallElement = 64 * 1024;   // SeekHead, Info, EBML head
constexpr std::int64_t kMaxTracks = 1024 * 1024;
constexpr std::int64_t kMaxCues = 16 * 1024 * 1024;
constexpr int kMaxTopElements = 1024;
// A film has a few thousand cue points and a handful of tracks.
constexpr std::size_t kMaxCuePositions = 200000;
constexpr std::size_t kMaxTrackEntries = 128;
constexpr std::uint32_t kDocType = 0x4282;
constexpr std::uint32_t kCueCodecState = 0xEA;
constexpr std::uint32_t kCueReference = 0xDB;

class Reader {
public:
    Reader(const MkvRead& read, std::int64_t file_size, std::string& why)
        : read_(read), size_(file_size), why_(why) {}

    std::error_code fail(const char* reason) {
        why_ = reason;
        return make_error_code(errc::invalid_argument);
    }

    // [offset, offset+len), all of it: a window read once serves the
    // element heads that follow.
    Result<std::vector<std::uint8_t>> bytes(std::int64_t offset, std::int64_t len) {
        if (offset < 0 || len < 0 || offset + len > size_) return fail("an element past the end of the file");
        if (offset >= win_start_ && offset + len <= win_start_ + static_cast<std::int64_t>(win_.size())) {
            auto from = win_.begin() + (offset - win_start_);
            return std::vector<std::uint8_t>(from, from + len);
        }
        auto want = std::max(len, std::min(kWindow, size_ - offset));
        auto got = read_(offset, want);
        if (!got) return got.error();
        if (static_cast<std::int64_t>(got.value().size()) < len) return fail("the file ended early");
        win_start_ = offset;
        win_ = std::move(got).value();
        return std::vector<std::uint8_t>(win_.begin(), win_.begin() + len);
    }

    // The element whose head is at `offset`, inside [offset, limit).
    Result<MkvElement> element(std::int64_t offset, std::int64_t limit) {
        auto head = bytes(offset, std::min<std::int64_t>(12, limit - offset));
        if (!head) return head.error();
        auto h = ebml::read_header(head.value().data(), head.value().size());
        if (!h) return fail("a malformed element");
        if (h->unknown_size) return fail("an element of unknown size (a live stream)");
        auto data = offset + static_cast<std::int64_t>(h->length);
        if (h->size > static_cast<std::uint64_t>(limit - data)) return fail("an element past its parent");
        return MkvElement{h->id, offset, data, data + static_cast<std::int64_t>(h->size)};
    }

    Result<std::vector<std::uint8_t>> payload(const MkvElement& e, std::int64_t max) {
        if (e.end - e.data > max) return fail("an element too large to rebuild");
        return bytes(e.data, e.end - e.data);
    }

private:
    const MkvRead& read_;
    const std::int64_t size_;
    std::string& why_;
    std::int64_t win_start_ = 0;
    std::vector<std::uint8_t> win_;
};

std::optional<std::vector<MkvSeekEntry>> seek_entries(const std::vector<std::uint8_t>& p,
                                                      std::int64_t segment_data) {
    auto seeks = ebml::children(p.data(), p.size());
    if (!seeks) return std::nullopt;
    std::vector<MkvSeekEntry> out;
    for (const auto& seek : *seeks) {
        if (seek.id != ebml::kSeek) continue;  // CRC-32, Void
        auto fields = ebml::children(p.data() + seek.data, seek.end - seek.data);
        if (!fields) return std::nullopt;
        std::optional<std::uint64_t> id, pos;
        for (const auto& f : *fields) {
            const auto* d = p.data() + seek.data + f.data;
            if (f.id == ebml::kSeekId) id = ebml::read_uint(d, f.end - f.data);
            if (f.id == ebml::kSeekPosition) pos = ebml::read_uint(d, f.end - f.data);
        }
        if (!id || !pos) return std::nullopt;
        out.push_back({static_cast<std::uint32_t>(*id), segment_data + static_cast<std::int64_t>(*pos)});
    }
    return out;
}

std::optional<std::vector<MkvCuePoint>> cue_points(const std::vector<std::uint8_t>& p,
                                                   std::int64_t segment_data) {
    auto points = ebml::children(p.data(), p.size());
    if (!points) return std::nullopt;
    std::vector<MkvCuePoint> out;
    std::size_t positions = 0;
    for (const auto& point : *points) {
        if (point.id != ebml::kCuePoint) continue;  // CRC-32: the rebuilt Cues have none
        auto fields = ebml::children(p.data() + point.data, point.end - point.data);
        if (!fields) return std::nullopt;
        MkvCuePoint cue;
        bool timed = false;
        for (const auto& f : *fields) {
            const auto* d = p.data() + point.data;
            if (f.id == ebml::kCueTime) {
                auto t = ebml::read_uint(d + f.data, f.end - f.data);
                if (!t) return std::nullopt;
                cue.time = *t;
                timed = true;
            }
            if (f.id != ebml::kCueTrackPositions) continue;
            auto parts = ebml::children(d + f.data, f.end - f.data);
            if (!parts) return std::nullopt;
            MkvCuePoint::Position pos;
            std::optional<std::uint64_t> track, cluster;
            for (const auto& part : *parts) {
                const auto* b = d + f.data + part.start;
                const auto* v = d + f.data + part.data;
                std::vector<std::uint8_t> raw(b, d + f.data + part.end);
                if (part.id == ebml::kCueTrack) track = ebml::read_uint(v, part.end - part.data);
                else if (part.id == ebml::kCueClusterPosition) cluster = ebml::read_uint(v, part.end - part.data);
                else if (part.id == ebml::kCueRelativePosition || part.id == ebml::kCueBlockNumber)
                    ebml::append(pos.relative, raw);
                else if (part.id != kCueCodecState && part.id != kCueReference)
                    ebml::append(pos.rest, raw);  // CueDuration: a place in time, not in the file
            }
            if (!track || !cluster || ++positions > kMaxCuePositions) return std::nullopt;
            pos.track = *track;
            pos.cluster = segment_data + static_cast<std::int64_t>(*cluster);
            cue.positions.push_back(std::move(pos));
        }
        if (!timed) return std::nullopt;
        if (!cue.positions.empty()) out.push_back(std::move(cue));
    }
    return out;
}

}  // namespace

Result<MkvStructure> read_mkv_structure(const MkvRead& read, std::int64_t file_size,
                                        std::string& why) {
    Reader r(read, file_size, why);
    MkvStructure s;
    s.file_size = file_size;

    // The EBML head says what kind of file this is; its id comes first.
    auto magic = r.bytes(0, std::min<std::int64_t>(4, file_size));
    if (!magic) return magic.error();
    if (magic.value().size() < 4 || magic.value()[0] != 0x1A || magic.value()[1] != 0x45 ||
        magic.value()[2] != 0xDF || magic.value()[3] != 0xA3) {
        return r.fail("not a Matroska file");
    }
    auto ebml = r.element(0, file_size);
    if (!ebml) return ebml.error();
    if (ebml.value().id != ebml::kEbml) return r.fail("not a Matroska file");
    auto ebml_payload = r.payload(ebml.value(), kMaxSmallElement);
    if (!ebml_payload) return ebml_payload.error();
    auto ebml_fields = ebml::children(ebml_payload.value().data(), ebml_payload.value().size());
    if (!ebml_fields) return r.fail("a malformed EBML head");
    std::string doc_type;
    for (const auto& f : *ebml_fields) {
        if (f.id == kDocType) doc_type.assign(ebml_payload.value().begin() + f.data,
                                              ebml_payload.value().begin() + f.end);
    }
    if (doc_type != "matroska" && doc_type != "webm") return r.fail("not a Matroska file");

    auto segment = r.element(ebml.value().end, file_size);
    if (!segment) return segment.error();
    if (segment.value().id != ebml::kSegment) return r.fail("no Segment after the EBML head");
    s.segment_start = segment.value().start;
    s.segment_data = segment.value().data;
    s.segment_end = segment.value().end;

    auto parse_seek_head = [&](const MkvElement& e) -> Result<void> {
        auto p = r.payload(e, kMaxSmallElement);
        if (!p) return p.error();
        auto entries = seek_entries(p.value(), s.segment_data);
        if (!entries) return r.fail("a malformed SeekHead");
        s.seek_heads[e.start] = std::move(*entries);
        return {};
    };
    auto parse_cues = [&](const MkvElement& e) -> Result<void> {
        if (!s.cues.empty() || s.cues_start != 0) return r.fail("two Cues");
        auto p = r.payload(e, kMaxCues);
        if (!p) return p.error();
        auto points = cue_points(p.value(), s.segment_data);
        if (!points) return r.fail("malformed or too large Cues");
        s.cues = std::move(*points);
        s.cues_start = e.start;
        return {};
    };
    auto parse_known = [&](const MkvElement& e) -> Result<void> {
        if (e.id == ebml::kSeekHead) return parse_seek_head(e);
        if (e.id == ebml::kCues) return parse_cues(e);
        if (e.id == ebml::kInfo) {
            auto p = r.payload(e, kMaxSmallElement);
            if (!p) return p.error();
            auto fields = ebml::children(p.value().data(), p.value().size());
            if (!fields) return r.fail("a malformed Info");
            for (const auto& f : *fields) {
                if (f.id != ebml::kTimestampScale) continue;
                auto scale = ebml::read_uint(p.value().data() + f.data, f.end - f.data);
                if (!scale || *scale == 0) return r.fail("a malformed TimestampScale");
                s.timestamp_scale = *scale;
            }
        }
        if (e.id == ebml::kTracks) {
            if (!s.tracks.empty()) return r.fail("two Tracks");
            auto p = r.payload(e, kMaxTracks);
            if (!p) return p.error();
            auto entries = ebml::children(p.value().data(), p.value().size());
            if (!entries || entries->empty() || entries->size() > kMaxTrackEntries) {
                return r.fail("malformed Tracks");
            }
            s.tracks = std::move(p).value();
        }
        return {};
    };

    // The head: every top-level element up to the first Cluster.
    std::int64_t pos = s.segment_data;
    for (int n = 0;; ++n) {
        if (pos >= s.segment_end || n >= kMaxTopElements) return r.fail("no Cluster");
        auto e = r.element(pos, s.segment_end);
        if (!e) return e.error();
        if (e.value().id == ebml::kCluster) break;
        if (auto known = parse_known(e.value()); !known) return known.error();
        s.head.push_back(e.value());
        pos = e.value().end;
    }
    s.first_cluster = pos;
    if (s.tracks.empty()) return r.fail("no Tracks");

    // After the Clusters: the first element a SeekHead points to past them.
    s.clusters_end = s.segment_end;
    for (const auto& [at, entries] : s.seek_heads) {
        for (const auto& entry : entries) {
            if (entry.id != ebml::kCluster && entry.position > s.first_cluster) {
                s.clusters_end = std::min(s.clusters_end, entry.position);
            }
        }
    }
    pos = s.clusters_end;
    for (int n = 0; pos < s.segment_end; ++n) {
        if (n >= kMaxTopElements) return r.fail("too many elements after the Clusters");
        auto e = r.element(pos, s.segment_end);
        if (!e) return e.error();
        if (e.value().id == ebml::kCluster) return r.fail("Clusters after the Cues");
        if (auto known = parse_known(e.value()); !known) return known.error();
        s.tail.push_back(e.value());
        pos = e.value().end;
    }
    if (s.cues.empty()) return r.fail("no Cues");
    // Only the cues that name a place among the Clusters count: the rebuilt
    // file puts its lines before those places, and needs at least one.
    bool named = false;
    for (auto& cue : s.cues) {
        auto& p = cue.positions;
        p.erase(std::remove_if(p.begin(), p.end(), [&](const MkvCuePoint::Position& pos) {
                    return pos.cluster < s.first_cluster || pos.cluster >= s.clusters_end;
                }),
                p.end());
        named = named || !p.empty();
    }
    if (!named) return r.fail("no Cues naming a Cluster");
    return s;
}

}  // namespace seekserve
