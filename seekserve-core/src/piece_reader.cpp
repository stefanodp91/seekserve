#include "seekserve/piece_reader.hpp"

#include <algorithm>

namespace seekserve {

TorrentPieceReader::TorrentPieceReader(Ask ask) : ask_(std::move(ask)) {}

Result<PieceBuffer> TorrentPieceReader::read(PieceIndex piece, std::chrono::milliseconds wait) {
    std::unique_lock lock(mu_);
    if (cancelled_) return make_error_code(errc::cancelled);

    // An answer that came while nobody waited: this read's.
    if (auto it = pending_.find(piece);
        it != pending_.end() && it->second.waiters == 0 && it->second.answer) {
        auto answer = *it->second.answer;
        pending_.erase(it);
        return answer;
    }

    auto& pending = pending_[piece];
    // Asked again by a read that finds no answer and nobody else waiting:
    // an answer that never comes must not stop the stream for good (app
    // BUG-238, DEC-659). A late answer to the first request is still taken.
    const bool ask = !pending.answer && (!pending.asked || pending.waiters == 0);
    pending.asked = true;
    ++pending.waiters;
    if (ask) {
        // Outside the lock: an answer may come at once, on this thread.
        lock.unlock();
        ask_(piece);
        lock.lock();
    }

    cv_.wait_for(lock, wait, [&] {
        if (cancelled_) return true;
        auto it = pending_.find(piece);
        return it != pending_.end() && it->second.answer.has_value();
    });

    auto it = pending_.find(piece);
    if (it == pending_.end()) {
        return make_error_code(cancelled_ ? errc::cancelled : errc::timeout_waiting_for_piece);
    }
    auto& mine = it->second;
    --mine.waiters;
    if (cancelled_) return make_error_code(errc::cancelled);
    if (mine.answer) {
        auto answer = *mine.answer;
        if (mine.waiters == 0) pending_.erase(it);
        return answer;
    }
    // Not yet: the answer, if it comes, is the next read's.
    if (mine.waiters == 0) keep_idle(mine);
    return make_error_code(errc::timeout_waiting_for_piece);
}

void TorrentPieceReader::cancel() {
    {
        std::lock_guard lock(mu_);
        cancelled_ = true;
        pending_.clear();
    }
    cv_.notify_all();
}

void TorrentPieceReader::on_read(PieceIndex piece, Result<PieceBuffer> answer) {
    {
        std::lock_guard lock(mu_);
        auto it = pending_.find(piece);
        // Not asked, or let go: nobody wants it.
        if (it == pending_.end()) return;
        auto& pending = it->second;
        if (pending.waiters == 0) {
            // An error is for who waits now: a later read, once the piece is
            // there again, asks again.
            if (!answer.ok()) {
                pending_.erase(it);
                return;
            }
            pending.answer = std::move(answer);
            keep_idle(pending);
        } else {
            pending.answer = std::move(answer);
        }
    }
    cv_.notify_all();
}

void TorrentPieceReader::keep_idle(Pending& pending) {
    pending.idle_since = ++idle_clock_;
    for (;;) {
        std::size_t idle = 0;
        auto oldest = pending_.end();
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->second.waiters != 0) continue;
            ++idle;
            if (oldest == pending_.end() || it->second.idle_since < oldest->second.idle_since) {
                oldest = it;
            }
        }
        if (idle <= kMaxIdlePieces || oldest == pending_.end()) return;
        pending_.erase(oldest);
    }
}

} // namespace seekserve
