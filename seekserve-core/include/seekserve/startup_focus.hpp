#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>
#include <vector>

#include <libtorrent/download_priority.hpp>

#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/config.hpp"
#include "seekserve/piece_availability.hpp"

namespace seekserve {

// The start of a stream with priority (app BUG-228, DEC-656). Through Tor
// the peers' queues fill with pieces of the file that are not due, and the
// pieces a reader waits for come one at a time behind them. When a reader
// starts or jumps far, the pieces of its first `startup_focus_bytes` go to
// the top priority and every other wanted piece to the lowest, never to
// zero: with nothing else wanted libtorrent would call the torrent
// finished, and the app's queue a download complete. It is over when those
// bytes are there or `startup_focus_timeout` passed; the caller then sets
// the files' priorities again.
//
// A file that may keep its index at its end (`with_end`, an MP4) has its
// last `startup_focus_bytes` at the top too: a reader that starts asks for
// them before it plays, and a TV gives up if they come late (app BUG-231).
class StartupFocus {
public:
    using Clock = std::chrono::steady_clock;

    explicit StartupFocus(SchedulerConfig config, bool with_end = false);

    // The pieces' priorities for a reader at byte `offset` of the file of
    // `mapper`, from the ones libtorrent has now (`current`); empty, and
    // nothing begun, when the focus is off or its bytes are there.
    std::vector<lt::download_priority_t> begin(
        std::int64_t offset, const ByteRangeMapper& mapper,
        const PieceAvailabilityIndex& avail,
        const std::vector<lt::download_priority_t>& current, Clock::time_point now);

    // Whether the focus begun is over: its pieces, the end's too, are there
    // or its time passed.
    bool over(const PieceAvailabilityIndex& avail, Clock::time_point now) const;

    bool active() const;
    void end();

private:
    bool complete(const PieceAvailabilityIndex& avail) const;

    SchedulerConfig config_;
    bool with_end_;
    bool active_ = false;
    PieceSpan head_{0, -1};
    PieceSpan end_{0, -1};  // empty without the end
    Clock::time_point started_{};
};

// Whether a file's container may keep its index at the end, from the
// extension of its path: MP4 and QuickTime files ("moov" after the data).
bool keeps_index_at_end(std::string_view path);

} // namespace seekserve
