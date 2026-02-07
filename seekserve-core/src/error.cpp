#include "seekserve/error.hpp"

namespace seekserve {

namespace {

class SeekServeCategory : public std::error_category {
public:
    const char* name() const noexcept override { return "seekserve"; }

    std::string message(int ev) const override {
        switch (static_cast<errc>(ev)) {
            case errc::success: return "success";
            case errc::torrent_not_found: return "torrent not found";
            case errc::file_not_found: return "file not found";
            case errc::metadata_not_ready: return "metadata not ready";
            case errc::piece_not_available: return "piece not available";
            case errc::timeout_waiting_for_piece: return "timeout waiting for piece";
            case errc::range_not_satisfiable: return "range not satisfiable";
            case errc::server_already_running: return "server already running";
            case errc::session_not_started: return "session not started";
            case errc::invalid_argument: return "invalid argument";
            case errc::io_error: return "I/O error";
            case errc::cancelled: return "cancelled";
            default: return "unknown error";
        }
    }
};

const SeekServeCategory category_instance{};

} // namespace

const std::error_category& seekserve_category() noexcept {
    return category_instance;
}

std::error_code make_error_code(errc e) noexcept {
    return {static_cast<int>(e), seekserve_category()};
}

} // namespace seekserve
