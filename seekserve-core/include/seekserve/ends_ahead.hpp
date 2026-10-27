#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <libtorrent/download_priority.hpp>

#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/types.hpp"

namespace seekserve {

// The start and the end of the next episode's file, fetched while another
// file of the pack plays (app BUG-231, DEC-678). When the player gets to the
// next episode, a TV reads the file's first bytes, then its last ones, where
// an MP4 keeps its index, and gives up if they come late; and a torrent whose
// wanted files are all there has lost its peers by then.

// The pieces of the first and the last `bytes` of the file of `mapper`, in
// order and without repeats; none without bytes.
std::vector<PieceIndex> file_end_pieces(const ByteRangeMapper& mapper, std::int64_t bytes);

// `current` with each of `pieces` that is not there yet (`have`) at the top
// priority; a piece outside the torrent is left out.
std::vector<lt::download_priority_t> with_pieces_first(
    std::vector<lt::download_priority_t> current, const std::vector<PieceIndex>& pieces,
    const std::function<bool(PieceIndex)>& have);

} // namespace seekserve
