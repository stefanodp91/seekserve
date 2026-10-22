#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "seekserve/error.hpp"
#include "seekserve/types.hpp"

namespace seekserve {

/// What a stream server reads a file through: a torrent's file, whose
/// pieces may still be missing (ByteSource), or a whole file on disk
/// (FileSource, app WORK-37).
class StreamSource {
public:
    virtual ~StreamSource() = default;

    // Reads [offset, offset+len), waiting at most `wait` for missing data:
    // a caller waits in slices and checks in between whether to go on.
    // Shorter at the end of the file.
    virtual Result<std::vector<std::uint8_t>> read(std::int64_t offset, std::int64_t len,
                                                   std::chrono::milliseconds wait) = 0;
    // Piece holding the byte at `offset`, or -1 once cancelled.
    virtual PieceIndex piece_at(std::int64_t offset) const = 0;
    // Bytes from `offset` to the end of its piece, or -1 when there is no
    // piece end to stop at (or once cancelled).
    virtual std::int64_t bytes_to_piece_end(std::int64_t offset) const = 0;
    // Every read from now on fails with errc::cancelled.
    virtual void cancel() = 0;
    virtual std::int64_t file_size() const = 0;
};

} // namespace seekserve
