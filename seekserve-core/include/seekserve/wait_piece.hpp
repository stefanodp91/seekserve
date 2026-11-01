#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
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

// Whether a connected peer that has the piece sends us blocks: libtorrent asks
// nothing of one that chokes us or is snubbed (app BUG-247).
bool someone_sends_it(const WaitPieceStats& stats);

// How many connected peers have every piece of `missing`, the pieces a file
// still misses: who could send the whole of it (app BUG-784, DEC-809). A
// peer that chokes us counts, as it may send later.
int file_holders(const std::vector<PieceIndex>& missing, const std::vector<lt::peer_info>& peers);

// The bytes of each piece on its way in `queue`: its blocks finished or
// being written, and the first bytes of those arriving. libtorrent's queue points into storage of the whole
// session that its next queue, of any torrent, writes again: copy it with
// this at once, under the one lock every caller takes.
std::map<PieceIndex, std::int64_t> blocks_there(const std::vector<lt::partial_piece_info>& queue,
                                                const std::function<int(PieceIndex)>& piece_size);

// The bytes of pieces `first` to `last`, those there (`have`) whole and
// those on their way as `on_its_way` says (`blocks_there`): how far the
// pieces a small file is in are (app BUG-784, DEC-811).
struct SpanBytes {
    std::int64_t downloaded = 0;
    std::int64_t size = 0;
};
SpanBytes span_bytes(PieceIndex first, PieceIndex last, const std::function<bool(PieceIndex)>& have,
                     const std::function<int(PieceIndex)>& piece_size,
                     const std::map<PieceIndex, std::int64_t>& on_its_way);

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
