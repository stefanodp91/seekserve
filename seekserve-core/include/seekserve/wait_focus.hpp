#pragma once

#include <chrono>

#include "seekserve/types.hpp"

namespace seekserve {

// No piece: no reader waits for a download.
inline constexpr PieceIndex kNoPiece = -1;

// When a reader waits for the same piece too long, the start with priority
// (StartupFocus) begins again from that piece (app BUG-246, DEC-669). On the
// app's phone a reader waited 75 s for a piece a connected peer had and would
// send: libtorrent asks a peer for a piece with a deadline only while that
// peer's queue takes less than 2 s (`torrent::request_time_critical_pieces`),
// and through Tor its queue of pieces not due took about 3 s. The start with
// priority puts the piece first for the ordinary requests too.
class WaitFocusWatch {
public:
    using Clock = std::chrono::steady_clock;

    // `after` 0 = never due, as before.
    explicit WaitFocusWatch(std::chrono::milliseconds after);

    // `piece` is the piece a reader waits for now, kNoPiece for none. True
    // once for each wait, when a reader has waited for the same piece for
    // `after`.
    bool due(PieceIndex piece, Clock::time_point now);

private:
    std::chrono::milliseconds after_;
    PieceIndex piece_ = kNoPiece;
    Clock::time_point since_{};
    bool told_ = false;
};

} // namespace seekserve
