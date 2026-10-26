#include "seekserve/wait_focus.hpp"

namespace seekserve {

WaitFocusWatch::WaitFocusWatch(std::chrono::milliseconds after) : after_(after) {}

bool WaitFocusWatch::due(PieceIndex piece, Clock::time_point now) {
    if (piece != piece_) {
        piece_ = piece;
        since_ = now;
        told_ = false;
    }
    if (after_.count() <= 0 || piece_ == kNoPiece || told_) return false;
    if (now - since_ < after_) return false;
    told_ = true;
    return true;
}

} // namespace seekserve
