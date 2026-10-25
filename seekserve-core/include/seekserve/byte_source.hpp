#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdint>

#include <libtorrent/torrent_handle.hpp>

#include "seekserve/types.hpp"
#include "seekserve/error.hpp"
#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/piece_availability.hpp"
#include "seekserve/stream_source.hpp"
#include "seekserve/piece_cache.hpp"
#include "seekserve/piece_reader.hpp"

namespace seekserve {

class ByteSource final : public StreamSource {
public:
    ByteSource(lt::torrent_handle handle,
               FileIndex file_idx,
               const std::string& file_path,
               ByteRangeMapper& mapper,
               PieceAvailabilityIndex& avail,
               std::chrono::milliseconds read_timeout = std::chrono::seconds(30),
               std::shared_ptr<PieceReader> pieces = nullptr,
               std::int64_t piece_cache_bytes = 0);

    // Waits up to the read timeout given at construction for the pieces
    // covering [offset, offset+len), then reads them from disk.
    Result<std::vector<std::uint8_t>> read(std::int64_t offset, std::int64_t len);
    // Same, waiting at most `wait`: lets a caller wait in slices and check
    // in between whether it should keep waiting (see HttpRangeServer).
    Result<std::vector<std::uint8_t>> read(std::int64_t offset, std::int64_t len,
                                           std::chrono::milliseconds wait) override;
    bool is_available(std::int64_t offset, std::int64_t len) const;
    // Piece holding the byte at `offset`, or -1 once cancelled.
    PieceIndex piece_at(std::int64_t offset) const override;
    // Bytes from `offset` to the end of its piece, or -1 once cancelled.
    std::int64_t bytes_to_piece_end(std::int64_t offset) const override;
    void notify_piece_complete();
    // After cancel() returns no call touches the mapper or the availability
    // index again, so their owner may destroy them.
    void cancel() override;
    std::int64_t file_size() const override;

    // Pieces read through libtorrent, and chunks found in memory instead.
    std::int64_t piece_reads() const;
    std::int64_t cache_hits() const;

private:
    Result<std::vector<std::uint8_t>> read_from_disk(std::int64_t offset, std::int64_t len);
    // [offset, offset+len), each piece that arrived during the stream through
    // libtorrent, the others from disk (app BUG-236, DEC-657).
    Result<std::vector<std::uint8_t>> read_by_piece(std::int64_t offset, std::int64_t len,
                                                    std::chrono::milliseconds wait);
    // A piece's bytes from memory or through libtorrent; one libtorrent does
    // not have goes back to waiting (errc::timeout_waiting_for_piece).
    Result<PieceBuffer> piece_bytes(PieceIndex piece, std::chrono::milliseconds wait);

    lt::torrent_handle handle_;
    FileIndex file_idx_;
    std::string file_path_;
    ByteRangeMapper& mapper_;
    PieceAvailabilityIndex& avail_;
    std::chrono::milliseconds timeout_;
    std::int64_t file_size_;

    // Guards every use of mapper_ and avail_ together with cancelled_, and
    // the condition variable, so a completion is never missed between the
    // check and the wait.
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> cancelled_{false};
    std::shared_ptr<PieceReader> pieces_;
    // The file's pieces that were there when the stream started: written to
    // the file long ago, they are read from it.
    PieceIndex first_piece_ = 0;
    std::vector<bool> on_disk_at_start_;
    PieceCache cache_;
    std::atomic<std::int64_t> piece_reads_{0};
    std::atomic<std::int64_t> cache_hits_{0};
};

} // namespace seekserve
