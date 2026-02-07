#pragma once

#include <vector>
#include <atomic>
#include <cstdint>

#include "seekserve/types.hpp"

namespace seekserve {

class PieceAvailabilityIndex {
public:
    PieceAvailabilityIndex() = default;
    PieceAvailabilityIndex(int num_pieces, int piece_length, int last_piece_length);

    void reset(int num_pieces, int piece_length, int last_piece_length);
    void mark_complete(PieceIndex piece);
    bool is_complete(PieceIndex piece) const;
    bool is_span_complete(const PieceSpan& span) const;
    int contiguous_from(PieceIndex start) const;
    std::int64_t contiguous_bytes_from(PieceIndex start_piece, int offset_in_piece) const;
    float progress() const;
    int num_pieces() const { return num_pieces_; }

private:
    int num_pieces_ = 0;
    int piece_length_ = 0;
    int last_piece_length_ = 0;
    std::vector<std::atomic<bool>> completed_;
    std::atomic<int> completed_count_{0};
};

} // namespace seekserve
