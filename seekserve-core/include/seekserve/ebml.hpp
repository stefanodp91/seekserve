#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seekserve::ebml {

/// The Matroska elements a rebuilt file reads or writes (app WORK-37 step 7).
enum Id : std::uint32_t {
    kEbml = 0x1A45DFA3,
    kSegment = 0x18538067,
    kSeekHead = 0x114D9B74,
    kSeek = 0x4DBB,
    kSeekId = 0x53AB,
    kSeekPosition = 0x53AC,
    kInfo = 0x1549A966,
    kTimestampScale = 0x2AD7B1,
    kTracks = 0x1654AE6B,
    kTrackEntry = 0xAE,
    kTrackNumber = 0xD7,
    kTrackUid = 0x73C5,
    kTrackType = 0x83,
    kFlagDefault = 0x88,
    kFlagLacing = 0x9C,
    kCodecId = 0x86,
    kLanguage = 0x22B59C,
    kName = 0x536E,
    kCues = 0x1C53BB6B,
    kCuePoint = 0xBB,
    kCueTime = 0xB3,
    kCueTrackPositions = 0xB7,
    kCueTrack = 0xF7,
    kCueClusterPosition = 0xF1,
    kCueRelativePosition = 0xF0,
    kCueDuration = 0xB2,
    kCueBlockNumber = 0x5378,
    kCluster = 0x1F43B675,
    kTimestamp = 0xE7,
    kBlockGroup = 0xA0,
    kBlock = 0xA1,
    kBlockDuration = 0x9B,
    kVoid = 0xEC,
    kCrc32 = 0xBF,
};

constexpr std::uint64_t kSubtitleTrackType = 0x11;

/// The head of an element: its id, the size of its payload and how many
/// bytes the two take. `unknown_size` for a size with every bit set (a
/// live stream's Segment or Cluster).
struct Header {
    std::uint32_t id = 0;
    std::uint64_t size = 0;
    std::size_t length = 0;
    bool unknown_size = false;
};

// The element head at the start of `data`; nullopt when `len` bytes do not
// hold all of it, or the bytes are no element head.
std::optional<Header> read_header(const std::uint8_t* data, std::size_t len);

// An unsigned integer payload of at most 8 bytes; nullopt when longer.
std::optional<std::uint64_t> read_uint(const std::uint8_t* data, std::size_t len);

/// A child element inside a master element's payload, by offsets into it.
struct Child {
    std::uint32_t id;
    std::size_t start;  // the element's head
    std::size_t data;   // its payload
    std::size_t end;    // one past its payload
};

// The children of a master element's payload; nullopt when they do not fill
// it exactly or one of them has an unknown size.
std::optional<std::vector<Child>> children(const std::uint8_t* data, std::size_t len);

// Writers. `width` 0 is the fewest bytes; otherwise exactly that many, so a
// value written later cannot change a length computed before.
void put_id(std::vector<std::uint8_t>& out, std::uint32_t id);
void put_size(std::vector<std::uint8_t>& out, std::uint64_t size, int width = 0);
std::vector<std::uint8_t> element(std::uint32_t id, const std::vector<std::uint8_t>& payload,
                                  int size_width = 0);
std::vector<std::uint8_t> uint_element(std::uint32_t id, std::uint64_t value, int width = 0);
std::vector<std::uint8_t> string_element(std::uint32_t id, const std::string& value);
void append(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes);

}  // namespace seekserve::ebml
