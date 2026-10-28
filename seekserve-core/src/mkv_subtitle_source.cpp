#include "seekserve/mkv_subtitle_source.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "seekserve/mkv_structure.hpp"
#include "seekserve/mp4_mkv_file.hpp"
#include "seekserve/mp4_structure.hpp"

namespace seekserve {

MkvSubtitleSource::MkvSubtitleSource(std::shared_ptr<StreamSource> inner,
                                     std::shared_ptr<const SubtitleTrack> track)
    : inner_(std::move(inner)), track_(std::move(track)) {}

const RebuiltFile* MkvSubtitleSource::layout() const {
    return ready_.load(std::memory_order_acquire);
}

Result<void> MkvSubtitleSource::prepare(std::chrono::milliseconds wait) {
    if (cancelled_.load()) return make_error_code(errc::cancelled);
    if (layout()) return {};
    std::lock_guard prepare_lock(prepare_mu_);
    if (layout()) return {};

    // Every read waits at most `wait`: a missing piece comes back as a
    // timeout, and the next call starts again from the head (the bytes read
    // so far are on disk by then).
    MkvRead read = [&](std::int64_t offset, std::int64_t len) -> Result<std::vector<std::uint8_t>> {
        auto got = inner_->read(offset, len, wait);
        if (!got && got.error() == make_error_code(errc::timeout_waiting_for_piece)) {
            std::lock_guard lock(mu_);
            waiting_for_ = ByteRange{offset, offset + len - 1};
        }
        return got;
    };
    std::string why;
    bool from_mp4 = false;
    auto rebuilt = rebuild(read, why, from_mp4);
    if (!rebuilt) return rebuilt.error();  // a timeout, a cancel, a read error

    std::shared_ptr<const RebuiltFile> built = rebuilt.value();
    SubtitleShareState state = SubtitleShareState::ready;
    if (built) {
        spdlog::info("MkvSubtitleSource: {} subtitle lines in a file of {} bytes{}", track_->cues.size(),
                     built->size(), from_mp4 ? ", from an MP4" : "");
    } else {
        built = std::make_shared<const PieceListFile>(passthrough_layout(inner_->file_size()));
        state = SubtitleShareState::unsupported;
        from_mp4 = false;
        spdlog::warn("MkvSubtitleSource: served without subtitles: {}", why);
    }
    {
        std::lock_guard lock(mu_);
        waiting_for_.reset();
        reason_ = state == SubtitleShareState::ready ? std::string{} : why;
        state_ = state;
        from_mp4_ = from_mp4;
        layout_ = built;
    }
    ready_.store(built.get(), std::memory_order_release);
    return {};
}

std::optional<ByteRange> MkvSubtitleSource::preparing_range() const {
    std::lock_guard lock(mu_);
    return waiting_for_;
}

Result<std::shared_ptr<const RebuiltFile>> MkvSubtitleSource::rebuild(const MkvRead& read, std::string& why,
                                                                       bool& from_mp4) {
    auto structure = read_mkv_structure(read, inner_->file_size(), why);
    if (structure) {
        return std::shared_ptr<const RebuiltFile>(
            std::make_shared<const PieceListFile>(build_mkv_subtitle_layout(structure.value(), *track_)));
    }
    if (structure.error() != make_error_code(errc::invalid_argument)) return structure.error();
    if (why != "not a Matroska file") return std::shared_ptr<const RebuiltFile>();

    auto mp4 = read_mp4_structure(read, inner_->file_size(), why);
    if (!mp4) {
        if (mp4.error() != make_error_code(errc::invalid_argument)) return mp4.error();
        if (why == "not an MP4 file") why = "neither a Matroska nor an MP4 file";
        return std::shared_ptr<const RebuiltFile>();
    }
    auto file = Mp4MkvFile::build(std::move(mp4).value(), *track_, why);
    from_mp4 = file != nullptr;
    return std::shared_ptr<const RebuiltFile>(file);
}

Result<std::vector<std::uint8_t>> MkvSubtitleSource::read(std::int64_t offset, std::int64_t len,
                                                          std::chrono::milliseconds wait) {
    if (cancelled_.load()) return make_error_code(errc::cancelled);
    const auto* l = layout();
    if (!l) return make_error_code(errc::timeout_waiting_for_piece);
    if (offset < 0 || len < 0) return make_error_code(errc::invalid_argument);

    std::vector<std::uint8_t> out;
    auto end = std::min(offset + len, l->size());
    while (offset < end) {
        auto p = l->piece_at(offset);
        if (!p) break;
        auto from = offset - p->at;
        auto n = std::min(p->length - from, end - offset);
        if (p->file_offset < 0) {
            auto first = p->bytes->begin() + p->bytes_offset + from;
            out.insert(out.end(), first, first + n);
        } else {
            auto got = inner_->read(p->file_offset + from, n, wait);
            if (!got) {
                // What is here goes out; the caller asks again for the rest.
                if (!out.empty() && got.error() == make_error_code(errc::timeout_waiting_for_piece)) break;
                return got.error();
            }
            auto& bytes = got.value();
            out.insert(out.end(), bytes.begin(), bytes.end());
            if (static_cast<std::int64_t>(bytes.size()) < n) break;  // the file got shorter
        }
        offset += n;
    }
    return out;
}

std::int64_t MkvSubtitleSource::file_offset(std::int64_t offset) const {
    const auto* l = layout();
    if (!l) return offset;
    // New bytes count as the original byte that follows them: the Cluster
    // their lines come before, or the sample after a block's head.
    auto at = l->file_offset(offset);
    return at >= 0 ? at : std::max<std::int64_t>(0, inner_->file_size() - 1);
}

PieceIndex MkvSubtitleSource::piece_at(std::int64_t offset) const {
    if (cancelled_.load()) return -1;
    return inner_->piece_at(file_offset(offset));
}

std::int64_t MkvSubtitleSource::bytes_to_piece_end(std::int64_t offset) const {
    if (cancelled_.load()) return -1;
    const auto* l = layout();
    if (!l) return -1;
    auto p = l->piece_at(offset);
    if (!p) return -1;
    auto left = p->at + p->length - offset;
    if (p->file_offset < 0) return left;
    auto to_end = inner_->bytes_to_piece_end(p->file_offset + (offset - p->at));
    return to_end > 0 ? std::min(left, to_end) : left;
}

void MkvSubtitleSource::cancel() {
    cancelled_.store(true);
    inner_->cancel();
}

std::int64_t MkvSubtitleSource::file_size() const {
    const auto* l = layout();
    return l ? l->size() : inner_->file_size();
}

std::optional<std::string> MkvSubtitleSource::content_type() const {
    std::lock_guard lock(mu_);
    if (!from_mp4_) return std::nullopt;
    return std::string("video/x-matroska");
}

SubtitleShareState MkvSubtitleSource::state() const {
    std::lock_guard lock(mu_);
    return state_;
}

std::string MkvSubtitleSource::reason() const {
    std::lock_guard lock(mu_);
    return reason_;
}

}  // namespace seekserve
