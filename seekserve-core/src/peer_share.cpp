#include "seekserve/peer_share.hpp"

#include <algorithm>

namespace seekserve {

PeerGroup peer_group(const lt::peer_info& peer, const std::vector<PieceIndex>& wanted) {
    if (peer.flags & lt::peer_info::seed) return PeerGroup::whole;
    const auto& has = peer.pieces;
    const bool whole = !has.empty() && std::all_of(wanted.begin(), wanted.end(), [&](PieceIndex p) {
        return p >= 0 && p < has.size() && has.get_bit(lt::piece_index_t{p});
    });
    if (whole) return PeerGroup::whole;
    if (peer.flags & lt::peer_info::remote_interested) return PeerGroup::wanting;
    return PeerGroup::idle;
}

void PeerShareTally::read(const std::vector<lt::peer_info>& peers, const std::vector<PieceIndex>& wanted) {
    std::map<lt::tcp::endpoint, std::pair<std::int64_t, std::int64_t>> now;
    for (auto& share : shares_) share.peers = 0;
    for (const auto& peer : peers) {
        const auto key = peer.remote_endpoint();
        std::int64_t received = peer.total_download;
        std::int64_t sent = peer.total_upload;
        if (auto it = last_.find(key); it != last_.end()) {
            // Lower than before: the peer connected again and its totals
            // started from zero; all of them are new.
            if (received >= it->second.first) received -= it->second.first;
            if (sent >= it->second.second) sent -= it->second.second;
        }
        auto& share = shares_[static_cast<std::size_t>(peer_group(peer, wanted))];
        ++share.peers;
        share.received += received;
        share.sent += sent;
        now[key] = {peer.total_download, peer.total_upload};
    }
    last_ = std::move(now);
}

const PeerGroupShare& PeerShareTally::share(PeerGroup group) const {
    return shares_[static_cast<std::size_t>(group)];
}

}  // namespace seekserve
