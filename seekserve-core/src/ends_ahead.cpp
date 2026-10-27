#include "seekserve/ends_ahead.hpp"

#include <algorithm>

namespace seekserve {

std::vector<PieceIndex> file_end_pieces(const ByteRangeMapper& mapper, std::int64_t bytes) {
    const std::int64_t size = mapper.file_size();
    if (bytes <= 0 || size <= 0) return {};

    const auto head = mapper.map(ByteRange{0, std::min(size, bytes) - 1});
    const auto end = mapper.map(ByteRange{std::max<std::int64_t>(0, size - bytes), size - 1});
    std::vector<PieceIndex> pieces;
    for (PieceIndex p = head.first; p <= head.last; ++p) pieces.push_back(p);
    for (PieceIndex p = std::max(end.first, head.last + 1); p <= end.last; ++p) {
        pieces.push_back(p);
    }
    return pieces;
}

std::vector<lt::download_priority_t> with_pieces_first(
    std::vector<lt::download_priority_t> current, const std::vector<PieceIndex>& pieces,
    const std::function<bool(PieceIndex)>& have) {
    for (const auto p : pieces) {
        if (p < 0 || static_cast<std::size_t>(p) >= current.size() || have(p)) continue;
        current[static_cast<std::size_t>(p)] = lt::top_priority;
    }
    return current;
}

} // namespace seekserve
