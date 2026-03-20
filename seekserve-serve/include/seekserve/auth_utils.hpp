#pragma once

#include <boost/beast/http.hpp>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;

namespace seekserve {

/// Constant-time string comparison to prevent timing attacks.
/// Returns true iff a == b, in O(n) time regardless of content.
inline bool constant_time_compare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile int result = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        result |= (a[i] ^ b[i]);
    }
    return result == 0;
}

/// Extracts the Bearer token from an HTTP request.
/// Checks Authorization header first, then ?token= query parameter.
/// Returns empty string if no token is found.
inline std::string extract_token(const http::request<http::string_body>& req) {
    // 1. Authorization: Bearer <token>
    auto auth_it = req.find(http::field::authorization);
    if (auth_it != req.end()) {
        auto val = std::string(auth_it->value());
        if (val.size() > 7 && val.substr(0, 7) == "Bearer ") {
            return val.substr(7);
        }
    }

    // 2. ?token=<token> query parameter
    auto target = std::string(req.target());
    auto qpos = target.find("token=");
    if (qpos != std::string::npos) {
        auto val_start = qpos + 6;
        auto amp = target.find('&', val_start);
        return target.substr(val_start, amp == std::string::npos ? amp : amp - val_start);
    }

    return {};
}

} // namespace seekserve
