// The plan of an MP4 served as a Matroska file (app WORK-37 step 7d): the
// head, where every Cluster starts and what it holds, and the Cues.

#include <algorithm>
#include <cstring>

#include "mp4_mkv_events.hpp"
#include "seekserve/ebml.hpp"
#include "seekserve/mp4_mkv_file.hpp"

namespace seekserve {

namespace {

using mp4mkv::Event;
using mp4mkv::EventWalker;

// A block's time is a signed 16-bit offset from its Cluster's.
constexpr std::int64_t kMaxClusterSpanMs = 30000;
// Keyframes closer than this share a Cluster: an all-intra video does not
// make a Cluster, and a cue, per frame.
constexpr std::int64_t kMinKeySpacingMs = 1000;
constexpr std::int64_t kMaxTimeNs = 48LL * 3600 * 1000 * mp4mkv::kNsPerMs;
constexpr std::uint64_t kMaxTotalSamples = 8000000;
constexpr std::uint64_t kTrackUidBase = 0x5EC0DE000000ULL;
constexpr int kWideNumber = 8;

enum : std::uint32_t {
    kEbmlVersion = 0x4286,
    kEbmlReadVersion = 0x42F7,
    kEbmlMaxIdLength = 0x42F2,
    kEbmlMaxSizeLength = 0x42F3,
    kDocType = 0x4282,
    kDocTypeVersion = 0x4287,
    kDocTypeReadVersion = 0x4285,
    kDuration = 0x4489,
    kMuxingApp = 0x4D80,
    kWritingApp = 0x5741,
    kCodecPrivate = 0x63A2,
    kVideo = 0xE0,
    kPixelWidth = 0xB0,
    kPixelHeight = 0xBA,
    kAudio = 0xE1,
    kSamplingFrequency = 0xB5,
    kChannels = 0x9F,
};

std::size_t vint_length(std::uint64_t value) {
    std::vector<std::uint8_t> v;
    ebml::put_size(v, value);
    return v.size();
}

std::vector<std::uint8_t> float_element(std::uint32_t id, double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    std::vector<std::uint8_t> payload;
    for (int i = 7; i >= 0; --i) payload.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    return ebml::element(id, payload);
}

const char* codec_id(Mp4Codec c) {
    switch (c) {
        case Mp4Codec::avc: return "V_MPEG4/ISO/AVC";
        case Mp4Codec::hevc: return "V_MPEGH/ISO/HEVC";
        case Mp4Codec::aac: return "A_AAC";
        case Mp4Codec::mp3: return "A_MPEG/L3";
        case Mp4Codec::ac3: return "A_AC3";
        case Mp4Codec::eac3: return "A_EAC3";
    }
    return "";
}

}  // namespace

namespace mp4mkv {

std::int64_t block_size(const Event& e, const SubtitleTrack& lines) {
    if (!e.text) {
        auto payload = 4 + static_cast<std::uint64_t>(e.sample.size);
        return static_cast<std::int64_t>(1 + vint_length(payload) + payload);
    }
    auto text = 4 + static_cast<std::uint64_t>(lines.cues[e.line].text.size());
    auto block = 1 + vint_length(text) + text;
    auto duration = ebml::uint_element(ebml::kBlockDuration,
                                       static_cast<std::uint64_t>(std::max<std::int64_t>(1, e.end_ms - e.pts_ms)));
    auto group = block + duration.size();
    return static_cast<std::int64_t>(1 + vint_length(group) + group);
}

}  // namespace mp4mkv

class Mp4MkvPlanner {
public:
    Mp4MkvPlanner(Mp4MkvFile& f, std::string& why) : f_(f), why_(why) {}

    bool plan() {
        if (!times()) return false;
        for (std::size_t t = 0; t < f_.mp4_.tracks.size(); ++t) {
            if (f_.mp4_.tracks[t].video) {
                video_ = t;
                break;
            }
        }
        f_.subtitle_number_ = f_.mp4_.tracks.size() + 1;
        auto first = head(0);
        segment_data_ = static_cast<std::int64_t>(ebml_.size()) + 4 + kWideNumber;
        clusters(static_cast<std::int64_t>(first.size()));
        auto cues = cues_element();
        f_.cues_ = std::make_shared<const std::vector<std::uint8_t>>(cues);
        f_.size_ = f_.cues_at_ + static_cast<std::int64_t>(cues.size());
        auto real = head(f_.cues_at_ - segment_data_);
        f_.head_ = std::make_shared<const std::vector<std::uint8_t>>(std::move(real));
        return true;
    }

private:
    bool fail(const char* reason) {
        why_ = reason;
        return false;
    }

    // The earliest and latest times, every one within two days.
    bool times() {
        std::uint64_t total = 0;
        std::int64_t earliest = 0, latest = 0;
        for (const auto& t : f_.mp4_.tracks) {
            total += t.sample_count;
            if (total > kMaxTotalSamples) return fail("too many samples");
            Mp4SampleCursor c(t);
            Mp4Sample s;
            while (c.next(s)) {
                auto pts = t.presentation_ns(s.pts);
                auto dts = t.presentation_ns(s.dts);
                if (std::max(std::abs(pts), std::abs(dts)) > kMaxTimeNs) return fail("times out of range");
                earliest = std::min(earliest, pts);
                latest = std::max(latest, pts);
            }
        }
        f_.shift_ns_ = -earliest;
        duration_ms_ = mp4mkv::to_ms(latest + f_.shift_ns_);
        return true;
    }

    std::vector<std::uint8_t> track_entries() const {
        std::vector<std::uint8_t> out;
        for (std::size_t i = 0; i < f_.mp4_.tracks.size(); ++i) {
            const auto& t = f_.mp4_.tracks[i];
            std::vector<std::uint8_t> e;
            ebml::append(e, ebml::uint_element(ebml::kTrackNumber, i + 1));
            ebml::append(e, ebml::uint_element(ebml::kTrackUid, kTrackUidBase + i + 1));
            ebml::append(e, ebml::uint_element(ebml::kTrackType, t.video ? 1 : 2));
            ebml::append(e, ebml::uint_element(ebml::kFlagLacing, 0));
            ebml::append(e, ebml::string_element(ebml::kCodecId, codec_id(t.codec)));
            if (!t.codec_private.empty()) ebml::append(e, ebml::element(kCodecPrivate, t.codec_private));
            ebml::append(e, ebml::string_element(ebml::kLanguage, t.language));
            std::vector<std::uint8_t> media;
            if (t.video) {
                ebml::append(media, ebml::uint_element(kPixelWidth, t.width));
                ebml::append(media, ebml::uint_element(kPixelHeight, t.height));
                ebml::append(e, ebml::element(kVideo, media));
            } else {
                ebml::append(media, float_element(kSamplingFrequency, t.sample_rate));
                ebml::append(media, ebml::uint_element(kChannels, t.channels));
                ebml::append(e, ebml::element(kAudio, media));
            }
            ebml::append(out, ebml::element(ebml::kTrackEntry, e));
        }
        // The lines: the only text track, the default one.
        auto n = f_.subtitle_number_;
        std::vector<std::uint8_t> e;
        ebml::append(e, ebml::uint_element(ebml::kTrackNumber, n));
        ebml::append(e, ebml::uint_element(ebml::kTrackUid, kTrackUidBase + n));
        ebml::append(e, ebml::uint_element(ebml::kTrackType, ebml::kSubtitleTrackType));
        ebml::append(e, ebml::uint_element(ebml::kFlagDefault, 1));
        ebml::append(e, ebml::uint_element(ebml::kFlagLacing, 0));
        ebml::append(e, ebml::string_element(ebml::kCodecId, "S_TEXT/UTF8"));
        ebml::append(e, ebml::string_element(ebml::kLanguage, f_.track_.language));
        if (!f_.track_.name.empty()) ebml::append(e, ebml::string_element(ebml::kName, f_.track_.name));
        ebml::append(out, ebml::element(ebml::kTrackEntry, e));
        return out;
    }

    // EBML header, Segment head, SeekHead, Info and Tracks, with the Cues at
    // `cues_position` (from the Segment's payload). Its length does not
    // depend on the position, written wide.
    std::vector<std::uint8_t> head(std::int64_t cues_position) {
        std::vector<std::uint8_t> h;
        ebml::append(h, ebml::uint_element(kEbmlVersion, 1));
        ebml::append(h, ebml::uint_element(kEbmlReadVersion, 1));
        ebml::append(h, ebml::uint_element(kEbmlMaxIdLength, 4));
        ebml::append(h, ebml::uint_element(kEbmlMaxSizeLength, 8));
        ebml::append(h, ebml::string_element(kDocType, "matroska"));
        ebml::append(h, ebml::uint_element(kDocTypeVersion, 4));
        ebml::append(h, ebml::uint_element(kDocTypeReadVersion, 2));
        ebml_ = ebml::element(ebml::kEbml, h);

        std::vector<std::uint8_t> info;
        ebml::append(info, ebml::uint_element(ebml::kTimestampScale, 1000000));
        ebml::append(info, float_element(kDuration, static_cast<double>(duration_ms_)));
        ebml::append(info, ebml::string_element(kMuxingApp, "seekserve"));
        ebml::append(info, ebml::string_element(kWritingApp, "seekserve"));
        auto info_el = ebml::element(ebml::kInfo, info);
        auto tracks_el = ebml::element(ebml::kTracks, track_entries());

        auto seek = [](std::uint32_t id, std::int64_t at) {
            std::vector<std::uint8_t> id_bytes;
            ebml::put_id(id_bytes, id);
            auto s = ebml::element(ebml::kSeekId, id_bytes);
            ebml::append(s, ebml::uint_element(ebml::kSeekPosition, static_cast<std::uint64_t>(at), kWideNumber));
            return ebml::element(ebml::kSeek, s);
        };
        auto seek_len = [&] {
            std::vector<std::uint8_t> s;
            ebml::append(s, seek(ebml::kInfo, 0));
            ebml::append(s, seek(ebml::kTracks, 0));
            ebml::append(s, seek(ebml::kCues, 0));
            return ebml::element(ebml::kSeekHead, s).size();
        }();
        auto info_at = static_cast<std::int64_t>(seek_len);
        auto tracks_at = info_at + static_cast<std::int64_t>(info_el.size());
        std::vector<std::uint8_t> seeks;
        ebml::append(seeks, seek(ebml::kInfo, info_at));
        ebml::append(seeks, seek(ebml::kTracks, tracks_at));
        ebml::append(seeks, seek(ebml::kCues, cues_position));

        std::vector<std::uint8_t> out = ebml_;
        ebml::put_id(out, ebml::kSegment);
        // Before the plan knows the size, only the length counts.
        auto segment = f_.size_ > segment_data_ ? f_.size_ - segment_data_ : 0;
        ebml::put_size(out, static_cast<std::uint64_t>(segment), kWideNumber);
        ebml::append(out, ebml::element(ebml::kSeekHead, seeks));
        ebml::append(out, info_el);
        ebml::append(out, tracks_el);
        return out;
    }

    // The Clusters, from `at`: a new one at a keyframe of the video (at
    // most one a second), or when its blocks would span over 30 s.
    void clusters(std::int64_t at) {
        std::vector<Mp4SampleCursor> start;
        for (const auto& t : f_.mp4_.tracks) start.emplace_back(t);
        EventWalker w(f_.mp4_, f_.track_, f_.shift_ns_, start, 0);
        Mp4MkvFile::ClusterPlan cur;
        bool open = false;
        std::int64_t lo = 0, hi = 0, key_ms = -1, blocks = 0;
        std::uint32_t count = 0;
        auto close = [&] {
            cur.timestamp = lo;
            auto payload = static_cast<std::int64_t>(
                ebml::uint_element(ebml::kTimestamp, static_cast<std::uint64_t>(lo)).size()) + blocks;
            cur.size = 4 + static_cast<std::int64_t>(vint_length(static_cast<std::uint64_t>(payload))) + payload;
            cur.at = at;
            at += cur.size;
            if (key_ms >= 0) cues_.emplace_back(key_ms, cur.at - segment_data_);
            f_.clusters_.push_back(std::move(cur));
        };
        Event e;
        while (w.peek(e)) {
            bool key = !e.text && e.track == video_ && e.sample.key;
            auto span = std::max(hi, e.pts_ms) - std::min(lo, e.pts_ms);
            if (!open || (key && (key_ms < 0 || e.pts_ms - key_ms >= kMinKeySpacingMs)) ||
                span > kMaxClusterSpanMs || count >= f_.max_blocks_) {
                if (open) close();
                cur = Mp4MkvFile::ClusterPlan{};
                cur.cursors = w.cursors();
                cur.samples.assign(f_.mp4_.tracks.size(), 0);
                cur.first_line = w.next_line();
                lo = hi = e.pts_ms;
                key_ms = key ? e.pts_ms : -1;
                blocks = 0;
                count = 0;
                open = true;
            }
            lo = std::min(lo, e.pts_ms);
            hi = std::max(hi, e.pts_ms);
            blocks += mp4mkv::block_size(e, f_.track_);
            ++count;
            if (e.text) {
                ++cur.lines;
            } else {
                ++cur.samples[e.track];
            }
            w.take(e);
        }
        if (open) close();
        f_.cues_at_ = at;
    }

    std::vector<std::uint8_t> cues_element() const {
        std::vector<std::uint8_t> points;
        for (const auto& [time, position] : cues_) {
            std::vector<std::uint8_t> where;
            ebml::append(where, ebml::uint_element(ebml::kCueTrack, video_ + 1));
            ebml::append(where, ebml::uint_element(ebml::kCueClusterPosition, static_cast<std::uint64_t>(position)));
            std::vector<std::uint8_t> point = ebml::uint_element(ebml::kCueTime, static_cast<std::uint64_t>(time));
            ebml::append(point, ebml::element(ebml::kCueTrackPositions, where));
            ebml::append(points, ebml::element(ebml::kCuePoint, point));
        }
        return ebml::element(ebml::kCues, points);
    }

    Mp4MkvFile& f_;
    std::string& why_;
    std::size_t video_ = 0;
    std::int64_t duration_ms_ = 0;
    std::int64_t segment_data_ = 0;
    std::vector<std::uint8_t> ebml_;
    std::vector<std::pair<std::int64_t, std::int64_t>> cues_;  // time, position
};

std::shared_ptr<const Mp4MkvFile> Mp4MkvFile::build(Mp4Structure mp4, const SubtitleTrack& track,
                                                    std::string& why, std::uint32_t max_blocks) {
    auto f = std::make_shared<Mp4MkvFile>();
    f->max_blocks_ = std::max<std::uint32_t>(1, max_blocks);
    f->mp4_ = std::move(mp4);
    f->track_ = track;
    if (!Mp4MkvPlanner(*f, why).plan()) return nullptr;
    return f;
}

}  // namespace seekserve
