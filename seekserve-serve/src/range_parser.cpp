#include "seekserve/range_parser.hpp"

namespace seekserve {

std::optional<ParsedRange> parse_range_header(const std::string& header, std::int64_t file_size) {
    if (file_size <= 0) return std::nullopt;

    // Must start with "bytes="
    const std::string prefix = "bytes=";
    if (header.size() < prefix.size()) return std::nullopt;
    if (header.compare(0, prefix.size(), prefix) != 0) return std::nullopt;

    auto spec = header.substr(prefix.size());

    // Reject multi-range (contains comma)
    if (spec.find(',') != std::string::npos) return std::nullopt;

    // Trim whitespace
    while (!spec.empty() && spec.front() == ' ') spec.erase(spec.begin());
    while (!spec.empty() && spec.back() == ' ') spec.pop_back();

    if (spec.empty()) return std::nullopt;

    auto dash = spec.find('-');
    if (dash == std::string::npos) return std::nullopt;

    auto left = spec.substr(0, dash);
    auto right = spec.substr(dash + 1);

    // Trim whitespace around parts
    while (!left.empty() && left.back() == ' ') left.pop_back();
    while (!right.empty() && right.front() == ' ') right.erase(right.begin());

    // Case 1: suffix-byte-range-spec: "bytes=-N" (last N bytes)
    if (left.empty()) {
        if (right.empty()) return std::nullopt;
        std::int64_t suffix;
        try {
            suffix = std::stoll(right);
        } catch (...) {
            return std::nullopt;
        }
        if (suffix <= 0) return std::nullopt;
        if (suffix >= file_size) {
            return ParsedRange{0, file_size - 1};
        }
        return ParsedRange{file_size - suffix, file_size - 1};
    }

    // Parse start
    std::int64_t start;
    try {
        start = std::stoll(left);
    } catch (...) {
        return std::nullopt;
    }
    if (start < 0) return std::nullopt;

    // Case 2: open-ended: "bytes=N-" (from N to end)
    if (right.empty()) {
        if (start >= file_size) return std::nullopt;
        return ParsedRange{start, file_size - 1};
    }

    // Case 3: closed range: "bytes=start-end"
    std::int64_t end;
    try {
        end = std::stoll(right);
    } catch (...) {
        return std::nullopt;
    }

    // Inverted range
    if (end < start) return std::nullopt;

    // Start beyond file
    if (start >= file_size) return std::nullopt;

    // Clamp end to file boundary
    if (end >= file_size) end = file_size - 1;

    return ParsedRange{start, end};
}

} // namespace seekserve
