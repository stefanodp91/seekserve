#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "seekserve/types.hpp"

namespace seekserve {

// The piece a reader waits for, for the app's debug log (app BUG-238,
// BUG-228, DEC-661): who has it, who sends it, where its blocks are. The
// peers' addresses only match blocks to peers here; none goes out.
struct WaitPieceStats {
    PieceIndex piece = -1;
    int peers = 0;        // connected peers that have it
    int peers_open = 0;   // of them, those that do not choke us and are not snubbed
    int blocks = 0;       // its blocks
    int done = 0;         // blocks there or being written
    int asked = 0;        // blocks asked and not there yet
    int free = 0;         // blocks nobody was asked for
    int askers = 0;       // the peers its asked blocks wait on
    int slowest_kbs = 0;  // how fast the slowest and the fastest of them send
    int fastest_kbs = 0;
    std::int64_t still_ms = 0;  // since when `done` has not grown (WaitPieceWatch)
};

// `piece` as libtorrent's peers and download queue tell it; `blocks_in_piece`
// for a piece no block of which was asked yet.
WaitPieceStats wait_piece_stats(PieceIndex piece, int blocks_in_piece,
                                const std::vector<lt::peer_info>& peers,
                                const std::vector<lt::partial_piece_info>& queue);

// Since when the awaited piece got no block: a measure from one status to
// the next, no time limit.
class WaitPieceWatch {
public:
    using Clock = std::chrono::steady_clock;

    std::int64_t still_ms(PieceIndex piece, int done, Clock::time_point now);

private:
    PieceIndex piece_ = -1;
    int done_ = -1;
    Clock::time_point since_{};
};

}  // namespace seekserve
