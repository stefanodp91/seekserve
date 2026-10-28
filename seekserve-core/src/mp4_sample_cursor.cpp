#include "seekserve/mp4_structure.hpp"

#include <cstdint>

namespace seekserve {

namespace {

constexpr std::int64_t kNsPerSecond = 1000000000;

// num / den rounded to the nearest, half away from zero; den > 0.
__int128 rounded_div(__int128 num, __int128 den) {
    __int128 q = num / den;
    __int128 r = num % den;
    if (2 * (r < 0 ? -r : r) >= den) q += num < 0 ? -1 : 1;
    return q;
}

}  // namespace

std::int64_t Mp4Track::presentation_ns(std::int64_t media_time) const {
    // Saturated: a hostile file's times past int64 stay at its ends, where
    // the rebuild refuses them, instead of wrapping.
    auto ns = rounded_div((static_cast<__int128>(media_time) - media_start) * kNsPerSecond, timescale) + delay_ns;
    auto top = static_cast<__int128>(INT64_MAX);
    return static_cast<std::int64_t>(ns > top ? top : ns < -top ? -top : ns);
}

Mp4SampleCursor::Mp4SampleCursor(const Mp4Track& track) : track_(&track) {
    if (track.sample_count == 0 || track.chunk_runs.empty() || track.chunk_offsets.empty()) return;
    left_in_chunk_ = track.chunk_runs[0].samples_per_chunk;
    offset_ = track.chunk_offsets[0];
    while (stts_ < track.time_to_sample.size() && track.time_to_sample[stts_].first == 0) ++stts_;
    if (stts_ < track.time_to_sample.size()) stts_left_ = track.time_to_sample[stts_].first;
    while (ctts_ < track.composition.size() && track.composition[ctts_].first == 0) ++ctts_;
    if (ctts_ < track.composition.size()) ctts_left_ = track.composition[ctts_].first;
}

bool Mp4SampleCursor::next(Mp4Sample& out) {
    const auto& t = *track_;
    if (index_ >= t.sample_count || left_in_chunk_ == 0) return false;

    out.index = index_;
    out.offset = offset_;
    out.size = t.fixed_size != 0 ? t.fixed_size : t.sizes[index_];
    out.dts = dts_;
    out.pts = dts_ + (ctts_left_ > 0 ? t.composition[ctts_].second : 0);
    out.key = t.all_sync || (sync_ < t.sync.size() && t.sync[sync_] == index_);
    if (!t.all_sync && out.key) ++sync_;

    // The time of the next sample.
    if (stts_left_ > 0) {
        dts_ += t.time_to_sample[stts_].second;
        if (--stts_left_ == 0) {
            do ++stts_;
            while (stts_ < t.time_to_sample.size() && t.time_to_sample[stts_].first == 0);
            if (stts_ < t.time_to_sample.size()) stts_left_ = t.time_to_sample[stts_].first;
        }
    }
    if (ctts_left_ > 0 && --ctts_left_ == 0) {
        do ++ctts_;
        while (ctts_ < t.composition.size() && t.composition[ctts_].first == 0);
        if (ctts_ < t.composition.size()) ctts_left_ = t.composition[ctts_].first;
    }

    // Its place: after this one in the chunk, or at the start of the next.
    ++index_;
    offset_ += out.size;
    if (--left_in_chunk_ == 0 && index_ < t.sample_count && chunk_ + 1 < t.chunk_offsets.size()) {
        ++chunk_;
        if (run_ + 1 < t.chunk_runs.size() && t.chunk_runs[run_ + 1].first_chunk == chunk_) ++run_;
        left_in_chunk_ = t.chunk_runs[run_].samples_per_chunk;
        offset_ = t.chunk_offsets[chunk_];
    }
    return true;
}

}  // namespace seekserve
