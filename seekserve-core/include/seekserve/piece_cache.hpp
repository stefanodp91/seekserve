#pragma once

#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <utility>

#include "seekserve/piece_reader.hpp"
#include "seekserve/types.hpp"

namespace seekserve {

// The pieces read through libtorrent, kept for the next chunks of the same
// piece (app BUG-236, DEC-657): the server sends a piece in 64 KB chunks.
// At most `max_bytes`, the least recently used going first, always the
// newest one: a piece bigger than the limit must still fit.
class PieceCache {
public:
    explicit PieceCache(std::int64_t max_bytes);

    std::optional<PieceBuffer> get(PieceIndex piece);
    void put(PieceIndex piece, PieceBuffer buffer);
    void clear();
    std::int64_t bytes() const;

private:
    std::int64_t max_bytes_;
    mutable std::mutex mu_;
    std::list<std::pair<PieceIndex, PieceBuffer>> pieces_;  // most recent first
    std::int64_t bytes_ = 0;
};

} // namespace seekserve
