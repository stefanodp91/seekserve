// An MP4 served as a Matroska file (app WORK-37 step 7d): the pieces of the
// head, the Cues and every Cluster, laid out again from its plan when read.

#include <algorithm>

#include <spdlog/spdlog.h>

#include "mp4_mkv_events.hpp"
#include "seekserve/ebml.hpp"
#include "seekserve/mp4_mkv_file.hpp"

namespace seekserve {

namespace {

using mp4mkv::Event;
using mp4mkv::EventWalker;

constexpr std::uint32_t kSimpleBlock = 0xA3;
constexpr std::uint8_t kKeyframe = 0x80;
// The Clusters a TV reads around one place: what it plays, and a jump back.
constexpr std::size_t kCachedClusters = 4;

// The new bytes of event `e` in a Cluster timed `tc`: a SimpleBlock's head
// (its sample follows, copied from the MP4) or a line's whole BlockGroup.
void put_block(std::vector<std::uint8_t>& out, const Event& e, std::int64_t tc,
               std::uint64_t track_number, const SubtitleTrack& lines) {
    std::vector<std::uint8_t> head;
    ebml::put_size(head, track_number);  // the track number is a variable-length number
    auto rel = static_cast<std::uint16_t>(static_cast<std::int16_t>(e.pts_ms - tc));
    head.push_back(static_cast<std::uint8_t>(rel >> 8));
    head.push_back(static_cast<std::uint8_t>(rel & 0xFF));
    if (!e.text) {
        head.push_back(e.sample.key ? kKeyframe : 0x00);
        ebml::put_id(out, kSimpleBlock);
        ebml::put_size(out, head.size() + e.sample.size);
        ebml::append(out, head);
        return;
    }
    head.push_back(0x00);  // no lacing, no flags
    const auto& text = lines.cues[e.line].text;
    head.insert(head.end(), text.begin(), text.end());
    auto group = ebml::element(ebml::kBlock, head);
    ebml::append(group, ebml::uint_element(ebml::kBlockDuration,
                                           static_cast<std::uint64_t>(std::max<std::int64_t>(1, e.end_ms - e.pts_ms))));
    ebml::append(out, ebml::element(ebml::kBlockGroup, group));
}

}  // namespace

Mp4MkvFile::Pieces Mp4MkvFile::cluster_pieces(std::size_t k) const {
    std::lock_guard lock(cache_mu_);
    for (const auto& [index, pieces] : cache_) {
        if (index == k) return pieces;
    }

    const auto& c = clusters_[k];
    auto bytes = std::make_shared<std::vector<std::uint8_t>>();
    auto pieces = std::make_shared<std::vector<MkvPiece>>();
    std::int64_t at = c.at;
    std::size_t new_from = 0;
    auto flush_new = [&] {
        if (bytes->size() == new_from) return;
        auto n = static_cast<std::int64_t>(bytes->size() - new_from);
        pieces->push_back({at, n, -1, bytes, static_cast<std::int64_t>(new_from)});
        at += n;
        new_from = bytes->size();
    };

    ebml::put_id(*bytes, ebml::kCluster);
    std::vector<std::uint8_t> size_bytes;
    // The payload's size: the plan's whole size less the id and this size.
    for (std::size_t len = 1; len <= 8; ++len) {
        auto payload = static_cast<std::uint64_t>(c.size - 4 - static_cast<std::int64_t>(len));
        size_bytes.clear();
        ebml::put_size(size_bytes, payload);
        if (size_bytes.size() == len) break;
    }
    ebml::append(*bytes, size_bytes);
    ebml::append(*bytes, ebml::uint_element(ebml::kTimestamp, static_cast<std::uint64_t>(c.timestamp)));

    EventWalker w(mp4_, track_, shift_ns_, c.cursors, c.first_line, c.samples, c.lines);
    Event e;
    while (w.peek(e)) {
        auto number = e.text ? subtitle_number_ : e.track + 1;
        put_block(*bytes, e, c.timestamp, number, track_);
        if (!e.text) {
            flush_new();
            pieces->push_back({at, e.sample.size, e.sample.offset, nullptr, 0});
            at += e.sample.size;
        }
        w.take(e);
    }
    flush_new();
    if (at != c.at + c.size) {
        spdlog::error("Mp4MkvFile: Cluster {} laid out in {} bytes, planned {}", k, at - c.at, c.size);
    }

    Pieces out = pieces;
    if (cache_.size() >= kCachedClusters) cache_.erase(cache_.begin());
    cache_.emplace_back(k, out);
    return out;
}

std::optional<MkvPiece> Mp4MkvFile::piece_at(std::int64_t offset) const {
    if (offset < 0 || offset >= size_) return std::nullopt;
    auto head = static_cast<std::int64_t>(head_->size());
    if (offset < head) return MkvPiece{0, head, -1, head_, 0};
    if (offset >= cues_at_) return MkvPiece{cues_at_, size_ - cues_at_, -1, cues_, 0};
    auto c = std::upper_bound(clusters_.begin(), clusters_.end(), offset,
                              [](std::int64_t o, const ClusterPlan& p) { return o < p.at; });
    auto pieces = cluster_pieces(static_cast<std::size_t>(std::prev(c) - clusters_.begin()));
    auto p = std::upper_bound(pieces->begin(), pieces->end(), offset,
                              [](std::int64_t o, const MkvPiece& x) { return o < x.at; });
    return *std::prev(p);
}

std::int64_t Mp4MkvFile::file_offset(std::int64_t offset) const {
    if (offset < 0 || offset >= cues_at_) return -1;
    std::size_t k = 0;
    if (offset >= static_cast<std::int64_t>(head_->size())) {
        auto c = std::upper_bound(clusters_.begin(), clusters_.end(), offset,
                                  [](std::int64_t o, const ClusterPlan& p) { return o < p.at; });
        k = static_cast<std::size_t>(std::prev(c) - clusters_.begin());
    }
    for (; k < clusters_.size(); ++k) {
        auto pieces = cluster_pieces(k);
        for (const auto& p : *pieces) {
            if (p.at + p.length <= offset) continue;
            if (p.file_offset >= 0) return p.file_offset + std::max<std::int64_t>(0, offset - p.at);
        }
    }
    return -1;
}

}  // namespace seekserve
