#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "seekserve/mkv_structure.hpp"
#include "seekserve/subtitle_track.hpp"

namespace seekserve {

/// A run of bytes of a rebuilt file: copied from the original file, from
/// `file_offset`, or new (`bytes`, from `bytes_offset`).
struct MkvPiece {
    std::int64_t at = 0;            // where it starts in the rebuilt file
    std::int64_t length = 0;
    std::int64_t file_offset = -1;  // -1 for new bytes
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::int64_t bytes_offset = 0;
};

/// A Matroska file with a subtitle track added (app WORK-37 step 7,
/// DEC-618): the original's Clusters copied byte for byte, a small Cluster of
/// subtitle lines before each Cluster the Cues name, a new default text
/// track, and the SeekHead, Cues and Segment size rewritten. Its size and
/// the place of every byte are known before any Cluster is read, so a TV can
/// ask for any range and jump.
struct MkvSubtitleLayout {
    std::vector<MkvPiece> pieces;  // in order, without gaps
    std::int64_t size = 0;
    std::uint64_t track_number = 0;

    // The piece holding byte `offset`; pieces.size() past the end.
    std::size_t piece_at(std::int64_t offset) const;
};

// The rebuilt file for `structure` and the checked `track`.
MkvSubtitleLayout build_mkv_subtitle_layout(const MkvStructure& structure,
                                            const SubtitleTrack& track);

// A layout that is the original file as it is: for a file that cannot be
// rebuilt, served without the subtitles.
MkvSubtitleLayout passthrough_layout(std::int64_t file_size);

}  // namespace seekserve
