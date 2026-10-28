#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "seekserve/error.hpp"

namespace seekserve {

/// Reads [offset, offset+len) of a file: errc::timeout_waiting_for_piece
/// while its bytes are missing, so a caller can try again later.
using MkvRead = std::function<Result<std::vector<std::uint8_t>>(std::int64_t offset, std::int64_t len)>;

/// A top-level element of a Matroska Segment, by offsets in the file.
struct MkvElement {
    std::uint32_t id = 0;
    std::int64_t start = 0;  // its head
    std::int64_t data = 0;   // its payload
    std::int64_t end = 0;    // one past its payload
};

/// An entry of a SeekHead: an element and where it starts in the file.
struct MkvSeekEntry {
    std::uint32_t id = 0;
    std::int64_t position = 0;
};

/// A point of the Cues: a time and, for each track it names, the Cluster
/// that holds it, with the other children of its CueTrackPositions.
struct MkvCuePoint {
    struct Position {
        std::uint64_t track = 0;
        std::int64_t cluster = 0;            // in the file
        std::vector<std::uint8_t> rest;      // CueRelativePosition, CueDuration…
        std::vector<std::uint8_t> relative;  // the bytes that name a place in the Cluster
    };
    std::uint64_t time = 0;  // in TimestampScale units
    std::vector<Position> positions;
};

/// What a rebuilt file needs of a Matroska file (app WORK-37 step 7): its
/// head up to the first Cluster, its Cues and the elements after its
/// Clusters, which are the bytes a TV reads first. The Clusters themselves
/// are never read.
struct MkvStructure {
    std::int64_t file_size = 0;
    std::int64_t segment_start = 0;
    std::int64_t segment_data = 0;  // Cues and SeekHead positions count from here
    std::int64_t segment_end = 0;
    std::vector<MkvElement> head;   // before the first Cluster
    std::int64_t first_cluster = 0;
    std::int64_t clusters_end = 0;  // where the elements after the Clusters start
    std::vector<MkvElement> tail;   // from clusters_end to segment_end
    std::map<std::int64_t, std::vector<MkvSeekEntry>> seek_heads;  // by element start
    std::int64_t cues_start = 0;
    std::vector<MkvCuePoint> cues;
    std::uint64_t timestamp_scale = 1000000;
    std::vector<std::uint8_t> tracks;  // the Tracks payload
};

// The structure of the Matroska file of `file_size` bytes that `read` reads.
// A timeout of `read` comes back as it is. A file this cannot rebuild is
// errc::invalid_argument, with the reason in `why`: not Matroska, a live
// stream with an unknown size, no Cues, Clusters after the Cues, or an
// element too large or malformed.
Result<MkvStructure> read_mkv_structure(const MkvRead& read, std::int64_t file_size,
                                        std::string& why);

}  // namespace seekserve
