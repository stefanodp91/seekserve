#include "seekserve/peer_share.hpp"

#include <algorithm>
#include <utility>

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

void PeerShareBook::set_wanted(std::vector<PieceIndex> wanted) {
    std::lock_guard lock(mu_);
    wanted_ = std::move(wanted);
    wanted_known_ = true;
}

void PeerShareBook::read(ConnectionShare& conn, const lt::peer_info& peer) {
    std::lock_guard lock(mu_);
    if (conn.closed) return;
    count(conn, peer);
}

void PeerShareBook::close(ConnectionShare& conn, const lt::peer_info* last) {
    std::lock_guard lock(mu_);
    if (conn.closed) return;
    if (last) count(conn, *last);
    if (conn.group) --shares_[static_cast<std::size_t>(*conn.group)].peers;
    conn.group.reset();
    conn.closed = true;
}

PeerShares PeerShareBook::shares() const {
    std::lock_guard lock(mu_);
    return shares_;
}

PeerGroup PeerShareBook::group_of(const lt::peer_info& peer) const {
    if (wanted_known_) return peer_group(peer, wanted_);
    if (peer.flags & lt::peer_info::seed) return PeerGroup::whole;
    if (peer.flags & lt::peer_info::remote_interested) return PeerGroup::wanting;
    return PeerGroup::idle;
}

// Called with mu_ held. A connection's totals only grow; one lower than
// before would be libtorrent's, and adds nothing.
void PeerShareBook::count(ConnectionShare& conn, const lt::peer_info& peer) {
    const auto group = group_of(peer);
    auto& share = shares_[static_cast<std::size_t>(group)];
    share.received += std::max<std::int64_t>(0, peer.total_download - conn.received);
    share.sent += std::max<std::int64_t>(0, peer.total_upload - conn.sent);
    conn.received = std::max(conn.received, peer.total_download);
    conn.sent = std::max(conn.sent, peer.total_upload);
    if (conn.group != group) {
        if (conn.group) --shares_[static_cast<std::size_t>(*conn.group)].peers;
        ++share.peers;
        conn.group = group;
    }
}

}  // namespace seekserve
