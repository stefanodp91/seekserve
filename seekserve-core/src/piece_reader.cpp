#include "seekserve/piece_reader.hpp"

namespace seekserve {

TorrentPieceReader::TorrentPieceReader(Ask ask) : ask_(std::move(ask)) {}

Result<PieceBuffer> TorrentPieceReader::read(PieceIndex piece, std::chrono::milliseconds wait) {
    std::unique_lock lock(mu_);
    if (cancelled_) return make_error_code(errc::cancelled);

    auto& pending = pending_[piece];
    const bool first = pending.waiters == 0 && !pending.answer;
    ++pending.waiters;
    if (first) {
        // Outside the lock: an answer may come at once, on this thread.
        lock.unlock();
        ask_(piece);
        lock.lock();
    }

    cv_.wait_for(lock, wait, [&] { return cancelled_ || pending_[piece].answer.has_value(); });

    auto& mine = pending_[piece];
    std::optional<Result<PieceBuffer>> answer = mine.answer;
    if (--mine.waiters == 0) pending_.erase(piece);

    if (cancelled_) return make_error_code(errc::cancelled);
    if (!answer) return make_error_code(errc::timeout_waiting_for_piece);
    return *answer;
}

void TorrentPieceReader::cancel() {
    {
        std::lock_guard lock(mu_);
        cancelled_ = true;
    }
    cv_.notify_all();
}

void TorrentPieceReader::on_read(PieceIndex piece, Result<PieceBuffer> answer) {
    {
        std::lock_guard lock(mu_);
        auto it = pending_.find(piece);
        // Nobody waits any more: the next read asks again.
        if (it == pending_.end()) return;
        it->second.answer = std::move(answer);
    }
    cv_.notify_all();
}

} // namespace seekserve
