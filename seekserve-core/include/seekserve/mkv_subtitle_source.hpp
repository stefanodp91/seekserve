#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "seekserve/rebuilt_file.hpp"
#include "seekserve/stream_source.hpp"
#include "seekserve/subtitle_track.hpp"

namespace seekserve {

/// Where the subtitles of a TV share are.
enum class SubtitleShareState {
    pending,      // the head and the Cues of the file are not read yet
    ready,        // the file served has the subtitle track
    unsupported,  // the file is served as it is: see the reason
};

/// The file of `inner` with a subtitle track inside, for a TV that shows only
/// the subtitles in the file (app WORK-37 step 7, DEC-618). prepare() reads
/// the head and the Cues of a Matroska file and lays out the rebuilt file
/// (MkvSubtitleLayout), or the index of an MP4 and lays out a Matroska file
/// from it (Mp4MkvFile, step 7d); every read then comes from `inner` and
/// from the new bytes. A file that cannot be rebuilt (neither, no Cues, a
/// fragmented MP4…) is served as it is, and state() says so.
class MkvSubtitleSource final : public StreamSource {
public:
    MkvSubtitleSource(std::shared_ptr<StreamSource> inner, std::shared_ptr<const SubtitleTrack> track);

    Result<std::vector<std::uint8_t>> read(std::int64_t offset, std::int64_t len,
                                           std::chrono::milliseconds wait) override;
    PieceIndex piece_at(std::int64_t offset) const override;
    std::int64_t bytes_to_piece_end(std::int64_t offset) const override;
    // Cancels `inner` too: the source it wraps is the share's.
    void cancel() override;
    std::int64_t file_size() const override;
    Result<void> prepare(std::chrono::milliseconds wait) override;
    std::optional<ByteRange> preparing_range() const override;
    std::int64_t file_offset(std::int64_t offset) const override;
    // "video/x-matroska" for an MP4 served as Matroska.
    std::optional<std::string> content_type() const override;

    SubtitleShareState state() const;
    // Why the file is served as it is; empty otherwise.
    std::string reason() const;

private:
    // The file once prepared, without locking: it never changes after.
    const RebuiltFile* layout() const;
    // The file for what `read` reads, or nullptr with the reason in `why`;
    // a timeout comes back as it is. `from_mp4` when an MP4 is laid out.
    Result<std::shared_ptr<const RebuiltFile>> rebuild(const MkvRead& read, std::string& why, bool& from_mp4);

    const std::shared_ptr<StreamSource> inner_;
    const std::shared_ptr<const SubtitleTrack> track_;

    std::mutex prepare_mu_;         // one preparation at a time
    mutable std::mutex mu_;         // the fields below
    std::optional<ByteRange> waiting_for_;
    std::string reason_;
    SubtitleShareState state_ = SubtitleShareState::pending;
    bool from_mp4_ = false;
    std::shared_ptr<const RebuiltFile> layout_;
    std::atomic<const RebuiltFile*> ready_{nullptr};
    std::atomic<bool> cancelled_{false};
};

}  // namespace seekserve
