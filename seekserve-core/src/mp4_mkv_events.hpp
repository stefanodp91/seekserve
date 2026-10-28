#pragma once

// The samples of an MP4 and the subtitle lines in the order of the MKV
// rebuilt from them (app WORK-37 step 7d): by decode time, then track, then
// index. The plan of the file and the reading of a Cluster walk them the
// same way, so a Cluster laid out again is the one planned.

#include <cstdint>
#include <limits>
#include <vector>

#include "seekserve/mp4_structure.hpp"
#include "seekserve/subtitle_track.hpp"

namespace seekserve::mp4mkv {

constexpr std::int64_t kNsPerMs = 1000000;

// ns to ms, rounded to the nearest, half away from zero.
inline std::int64_t to_ms(std::int64_t ns) {
    return ns >= 0 ? (ns + kNsPerMs / 2) / kNsPerMs : -((-ns + kNsPerMs / 2) / kNsPerMs);
}

struct Event {
    bool text = false;
    std::size_t track = 0;  // the sample's, by its place in Mp4Structure::tracks
    Mp4Sample sample;
    std::size_t line = 0;   // the line's, in SubtitleTrack::cues
    std::int64_t pts_ms = 0;
    std::int64_t end_ms = 0;  // a line's
};

// The size of the element of `e` in its Cluster: a SimpleBlock for a
// sample, a BlockGroup with its duration for a line.
std::int64_t block_size(const Event& e, const SubtitleTrack& lines);

class EventWalker {
public:
    static constexpr std::uint32_t kAll = std::numeric_limits<std::uint32_t>::max();

    // From `cursors` (one per track) and line `first_line`, at most
    // `samples[t]` samples of track t and `lines` lines; every one when empty.
    EventWalker(const Mp4Structure& mp4, const SubtitleTrack& lines, std::int64_t shift_ns,
                std::vector<Mp4SampleCursor> cursors, std::size_t first_line,
                std::vector<std::uint32_t> samples = {}, std::size_t line_count = kAll)
        : mp4_(mp4), lines_(lines), shift_ns_(shift_ns), next_line_(first_line), lines_left_(line_count) {
        for (std::size_t t = 0; t < cursors.size(); ++t) {
            Stream s{cursors[t], cursors[t], {}, false, samples.empty() ? kAll : samples[t]};
            load(s);
            streams_.push_back(s);
        }
    }

    // The next event, without taking it; false when there is none.
    bool peek(Event& e) const {
        int best = -1;
        std::int64_t best_dts = 0;
        for (std::size_t t = 0; t < streams_.size(); ++t) {
            if (!streams_[t].has) continue;
            auto dts = mp4_.tracks[t].presentation_ns(streams_[t].peek.dts) + shift_ns_;
            if (best < 0 || dts < best_dts) {
                best = static_cast<int>(t);
                best_dts = dts;
            }
        }
        bool line = lines_left_ > 0 && next_line_ < lines_.cues.size();
        auto line_ns = line ? lines_.cues[next_line_].start_ms * kNsPerMs + shift_ns_ : 0;
        // A line goes after the samples of its time: tracks come first.
        if (line && (best < 0 || line_ns < best_dts)) {
            e = Event{};
            e.text = true;
            e.line = next_line_;
            e.pts_ms = to_ms(line_ns);
            e.end_ms = to_ms(lines_.cues[next_line_].end_ms * kNsPerMs + shift_ns_);
            return true;
        }
        if (best < 0) return false;
        const auto& s = streams_[best];
        e = Event{};
        e.track = static_cast<std::size_t>(best);
        e.sample = s.peek;
        e.pts_ms = to_ms(mp4_.tracks[best].presentation_ns(s.peek.pts) + shift_ns_);
        return true;
    }

    // Takes the event peek() gives.
    void take(const Event& e) {
        if (e.text) {
            ++next_line_;
            if (lines_left_ != kAll) --lines_left_;
            return;
        }
        auto& s = streams_[e.track];
        s.before = s.after;
        if (s.left != kAll) --s.left;
        load(s);
    }

    // Where each track is before its next sample: a Cluster starting there.
    std::vector<Mp4SampleCursor> cursors() const {
        std::vector<Mp4SampleCursor> out;
        for (const auto& s : streams_) out.push_back(s.before);
        return out;
    }
    std::size_t next_line() const { return next_line_; }

private:
    struct Stream {
        Mp4SampleCursor before;  // gives `peek`
        Mp4SampleCursor after;   // past it
        Mp4Sample peek;
        bool has = false;
        std::uint32_t left = kAll;
    };

    static void load(Stream& s) {
        s.after = s.before;
        s.has = s.left > 0 && s.after.next(s.peek);
    }

    const Mp4Structure& mp4_;
    const SubtitleTrack& lines_;
    const std::int64_t shift_ns_;
    std::vector<Stream> streams_;
    std::size_t next_line_ = 0;
    std::size_t lines_left_ = kAll;
};

}  // namespace seekserve::mp4mkv
