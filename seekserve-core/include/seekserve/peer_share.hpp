#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <libtorrent/peer_info.hpp>
#include <libtorrent/socket.hpp>

#include "seekserve/types.hpp"

namespace seekserve {

// Who a connected peer is to us, for what our sharing could buy (app
// DEC-995, DEC-996): one with every piece we want gives and wants nothing;
// one still downloading may want pieces of ours, and only that one rewards
// what we send it.
enum class PeerGroup {
    whole,    // has every piece we want (a seed, or a peer of a pack with our episode)
    idle,     // still downloading, wants nothing of ours
    wanting,  // still downloading and interested in pieces of ours
};

inline constexpr int kPeerGroups = 3;

// The group of `peer`, given the pieces we want.
PeerGroup peer_group(const lt::peer_info& peer, const std::vector<PieceIndex>& wanted);

// What each group gave us and got from us: the pieces only, no protocol
// chatter, and how many of its peers were connected at the last reading.
struct PeerGroupShare {
    int peers = 0;
    std::int64_t received = 0;
    std::int64_t sent = 0;
};

// The bytes of pieces each group gave and got since the first reading,
// counted by the increments of each peer's totals between readings: a peer's
// totals start again from zero when it connects again. The peers' addresses
// only tell one reading's peer from the next here; none goes out, nor is
// kept once the peer is gone.
class PeerShareTally {
public:
    void read(const std::vector<lt::peer_info>& peers, const std::vector<PieceIndex>& wanted);

    const PeerGroupShare& share(PeerGroup group) const;

    // How many peers the tally remembers: those of the last reading.
    std::size_t remembered() const { return last_.size(); }

private:
    std::map<lt::tcp::endpoint, std::pair<std::int64_t, std::int64_t>> last_;
    std::array<PeerGroupShare, kPeerGroups> shares_{};
};

}  // namespace seekserve
