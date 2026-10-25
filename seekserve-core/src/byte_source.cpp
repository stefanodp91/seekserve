#include "seekserve/byte_source.hpp"

#include <algorithm>
#include <fstream>

#include <spdlog/spdlog.h>

namespace seekserve {

ByteSource::ByteSource(lt::torrent_handle handle,
                       FileIndex file_idx,
                       const std::string& file_path,
                       ByteRangeMapper& mapper,
                       PieceAvailabilityIndex& avail,
                       std::chrono::milliseconds read_timeout,
                       std::shared_ptr<PieceReader> pieces,
                       std::int64_t piece_cache_bytes)
    : handle_(std::move(handle))
    , file_idx_(file_idx)
    , file_path_(file_path)
    , mapper_(mapper)
    , avail_(avail)
    , timeout_(read_timeout)
    , file_size_(mapper.file_size())
    , pieces_(std::move(pieces))
    , cache_(piece_cache_bytes)
{
    first_piece_ = mapper_.first_piece();
    const PieceIndex end = mapper_.end_piece();
    on_disk_at_start_.resize(static_cast<std::size_t>(std::max(0, end - first_piece_)));
    for (PieceIndex p = first_piece_; p < end; ++p) {
        on_disk_at_start_[static_cast<std::size_t>(p - first_piece_)] = avail_.is_complete(p);
    }
}

std::int64_t ByteSource::piece_reads() const { return piece_reads_.load(); }

std::int64_t ByteSource::cache_hits() const { return cache_hits_.load(); }

Result<std::vector<std::uint8_t>> ByteSource::read(std::int64_t offset, std::int64_t len) {
    return read(offset, len, timeout_);
}

Result<std::vector<std::uint8_t>> ByteSource::read(std::int64_t offset, std::int64_t len,
                                                   std::chrono::milliseconds wait) {
    {
        std::unique_lock lock(mu_);
        if (cancelled_.load(std::memory_order_acquire)) {
            return make_error_code(errc::cancelled);
        }

        ByteRange range{offset, offset + len - 1};
        auto span = mapper_.map(range);

        bool ok = cv_.wait_for(lock, wait, [&] {
            return cancelled_.load(std::memory_order_acquire) || avail_.is_span_complete(span);
        });

        if (cancelled_.load(std::memory_order_acquire)) {
            return make_error_code(errc::cancelled);
        }
        if (!ok) {
            return make_error_code(errc::timeout_waiting_for_piece);
        }
    }

    if (!pieces_) return read_from_disk(offset, len);
    return read_by_piece(offset, len, wait);
}

Result<std::vector<std::uint8_t>> ByteSource::read_by_piece(std::int64_t offset,
                                                            std::int64_t len,
                                                            std::chrono::milliseconds wait) {
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(std::max<std::int64_t>(len, 0)));
    const std::int64_t end = offset + len;
    for (std::int64_t pos = offset; pos < end;) {
        PieceIndex piece = 0;
        int in_piece = 0;
        std::int64_t n = 0;
        bool on_disk = false;
        {
            std::lock_guard lock(mu_);
            if (cancelled_.load(std::memory_order_acquire)) return make_error_code(errc::cancelled);
            const auto span = mapper_.map(ByteRange{pos, pos});
            piece = span.first;
            in_piece = span.first_offset;
            n = std::min<std::int64_t>(end - pos, mapper_.piece_length() - in_piece);
            const auto i = static_cast<std::size_t>(piece - first_piece_);
            on_disk = i < on_disk_at_start_.size() && on_disk_at_start_[i];
        }
        if (on_disk) {
            auto bytes = read_from_disk(pos, n);
            if (!bytes.ok()) return bytes.error();
            out.insert(out.end(), bytes.value().begin(), bytes.value().end());
            if (static_cast<std::int64_t>(bytes.value().size()) < n) break;
        } else {
            auto bytes = piece_bytes(piece, wait);
            if (!bytes.ok()) return bytes.error();
            const auto take = std::min<std::int64_t>(n, bytes.value().size - in_piece);
            if (take <= 0) break;
            const char* from = bytes.value().data.get() + in_piece;
            out.insert(out.end(), from, from + take);
            if (take < n) break;
        }
        pos += n;
    }
    return out;
}

Result<PieceBuffer> ByteSource::piece_bytes(PieceIndex piece, std::chrono::milliseconds wait) {
    if (auto kept = cache_.get(piece)) {
        ++cache_hits_;
        return *kept;
    }
    auto read = pieces_->read(piece, wait);
    if (read.ok()) {
        ++piece_reads_;
        cache_.put(piece, read.value());
        return read;
    }
    if (read.error() == make_error_code(errc::piece_not_available)) {
        std::lock_guard lock(mu_);
        if (!cancelled_.load(std::memory_order_acquire)) avail_.mark_incomplete(piece);
        return make_error_code(errc::timeout_waiting_for_piece);
    }
    return read.error();
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
    std::lock_guard lock(mu_);
    if (cancelled_.load(std::memory_order_acquire)) return false;
    ByteRange range{offset, offset + len - 1};
    auto span = mapper_.map(range);
    return avail_.is_span_complete(span);
}

PieceIndex ByteSource::piece_at(std::int64_t offset) const {
    std::lock_guard lock(mu_);
    if (cancelled_.load(std::memory_order_acquire)) return -1;
    return mapper_.map(ByteRange{offset, offset}).first;
}

std::int64_t ByteSource::bytes_to_piece_end(std::int64_t offset) const {
    std::lock_guard lock(mu_);
    if (cancelled_.load(std::memory_order_acquire)) return -1;
    auto span = mapper_.map(ByteRange{offset, offset});
    return static_cast<std::int64_t>(mapper_.piece_length()) - span.first_offset;
}

void ByteSource::notify_piece_complete() {
    // Taking the lock orders this after a reader's check of the pieces: a
    // completion landing between that check and the wait would otherwise be
    // missed until the next piece or the timeout.
    { std::lock_guard lock(mu_); }
    cv_.notify_all();
}

void ByteSource::cancel() {
    {
        std::lock_guard lock(mu_);
        cancelled_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
    if (pieces_) pieces_->cancel();
    cache_.clear();
}

std::int64_t ByteSource::file_size() const {
    return file_size_;
}

} // namespace seekserve
