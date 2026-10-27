#pragma once

#include <chrono>

#include "seekserve/types.hpp"

namespace seekserve {

// No piece: no reader waits for a download.
inline constexpr PieceIndex kNoPiece = -1;

// When a reader waits for the same piece and no block of it arrives for a
// while, that piece is asked first (app BUG-246, DEC-670), again after each
// such while, as long as a connected peer that has it sends us blocks (app
// BUG-247: one that chokes us is asked nothing). On the app's phone a reader
// waited 75 s for a piece a connected peer had and would send: libtorrent
// asks a peer for a piece with a deadline only while that peer's queue takes
// less than 2 s (`torrent::request_time_critical_pieces`), keeps that queue at
// about 3 s of pieces not due (`request_queue_time`), and cancels those only
// when a deadline comes on an empty list (`torrent::set_piece_deadline`).
class WaitFocusWatch {
public:
    using Clock = std::chrono::steady_clock;

    // `after` 0 = never due, as before.
    explicit WaitFocusWatch(std::chrono::milliseconds after);

    // `piece` is the piece a reader waits for now (kNoPiece for none), `done`
    // its blocks there, `someone_sends_it` whether a connected peer that has
    // it sends us blocks (`someone_sends_it`, wait_piece.hpp). True when no
    // block of the same piece arrived for `after`, and again after each
    // further `after`; never while nobody sends it, which would only cancel
    // the other requests.
    bool due(PieceIndex piece, int done, bool someone_sends_it, Clock::time_point now);

    // Whether it can ever be due: with `after` 0 nothing needs reading.
    bool on() const { return after_.count() > 0; }

private:
    std::chrono::milliseconds after_;
    PieceIndex piece_ = kNoPiece;
    int done_ = 0;
    Clock::time_point since_{};
};

} // namespace seekserve
