#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "seekserve/mkv_subtitle_layout.hpp"
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
/// the head and the Cues of the file and lays out the rebuilt file
/// (MkvSubtitleLayout); every read then comes from `inner` and from the new
/// bytes. A file that cannot be rebuilt (not Matroska, no Cues…) is served
/// as it is, and state() says so.
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

    SubtitleShareState state() const;
    // Why the file is served as it is; empty otherwise.
    std::string reason() const;

private:
    // The layout once prepared, without locking: it never changes after.
    const MkvSubtitleLayout* layout() const;

    const std::shared_ptr<StreamSource> inner_;
    const std::shared_ptr<const SubtitleTrack> track_;

    std::mutex prepare_mu_;         // one preparation at a time
    mutable std::mutex mu_;         // the fields below
    std::optional<ByteRange> waiting_for_;
    std::string reason_;
    SubtitleShareState state_ = SubtitleShareState::pending;
    std::shared_ptr<const MkvSubtitleLayout> layout_;
    std::atomic<const MkvSubtitleLayout*> ready_{nullptr};
    std::atomic<bool> cancelled_{false};
};

}  // namespace seekserve
