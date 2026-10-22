#include "seekserve/file_source.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace seekserve {

Result<std::shared_ptr<FileSource>> FileSource::open(const std::string& path) {
    // O_NOFOLLOW: a link in the last place is refused, not followed.
    int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) return make_error_code(errc::file_not_found);
        if (errno == ELOOP || errno == EISDIR) return make_error_code(errc::invalid_argument);
        return make_error_code(errc::io_error);
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        return make_error_code(errc::io_error);
    }
    if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        return make_error_code(errc::invalid_argument);
    }
    return std::shared_ptr<FileSource>(new FileSource(fd, static_cast<std::int64_t>(st.st_size)));
}

FileSource::FileSource(int fd, std::int64_t size) : fd_(fd), size_(size) {}

FileSource::~FileSource() {
    ::close(fd_);
}

Result<std::vector<std::uint8_t>> FileSource::read(std::int64_t offset, std::int64_t len,
                                                   std::chrono::milliseconds) {
    if (cancelled_.load()) return make_error_code(errc::cancelled);
    if (offset < 0 || len < 0) return make_error_code(errc::invalid_argument);
    if (offset >= size_) return std::vector<std::uint8_t>{};
    auto want = std::min(len, size_ - offset);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(want));
    std::int64_t done = 0;
    while (done < want) {
        // pread: no shared file position, so readers on other threads
        // need no lock.
        auto n = ::pread(fd_, buf.data() + done, static_cast<std::size_t>(want - done),
                         static_cast<off_t>(offset + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return make_error_code(errc::io_error);
        }
        if (n == 0) break;  // the file got shorter since it was opened
        done += n;
    }
    buf.resize(static_cast<std::size_t>(done));
    return buf;
}

PieceIndex FileSource::piece_at(std::int64_t) const {
    return cancelled_.load() ? -1 : 0;
}

std::int64_t FileSource::bytes_to_piece_end(std::int64_t) const {
    return -1;
}

void FileSource::cancel() {
    cancelled_.store(true);
}

std::int64_t FileSource::file_size() const {
    return size_;
}

} // namespace seekserve
