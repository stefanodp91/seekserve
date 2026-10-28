#include "seekserve/ebml.hpp"

namespace seekserve::ebml {

namespace {

// Bytes taken by a variable-length number whose first byte is `first`: one
// more for each leading zero bit; 0 for a byte with no marker in `max` bits.
std::size_t vint_length(std::uint8_t first, std::size_t max) {
    for (std::size_t n = 1; n <= max; ++n) {
        if (first & (0x80u >> (n - 1))) return n;
    }
    return 0;
}

int fewest_bytes(std::uint64_t value) {
    int n = 1;
    while (n < 8 && (value >> (8 * n)) != 0) ++n;
    return n;
}

}  // namespace

std::optional<Header> read_header(const std::uint8_t* data, std::size_t len) {
    if (len == 0) return std::nullopt;
    auto id_len = vint_length(data[0], 4);
    if (id_len == 0 || id_len > len) return std::nullopt;
    Header h;
    for (std::size_t i = 0; i < id_len; ++i) h.id = (h.id << 8) | data[i];

    if (id_len == len) return std::nullopt;
    auto size_len = vint_length(data[id_len], 8);
    if (size_len == 0 || id_len + size_len > len) return std::nullopt;
    std::uint64_t size = data[id_len] & (0xFFu >> size_len);
    bool all_ones = size == (0xFFu >> size_len);
    for (std::size_t i = 1; i < size_len; ++i) {
        auto byte = data[id_len + i];
        size = (size << 8) | byte;
        all_ones = all_ones && byte == 0xFF;
    }
    h.size = size;
    h.unknown_size = all_ones;
    h.length = id_len + size_len;
    return h;
}

std::optional<std::uint64_t> read_uint(const std::uint8_t* data, std::size_t len) {
    if (len > 8) return std::nullopt;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < len; ++i) value = (value << 8) | data[i];
    return value;
}

std::optional<std::vector<Child>> children(const std::uint8_t* data, std::size_t len) {
    std::vector<Child> out;
    std::size_t pos = 0;
    while (pos < len) {
        auto h = read_header(data + pos, len - pos);
        if (!h || h->unknown_size || h->size > len - pos - h->length) return std::nullopt;
        auto payload = pos + h->length;
        out.push_back(Child{h->id, pos, payload, payload + static_cast<std::size_t>(h->size)});
        pos = out.back().end;
    }
    return out;
}

void put_id(std::vector<std::uint8_t>& out, std::uint32_t id) {
    int n = id > 0xFFFFFF ? 4 : id > 0xFFFF ? 3 : id > 0xFF ? 2 : 1;
    for (int i = n - 1; i >= 0; --i) out.push_back(static_cast<std::uint8_t>(id >> (8 * i)));
}

void put_size(std::vector<std::uint8_t>& out, std::uint64_t size, int width) {
    if (width == 0) {
        width = 1;
        // A value of all ones in its width would mean "unknown size".
        while (width < 8 && size >= (std::uint64_t{1} << (7 * width)) - 1) ++width;
    }
    auto marked = (std::uint64_t{1} << (7 * width)) | size;
    for (int i = width - 1; i >= 0; --i) out.push_back(static_cast<std::uint8_t>(marked >> (8 * i)));
}

std::vector<std::uint8_t> element(std::uint32_t id, const std::vector<std::uint8_t>& payload,
                                  int size_width) {
    std::vector<std::uint8_t> out;
    out.reserve(payload.size() + 12);
    put_id(out, id);
    put_size(out, payload.size(), size_width);
    append(out, payload);
    return out;
}

std::vector<std::uint8_t> uint_element(std::uint32_t id, std::uint64_t value, int width) {
    if (width == 0) width = fewest_bytes(value);
    std::vector<std::uint8_t> payload;
    for (int i = width - 1; i >= 0; --i) payload.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    return element(id, payload);
}

std::vector<std::uint8_t> string_element(std::uint32_t id, const std::string& value) {
    return element(id, std::vector<std::uint8_t>(value.begin(), value.end()));
}

void append(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

}  // namespace seekserve::ebml
