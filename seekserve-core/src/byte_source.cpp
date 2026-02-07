#include "seekserve/byte_source.hpp"

#include <fstream>

#include <spdlog/spdlog.h>

namespace seekserve {

ByteSource::ByteSource(lt::torrent_handle handle,
                       FileIndex file_idx,
                       const std::string& file_path,
                       ByteRangeMapper& mapper,
                       PieceAvailabilityIndex& avail,
                       std::chrono::milliseconds read_timeout)
    : handle_(std::move(handle))
    , file_idx_(file_idx)
    , file_path_(file_path)
    , mapper_(mapper)
    , avail_(avail)
    , timeout_(read_timeout)
{
}

Result<std::vector<std::uint8_t>> ByteSource::read(std::int64_t offset, std::int64_t len) {
    if (cancelled_.load(std::memory_order_acquire)) {
        return make_error_code(errc::cancelled);
    }

    ByteRange range{offset, offset + len - 1};
    auto span = mapper_.map(range);

    {
        std::unique_lock lock(mu_);
        bool ok = cv_.wait_for(lock, timeout_, [&] {
            return avail_.is_span_complete(span) || cancelled_.load(std::memory_order_acquire);
        });

        if (cancelled_.load(std::memory_order_acquire)) {
            return make_error_code(errc::cancelled);
        }
        if (!ok) {
            return make_error_code(errc::timeout_waiting_for_piece);
        }
    }

    return read_from_disk(offset, len);
}

Result<std::vector<std::uint8_t>> ByteSource::read_from_disk(std::int64_t offset, std::int64_t len) {
    std::ifstream file(file_path_, std::ios::binary);
    if (!file) {
        spdlog::error("ByteSource: failed to open file '{}'", file_path_);
        return make_error_code(errc::io_error);
    }

    file.seekg(offset);
    if (!file) {
        return make_error_code(errc::io_error);
    }

    std::vector<std::uint8_t> buf(len);
    file.read(reinterpret_cast<char*>(buf.data()), len);
    auto bytes_read = file.gcount();
    buf.resize(static_cast<std::size_t>(bytes_read));

    return buf;
}

bool ByteSource::is_available(std::int64_t offset, std::int64_t len) const {
    ByteRange range{offset, offset + len - 1};
    auto span = mapper_.map(range);
    return avail_.is_span_complete(span);
}

void ByteSource::notify_piece_complete() {
    cv_.notify_all();
}

void ByteSource::cancel() {
    cancelled_.store(true, std::memory_order_release);
    cv_.notify_all();
}

std::int64_t ByteSource::file_size() const {
    return mapper_.file_size();
}

} // namespace seekserve
