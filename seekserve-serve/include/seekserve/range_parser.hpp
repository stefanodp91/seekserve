#pragma once

#include <string>
#include <optional>
#include <cstdint>

namespace seekserve {

struct ParsedRange {
    std::int64_t start;
    std::int64_t end;  // inclusive
};

/// Parse an RFC 7233 Range header value.
/// Supports single byte-range: "bytes=start-end", "bytes=start-", "bytes=-suffix".
/// Multi-range is NOT supported (returns nullopt).
/// Returns nullopt on malformed input or unsatisfiable range.
std::optional<ParsedRange> parse_range_header(const std::string& header, std::int64_t file_size);

} // namespace seekserve
