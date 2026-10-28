#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "seekserve/error.hpp"
#include "seekserve/types.hpp"

namespace seekserve {

/// What a stream server reads a file through: a torrent's file, whose
/// pieces may still be missing (ByteSource), a whole file on disk
/// (FileSource, app WORK-37), or a file rebuilt from one of those
/// (MkvSubtitleSource, app WORK-37 step 7).
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
    // Valid once prepare() has succeeded.
    virtual std::int64_t file_size() const = 0;

    // A source that has to read part of its file before it knows its size
    // and where each of its bytes comes from (a rebuilt file): waits at most
    // `wait`, errc::timeout_waiting_for_piece while data is missing. A
    // server calls it in slices before it answers a request.
    virtual Result<void> prepare(std::chrono::milliseconds /*wait*/) { return {}; }
    // While prepare() waits: the bytes of the underlying file it waits for,
    // for the scheduler.
    virtual std::optional<ByteRange> preparing_range() const { return std::nullopt; }
    // Where the byte at `offset` comes from in the underlying file, for the
    // scheduler: the same offset unless the file is rebuilt.
    virtual std::int64_t file_offset(std::int64_t offset) const { return offset; }
};

} // namespace seekserve
