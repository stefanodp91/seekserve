#pragma once

#include <libtorrent/file_storage.hpp>

#include "seekserve/types.hpp"

namespace seekserve {

class ByteRangeMapper {
public:
    ByteRangeMapper(const lt::file_storage& fs, FileIndex file_idx);

    PieceSpan map(const ByteRange& range) const;
    int piece_length() const;
    std::int64_t file_size() const;
    PieceIndex first_piece() const;
    PieceIndex end_piece() const;

private:
    const lt::file_storage& fs_;
    FileIndex file_idx_;
    std::int64_t file_offset_;
    std::int64_t file_size_;
    int piece_length_;
};

} // namespace seekserve
