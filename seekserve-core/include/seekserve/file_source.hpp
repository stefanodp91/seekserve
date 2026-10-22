#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "seekserve/stream_source.hpp"

namespace seekserve {

/// A whole file on disk as a stream source: a download that is no torrent's
/// any more, served to a TV (app WORK-37, DEC-627). It has no pieces, so a
/// server neither waits nor reports a position for it. The file is held open
/// from open() on: what is read is the file that was checked, whatever
/// happens to its path later.
class FileSource final : public StreamSource {
public:
    // errc::file_not_found for a path with nothing there, invalid_argument
    // for anything that is not a regular file (a folder) or a final link,
    // io_error when it cannot be read.
    static Result<std::shared_ptr<FileSource>> open(const std::string& path);
    ~FileSource() override;

    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    Result<std::vector<std::uint8_t>> read(std::int64_t offset, std::int64_t len,
                                           std::chrono::milliseconds wait) override;
    PieceIndex piece_at(std::int64_t offset) const override;
    std::int64_t bytes_to_piece_end(std::int64_t offset) const override;
    void cancel() override;
    std::int64_t file_size() const override;

private:
    FileSource(int fd, std::int64_t size);

    const int fd_;
    const std::int64_t size_;
    std::atomic<bool> cancelled_{false};
};

} // namespace seekserve
