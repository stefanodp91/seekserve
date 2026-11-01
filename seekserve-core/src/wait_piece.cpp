#include "seekserve/wait_piece.hpp"

#include <algorithm>
#include <set>

namespace seekserve {

namespace {

constexpr int kBytesPerKilobyte = 1024;

bool has(const lt::peer_info& peer, PieceIndex piece) {
    if (peer.flags & lt::peer_info::seed) return true;
    const lt::piece_index_t index{piece};
    return piece >= 0 && piece < peer.pieces.size() && peer.pieces.get_bit(index);
}

const lt::peer_info* peer_at(const std::vector<lt::peer_info>& peers, const lt::tcp::endpoint& at) {
    for (const auto& peer : peers) {
        if (peer.remote_endpoint() == at) return &peer;
    }
    return nullptr;
}

}  // namespace

bool someone_sends_it(const WaitPieceStats& stats) { return stats.peers_open > 0; }

std::map<PieceIndex, std::int64_t> blocks_there(const std::vector<lt::partial_piece_info>& queue,
                                                const std::function<int(PieceIndex)>& piece_size) {
    // libtorrent's own block size, smaller only in a smaller piece.
    constexpr int kBlockSize = 16 * 1024;
    std::map<PieceIndex, std::int64_t> there;
    for (const auto& partial : queue) {
        if (partial.blocks == nullptr) continue;
        const auto piece = static_cast<PieceIndex>(partial.piece_index);
        const int size = piece_size(piece);
        std::int64_t bytes = 0;
        for (int b = 0; b < partial.blocks_in_piece; ++b) {
            const auto& block = partial.blocks[b];
            if (block.state == lt::block_info::finished || block.state == lt::block_info::writing) {
                bytes += std::max(0, std::min(kBlockSize, size - b * kBlockSize));
            } else if (block.state == lt::block_info::requested) {
                // Its first bytes, as libtorrent's file_progress counts them.
                bytes += block.bytes_progress;
            }
        }
        there[piece] = bytes;
    }
    return there;
}

SpanBytes span_bytes(PieceIndex first, PieceIndex last, const std::function<bool(PieceIndex)>& have,
                     const std::function<int(PieceIndex)>& piece_size,
                     const std::map<PieceIndex, std::int64_t>& on_its_way) {
    SpanBytes bytes;
    for (PieceIndex p = first; p <= last; ++p) {
        const int size = piece_size(p);
        bytes.size += size;
        if (have(p)) {
            bytes.downloaded += size;
        } else if (auto it = on_its_way.find(p); it != on_its_way.end()) {
            bytes.downloaded += it->second;
        }
    }
    return bytes;
}

int file_holders(const std::vector<PieceIndex>& missing, const std::vector<lt::peer_info>& peers) {
    return static_cast<int>(std::count_if(peers.begin(), peers.end(), [&](const lt::peer_info& peer) {
        return std::all_of(missing.begin(), missing.end(),
                           [&](PieceIndex piece) { return has(peer, piece); });
    }));
}

WaitPieceStats wait_piece_stats(PieceIndex piece, int blocks_in_piece,
                                const std::vector<lt::peer_info>& peers,
                                const std::vector<lt::partial_piece_info>& queue) {
    WaitPieceStats stats;
    stats.piece = piece;
    for (const auto& peer : peers) {
        if (!has(peer, piece)) continue;
        ++stats.peers;
        if (!(peer.flags & (lt::peer_info::remote_choked | lt::peer_info::snubbed))) {
            ++stats.peers_open;
        }
    }

    stats.blocks = blocks_in_piece;
    stats.free = blocks_in_piece;
    const auto mine = std::find_if(queue.begin(), queue.end(), [&](const auto& partial) {
        return static_cast<PieceIndex>(partial.piece_index) == piece;
    });
    if (mine == queue.end() || mine->blocks == nullptr) return stats;

    stats.blocks = mine->blocks_in_piece;
    stats.free = 0;
    std::set<lt::tcp::endpoint> askers;
    for (int i = 0; i < mine->blocks_in_piece; ++i) {
        const auto& block = mine->blocks[i];
        switch (block.state) {
            case lt::block_info::finished:
            case lt::block_info::writing:
                ++stats.done;
                break;
            case lt::block_info::requested:
                ++stats.asked;
                if (block.num_peers > 0) askers.insert(block.peer());
                break;
            default:
                ++stats.free;
                break;
        }
    }

    stats.askers = static_cast<int>(askers.size());
    bool first = true;
    for (const auto& at : askers) {
        const auto* peer = peer_at(peers, at);
        if (peer == nullptr) continue;
        const int kbs = (peer->down_speed + kBytesPerKilobyte / 2) / kBytesPerKilobyte;
        stats.slowest_kbs = first ? kbs : std::min(stats.slowest_kbs, kbs);
        stats.fastest_kbs = first ? kbs : std::max(stats.fastest_kbs, kbs);
        first = false;
    }
    return stats;
}

std::int64_t WaitPieceWatch::still_ms(PieceIndex piece, int done, Clock::time_point now) {
    if (piece != piece_ || done != done_) {
        piece_ = piece;
        done_ = done;
        since_ = now;
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - since_).count();
}

}  // namespace seekserve
