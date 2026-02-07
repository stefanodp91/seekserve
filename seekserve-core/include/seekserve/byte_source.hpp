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

namespace seekserve {

class ByteSource {
public:
    ByteSource(lt::torrent_handle handle,
               FileIndex file_idx,
               const std::string& file_path,
               ByteRangeMapper& mapper,
               PieceAvailabilityIndex& avail,
               std::chrono::milliseconds read_timeout = std::chrono::seconds(30));

    Result<std::vector<std::uint8_t>> read(std::int64_t offset, std::int64_t len);
    bool is_available(std::int64_t offset, std::int64_t len) const;
    void notify_piece_complete();
    void cancel();
    std::int64_t file_size() const;

private:
    Result<std::vector<std::uint8_t>> read_from_disk(std::int64_t offset, std::int64_t len);

    lt::torrent_handle handle_;
    FileIndex file_idx_;
    std::string file_path_;
    ByteRangeMapper& mapper_;
    PieceAvailabilityIndex& avail_;
    std::chrono::milliseconds timeout_;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> cancelled_{false};
};

} // namespace seekserve
