#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#include <libtorrent/peer_info.hpp>

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
// chatter, and how many of its peers are connected.
struct PeerGroupShare {
    int peers = 0;
    std::int64_t received = 0;
    std::int64_t sent = 0;
};

using PeerShares = std::array<PeerGroupShare, kPeerGroups>;

// One connection as its book last read it: its totals then and its group.
// The totals count from where they stood when it was established.
// Each connection has its own, so two peers are never taken for one, even
// with the same address (app BUG-792: the web seeds behind the SOCKS5 proxy
// all have 0.0.0.0 and the port of their URL).
struct ConnectionShare {
    std::int64_t received = 0;
    std::int64_t sent = 0;
    // Its group at the last reading; none before the first and once closed.
    std::optional<PeerGroup> group;
    bool started = false;
    bool closed = false;
    // The connection it reads, as an identity only (app BUG-792 diagnosis).
    const void* connection = nullptr;
};

// What a book saw besides the bytes, to tell its own counting from the
// torrent's (app BUG-792 diagnosis): its readers, a connection with two of
// them, the torrent's pauses and resumes, and libtorrent's own counts since
// the torrent last started, which the engine adds.
struct PeerShareDiag {
    std::int64_t readers_opened = 0;
    std::int64_t readers_closed = 0;
    std::int64_t readers_twice = 0;
    std::int64_t readers_started = 0;
    std::int64_t pauses = 0;
    std::int64_t resumes = 0;
    std::int64_t total_payload_download = 0;
    std::int64_t total_payload_upload = 0;
};

// The bytes of pieces each group gave and got over the connections of one
// torrent since it was added (app DEC-995, DEC-996, BUG-792). Each
// connection reports its own increments, the last ones when it closes, so
// the sum is what libtorrent counts for the torrent (all_time_download and
// all_time_upload since the add). The bytes between two readings go to the
// group of the second. Thread-safe: the connections report from libtorrent's
// network thread, the engine asks from its own. No address is kept.
class PeerShareBook {
public:
    // The pieces the torrent wants, from the engine. Until the first call a
    // peer is whole only when libtorrent knows it for a seed.
    void set_wanted(std::vector<PieceIndex> wanted);

    // `conn` established, as `peer` stood then: libtorrent starts a
    // connection to a peer it knew at the totals of the earlier ones (app
    // BUG-792), counted already. Only what comes after counts; once.
    void start(ConnectionShare& conn, const lt::peer_info& peer);
    // A reader for `conn`, of the connection `connection` (an identity).
    void open(ConnectionShare& conn, const void* connection);
    // The torrent paused or resumed (diagnosis).
    void paused();
    void resumed();

    // A reading of `conn`, connected, from `peer`: what it gave and got since
    // its last reading goes to its group now. Nothing before it started.
    void read(ConnectionShare& conn, const lt::peer_info& peer);
    // `conn` closed: with `last`, its last reading, what it gave and got
    // since the one before still counts, if it ever started. Then it is no
    // longer connected.
    void close(ConnectionShare& conn, const lt::peer_info* last);

    PeerShares shares() const;
    PeerShareDiag diag() const;

private:
    PeerGroup group_of(const lt::peer_info& peer) const;
    void count(ConnectionShare& conn, const lt::peer_info& peer);

    mutable std::mutex mu_;
    std::vector<PieceIndex> wanted_;
    bool wanted_known_ = false;
    PeerShares shares_{};
    PeerShareDiag diag_{};
    std::set<const void*> open_connections_;
};

}  // namespace seekserve
