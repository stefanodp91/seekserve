#include "seekserve/mkv_subtitle_source.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "seekserve/mkv_structure.hpp"

namespace seekserve {

MkvSubtitleSource::MkvSubtitleSource(std::shared_ptr<StreamSource> inner,
                                     std::shared_ptr<const SubtitleTrack> track)
    : inner_(std::move(inner)), track_(std::move(track)) {}

const MkvSubtitleLayout* MkvSubtitleSource::layout() const {
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
    auto structure = read_mkv_structure(read, inner_->file_size(), why);
    if (!structure && structure.error() != make_error_code(errc::invalid_argument)) {
        return structure.error();  // a timeout, a cancel, a read error
    }

    std::shared_ptr<const MkvSubtitleLayout> built;
    SubtitleShareState state;
    if (structure) {
        built = std::make_shared<const MkvSubtitleLayout>(
            build_mkv_subtitle_layout(structure.value(), *track_));
        state = SubtitleShareState::ready;
        spdlog::info("MkvSubtitleSource: {} subtitle lines in a file of {} bytes",
                     track_->cues.size(), built->size);
    } else {
        built = std::make_shared<const MkvSubtitleLayout>(passthrough_layout(inner_->file_size()));
        state = SubtitleShareState::unsupported;
        spdlog::warn("MkvSubtitleSource: served without subtitles: {}", why);
    }
    {
        std::lock_guard lock(mu_);
        waiting_for_.reset();
        reason_ = state == SubtitleShareState::ready ? std::string{} : why;
        state_ = state;
        layout_ = built;
    }
    ready_.store(built.get(), std::memory_order_release);
    return {};
}

std::optional<ByteRange> MkvSubtitleSource::preparing_range() const {
    std::lock_guard lock(mu_);
    return waiting_for_;
}

Result<std::vector<std::uint8_t>> MkvSubtitleSource::read(std::int64_t offset, std::int64_t len,
                                                          std::chrono::milliseconds wait) {
    if (cancelled_.load()) return make_error_code(errc::cancelled);
    const auto* l = layout();
    if (!l) return make_error_code(errc::timeout_waiting_for_piece);
    if (offset < 0 || len < 0) return make_error_code(errc::invalid_argument);

    std::vector<std::uint8_t> out;
    auto end = std::min(offset + len, l->size);
    for (auto i = l->piece_at(offset); i < l->pieces.size() && offset < end; ++i) {
        const auto& p = l->pieces[i];
        auto from = offset - p.at;
        auto n = std::min(p.length - from, end - offset);
        if (p.file_offset < 0) {
            auto first = p.bytes->begin() + p.bytes_offset + from;
            out.insert(out.end(), first, first + n);
        } else {
            auto got = inner_->read(p.file_offset + from, n, wait);
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
    // their lines come before.
    for (auto i = l->piece_at(offset); i < l->pieces.size(); ++i) {
        const auto& p = l->pieces[i];
        if (p.file_offset >= 0) return p.file_offset + std::max<std::int64_t>(0, offset - p.at);
    }
    return std::max<std::int64_t>(0, inner_->file_size() - 1);
}

PieceIndex MkvSubtitleSource::piece_at(std::int64_t offset) const {
    if (cancelled_.load()) return -1;
    return inner_->piece_at(file_offset(offset));
}

std::int64_t MkvSubtitleSource::bytes_to_piece_end(std::int64_t offset) const {
    if (cancelled_.load()) return -1;
    const auto* l = layout();
    if (!l) return -1;
    auto i = l->piece_at(offset);
    if (i >= l->pieces.size()) return -1;
    const auto& p = l->pieces[i];
    auto left = p.at + p.length - offset;
    if (p.file_offset < 0) return left;
    auto to_end = inner_->bytes_to_piece_end(p.file_offset + (offset - p.at));
    return to_end > 0 ? std::min(left, to_end) : left;
}

void MkvSubtitleSource::cancel() {
    cancelled_.store(true);
    inner_->cancel();
}

std::int64_t MkvSubtitleSource::file_size() const {
    const auto* l = layout();
    return l ? l->size : inner_->file_size();
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
