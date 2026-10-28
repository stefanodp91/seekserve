#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "seekserve/error.hpp"

namespace seekserve {

/// Reads [offset, offset+len) of a file: errc::timeout_waiting_for_piece
/// while its bytes are missing, so a caller can try again later.
using Mp4Read = std::function<Result<std::vector<std::uint8_t>>(std::int64_t offset, std::int64_t len)>;

/// The codecs an MP4 track can have for the MKV rebuilt from it (app WORK-37
/// step 7d): the ones Matroska has a codec ID for with the same bytes.
enum class Mp4Codec {
    avc,   // avc1, avc3: V_MPEG4/ISO/AVC, avcC as CodecPrivate
    hevc,  // hvc1, hev1: V_MPEGH/ISO/HEVC, hvcC as CodecPrivate
    aac,   // mp4a with an AAC object type: A_AAC, AudioSpecificConfig
    mp3,   // mp4a with an MPEG-1/2 audio object type, or .mp3: A_MPEG/L3
    ac3,   // ac-3: A_AC3
    eac3,  // ec-3: A_EAC3
};

/// A row of the sample-to-chunk table: from chunk `first_chunk` (0-based),
/// `samples_per_chunk` samples in each chunk.
struct Mp4ChunkRun {
    std::uint32_t first_chunk = 0;
    std::uint32_t samples_per_chunk = 0;
};

/// A video or audio track of an MP4, with its sample tables as the file has
/// them: a two-hour film has about half a million samples, and the tables
/// stay a few MB.
struct Mp4Track {
    std::uint32_t id = 0;
    bool video = false;
    Mp4Codec codec = Mp4Codec::avc;
    std::uint32_t timescale = 0;
    std::string language;                  // ISO 639-2 from mdhd; "und" when not said
    std::vector<std::uint8_t> codec_private;  // avcC, hvcC or AudioSpecificConfig payload
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;
    std::uint32_t sample_rate = 0;

    // The edit list: the media starts at `media_start` (track units), after
    // `delay_ns` of nothing (empty edits).
    std::int64_t media_start = 0;
    std::int64_t delay_ns = 0;

    std::vector<std::pair<std::uint32_t, std::uint32_t>> time_to_sample;  // count, delta
    std::vector<std::pair<std::uint32_t, std::int32_t>> composition;     // count, offset
    std::vector<Mp4ChunkRun> chunk_runs;
    std::vector<std::int64_t> chunk_offsets;
    std::uint32_t fixed_size = 0;          // when every sample has this size
    std::vector<std::uint32_t> sizes;      // otherwise
    std::vector<std::uint32_t> sync;       // 0-based keyframes, in order
    bool all_sync = true;                  // no sync table: every sample is a keyframe
    std::uint64_t sample_count = 0;

    // A time of the media (track units) on the film's time line, in ns.
    std::int64_t presentation_ns(std::int64_t media_time) const;
};

/// What an MKV rebuilt from an MP4 needs of it (app WORK-37 step 7d): its
/// video and audio tracks with their sample tables. The samples themselves
/// are never read.
struct Mp4Structure {
    std::int64_t file_size = 0;
    std::uint32_t movie_timescale = 0;
    std::int64_t moov_start = 0;
    std::int64_t moov_end = 0;
    std::vector<Mp4Track> tracks;  // in the file's order; a video track among them
};

/// A sample of a track: where it is in the file, its decode and presentation
/// times as the file says them (track units, before the edit list) and
/// whether it is a keyframe.
struct Mp4Sample {
    std::uint64_t index = 0;
    std::int64_t offset = 0;
    std::uint32_t size = 0;
    std::int64_t dts = 0;
    std::int64_t pts = 0;
    bool key = false;
};

/// Walks the samples of a track in order. A copy goes on from where the
/// original was: a rebuilt file keeps one per Cluster instead of every
/// sample.
class Mp4SampleCursor {
public:
    explicit Mp4SampleCursor(const Mp4Track& track);

    // The next sample; false past the last one.
    bool next(Mp4Sample& out);
    // The index of the sample next() gives.
    std::uint64_t index() const { return index_; }

private:
    const Mp4Track* track_;
    std::uint64_t index_ = 0;
    std::size_t chunk_ = 0;
    std::size_t run_ = 0;               // in chunk_runs
    std::uint32_t left_in_chunk_ = 0;
    std::int64_t offset_ = 0;
    std::size_t stts_ = 0;
    std::uint32_t stts_left_ = 0;
    std::size_t ctts_ = 0;
    std::uint32_t ctts_left_ = 0;
    std::int64_t dts_ = 0;
    std::size_t sync_ = 0;
};

// The structure of the MP4 of `file_size` bytes that `read` reads. A timeout
// of `read` comes back as it is. A file this cannot rebuild is
// errc::invalid_argument, with the reason in `why`: not an MP4, no moov, a
// fragmented MP4, a track with more than one sample description, an edit
// list with more than one part, a codec Matroska cannot take as it is,
// samples outside the file, or tables too large or malformed.
Result<Mp4Structure> read_mp4_structure(const Mp4Read& read, std::int64_t file_size,
                                        std::string& why);

}  // namespace seekserve
