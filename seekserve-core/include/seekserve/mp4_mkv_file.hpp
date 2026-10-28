#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "seekserve/mp4_structure.hpp"
#include "seekserve/rebuilt_file.hpp"
#include "seekserve/subtitle_track.hpp"

namespace seekserve {

/// An MP4 served as a Matroska file with a subtitle track (app WORK-37 step
/// 7d): a TV that does not show the text tracks of MP4s shows the ones
/// inside an MKV. The samples of the MP4 are copied as they are, in
/// Clusters that start at the video's keyframes, with the lines among them
/// at their times and the Cues after them. Only the Clusters' starting
/// points are kept; the bytes of a Cluster are laid out again when read,
/// so a film of two hours takes a few hundred KB instead of tens of MB.
class Mp4MkvFile final : public RebuiltFile {
public:
    // The file for `mp4` and the checked `track`; nullptr with the reason in
    // `why` for an MP4 whose times or size this cannot lay out.
    static std::shared_ptr<const Mp4MkvFile> build(Mp4Structure mp4, const SubtitleTrack& track,
                                                   std::string& why);

    std::int64_t size() const override { return size_; }
    std::optional<MkvPiece> piece_at(std::int64_t offset) const override;
    std::int64_t file_offset(std::int64_t offset) const override;

    std::uint64_t subtitle_track() const { return subtitle_number_; }
    std::size_t cluster_count() const { return clusters_.size(); }

    /// Where a Cluster starts: the state of every track before its first
    /// sample, and of the lines.
    struct ClusterPlan {
        std::int64_t at = 0;      // in the file
        std::int64_t size = 0;    // the whole element
        std::int64_t timestamp = 0;  // ms
        std::vector<Mp4SampleCursor> cursors;
        std::vector<std::uint32_t> samples;  // per track
        std::size_t first_line = 0;
        std::size_t lines = 0;
    };

    // Use build(); public for std::make_shared.
    Mp4MkvFile() = default;

private:
    friend class Mp4MkvPlanner;
    using Pieces = std::shared_ptr<const std::vector<MkvPiece>>;

    // The pieces of Cluster `k`, laid out again or from the last ones.
    Pieces cluster_pieces(std::size_t k) const;

    Mp4Structure mp4_;
    SubtitleTrack track_;
    std::int64_t shift_ns_ = 0;  // Matroska has no time before zero
    std::uint64_t subtitle_number_ = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> head_;
    std::shared_ptr<const std::vector<std::uint8_t>> cues_;
    std::vector<ClusterPlan> clusters_;
    std::int64_t cues_at_ = 0;
    std::int64_t size_ = 0;

    mutable std::mutex cache_mu_;
    mutable std::vector<std::pair<std::size_t, Pieces>> cache_;  // the last Clusters read
};

}  // namespace seekserve
