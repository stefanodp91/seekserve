#include "seekserve/wait_focus.hpp"

namespace seekserve {

WaitFocusWatch::WaitFocusWatch(std::chrono::milliseconds after) : after_(after) {}

bool WaitFocusWatch::due(PieceIndex piece, int done, bool someone_has_it, Clock::time_point now) {
    if (piece != piece_ || done != done_) {
        piece_ = piece;
        done_ = done;
        since_ = now;
    }
    if (after_.count() <= 0 || piece_ == kNoPiece || !someone_has_it) return false;
    if (now - since_ < after_) return false;
    since_ = now;
    return true;
}

} // namespace seekserve
