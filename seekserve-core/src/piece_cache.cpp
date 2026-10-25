#include "seekserve/piece_cache.hpp"

namespace seekserve {

PieceCache::PieceCache(std::int64_t max_bytes) : max_bytes_(max_bytes) {}

std::optional<PieceBuffer> PieceCache::get(PieceIndex piece) {
    std::lock_guard lock(mu_);
    for (auto it = pieces_.begin(); it != pieces_.end(); ++it) {
        if (it->first != piece) continue;
        pieces_.splice(pieces_.begin(), pieces_, it);
        return pieces_.front().second;
    }
    return std::nullopt;
}

void PieceCache::put(PieceIndex piece, PieceBuffer buffer) {
    std::lock_guard lock(mu_);
    for (auto it = pieces_.begin(); it != pieces_.end(); ++it) {
        if (it->first != piece) continue;
        bytes_ -= it->second.size;
        pieces_.erase(it);
        break;
    }
    bytes_ += buffer.size;
    pieces_.emplace_front(piece, std::move(buffer));
    // The newest always stays, even bigger than the limit.
    while (pieces_.size() > 1 && bytes_ > max_bytes_) {
        bytes_ -= pieces_.back().second.size;
        pieces_.pop_back();
    }
}

void PieceCache::clear() {
    std::lock_guard lock(mu_);
    pieces_.clear();
    bytes_ = 0;
}

std::int64_t PieceCache::bytes() const {
    std::lock_guard lock(mu_);
    return bytes_;
}

} // namespace seekserve
