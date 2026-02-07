#include "seekserve/byte_range_mapper.hpp"

namespace seekserve {

ByteRangeMapper::ByteRangeMapper(const lt::file_storage& fs, FileIndex file_idx)
    : fs_(fs)
    , file_idx_(file_idx)
    , file_offset_(fs.file_offset(lt::file_index_t{file_idx}))
    , file_size_(fs.file_size(lt::file_index_t{file_idx}))
    , piece_length_(fs.piece_length())
{
}

PieceSpan ByteRangeMapper::map(const ByteRange& range) const {
    auto first_req = fs_.map_file(lt::file_index_t{file_idx_},
                                   range.start, 1);
    auto last_req = fs_.map_file(lt::file_index_t{file_idx_},
                                  range.end, 1);

    PieceSpan span;
    span.first = static_cast<PieceIndex>(first_req.piece);
    span.last = static_cast<PieceIndex>(last_req.piece);
    span.first_offset = first_req.start;

    int last_piece_len = fs_.piece_size(lt::piece_index_t{span.last});
    span.last_length = last_req.start + last_req.length;
    if (span.last_length > last_piece_len) {
        span.last_length = last_piece_len;
    }

    return span;
}

int ByteRangeMapper::piece_length() const { return piece_length_; }
std::int64_t ByteRangeMapper::file_size() const { return file_size_; }
PieceIndex ByteRangeMapper::first_piece() const {
    auto req = fs_.map_file(lt::file_index_t{file_idx_}, 0, 1);
    return static_cast<PieceIndex>(req.piece);
}
PieceIndex ByteRangeMapper::end_piece() const {
    if (file_size_ == 0) return first_piece();
    auto req = fs_.map_file(lt::file_index_t{file_idx_}, file_size_ - 1, 1);
    return static_cast<PieceIndex>(req.piece) + 1;
}

} // namespace seekserve
