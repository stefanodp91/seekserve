#pragma once

#include <system_error>
#include <string>
#include <variant>

namespace seekserve {

enum class errc {
    success = 0,
    torrent_not_found,
    file_not_found,
    metadata_not_ready,
    piece_not_available,
    timeout_waiting_for_piece,
    range_not_satisfiable,
    server_already_running,
    session_not_started,
    invalid_argument,
    io_error,
    cancelled,
};

const std::error_category& seekserve_category() noexcept;

std::error_code make_error_code(errc e) noexcept;

template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(std::error_code ec) : data_(ec) {}

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    const T& value() const& { return std::get<T>(data_); }
    T& value() & { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }

    std::error_code error() const {
        if (ok()) return {};
        return std::get<std::error_code>(data_);
    }

private:
    std::variant<T, std::error_code> data_;
};

template<>
class Result<void> {
public:
    Result() : ec_{} {}
    Result(std::error_code ec) : ec_(ec) {}

    bool ok() const { return !ec_; }
    explicit operator bool() const { return ok(); }
    std::error_code error() const { return ec_; }

private:
    std::error_code ec_;
};

} // namespace seekserve

namespace std {
template<>
struct is_error_code_enum<seekserve::errc> : true_type {};
}
