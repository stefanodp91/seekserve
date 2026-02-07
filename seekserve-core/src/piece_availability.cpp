#include "seekserve/piece_availability.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace seekserve {

PieceAvailabilityIndex::PieceAvailabilityIndex(int num_pieces, int piece_length, int last_piece_length)
    : num_pieces_(num_pieces)
    , piece_length_(piece_length)
    , last_piece_length_(last_piece_length)
    , completed_(num_pieces)
{
    for (auto& a : completed_) {
        a.store(false, std::memory_order_relaxed);
    }
    spdlog::debug("PieceAvailabilityIndex: created with {} pieces, piece_len={}", num_pieces, piece_length);
}

void PieceAvailabilityIndex::reset(int num_pieces, int piece_length, int last_piece_length) {
    num_pieces_ = num_pieces;
    piece_length_ = piece_length;
    last_piece_length_ = last_piece_length;
    completed_ = std::vector<std::atomic<bool>>(num_pieces);
    for (auto& a : completed_) {
        a.store(false, std::memory_order_relaxed);
    }
    completed_count_.store(0, std::memory_order_relaxed);
}

void PieceAvailabilityIndex::mark_complete(PieceIndex piece) {
    if (piece < 0 || piece >= num_pieces_) return;
    bool expected = false;
    if (completed_[piece].compare_exchange_strong(expected, true, std::memory_order_release)) {
        completed_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool PieceAvailabilityIndex::is_complete(PieceIndex piece) const {
    if (piece < 0 || piece >= num_pieces_) return false;
    return completed_[piece].load(std::memory_order_acquire);
}

bool PieceAvailabilityIndex::is_span_complete(const PieceSpan& span) const {
    for (PieceIndex p = span.first; p <= span.last; ++p) {
        if (!is_complete(p)) return false;
    }
    return true;
}

int PieceAvailabilityIndex::contiguous_from(PieceIndex start) const {
    int count = 0;
    for (PieceIndex p = start; p < num_pieces_; ++p) {
        if (!is_complete(p)) break;
        ++count;
    }
    return count;
}

std::int64_t PieceAvailabilityIndex::contiguous_bytes_from(PieceIndex start_piece,
                                                            int offset_in_piece) const {
    if (!is_complete(start_piece)) return 0;

    int first_piece_bytes = (start_piece == num_pieces_ - 1)
        ? last_piece_length_ - offset_in_piece
        : piece_length_ - offset_in_piece;

    std::int64_t total = first_piece_bytes;

    for (PieceIndex p = start_piece + 1; p < num_pieces_; ++p) {
        if (!is_complete(p)) break;
        total += (p == num_pieces_ - 1) ? last_piece_length_ : piece_length_;
    }

    return total;
}

float PieceAvailabilityIndex::progress() const {
    if (num_pieces_ == 0) return 0.0f;
    return static_cast<float>(completed_count_.load(std::memory_order_relaxed))
         / static_cast<float>(num_pieces_);
}

} // namespace seekserve
