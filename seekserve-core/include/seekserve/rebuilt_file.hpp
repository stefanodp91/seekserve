#pragma once

#include <cstdint>
#include <optional>

#include "seekserve/mkv_subtitle_layout.hpp"

namespace seekserve {

/// A file a TV share serves in place of the one on disk (app WORK-37 step
/// 7): runs of bytes copied from the original and new ones, whose size and
/// places are known before any byte is read.
class RebuiltFile {
public:
    virtual ~RebuiltFile() = default;

    virtual std::int64_t size() const = 0;
    // The run of bytes that holds `offset`; nullopt past the end.
    virtual std::optional<MkvPiece> piece_at(std::int64_t offset) const = 0;
    // The byte of the original the bytes at `offset` stand for: the copied
    // byte itself, or for new bytes the first copied byte after them; -1
    // when none follows.
    virtual std::int64_t file_offset(std::int64_t offset) const = 0;
};

/// A rebuilt file that is a list of pieces in memory: an MKV with a track
/// added (MkvSubtitleLayout), or a file served as it is.
class PieceListFile final : public RebuiltFile {
public:
    explicit PieceListFile(MkvSubtitleLayout layout) : layout_(std::move(layout)) {}

    const MkvSubtitleLayout& layout() const { return layout_; }

    std::int64_t size() const override { return layout_.size; }
    std::optional<MkvPiece> piece_at(std::int64_t offset) const override {
        auto i = layout_.piece_at(offset);
        if (i >= layout_.pieces.size()) return std::nullopt;
        return layout_.pieces[i];
    }
    std::int64_t file_offset(std::int64_t offset) const override {
        for (auto i = layout_.piece_at(offset); i < layout_.pieces.size(); ++i) {
            const auto& p = layout_.pieces[i];
            if (p.file_offset >= 0) return p.file_offset + std::max<std::int64_t>(0, offset - p.at);
        }
        return -1;
    }

private:
    MkvSubtitleLayout layout_;
};

}  // namespace seekserve
