#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include <libtorrent/address.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "seekserve/wait_piece.hpp"

using namespace seekserve;
using namespace std::chrono_literals;

// The piece a reader waits for, for the app's debug log (app BUG-238,
// BUG-228, DEC-661): who has it, who sends it, where its blocks are.
namespace {

constexpr PieceIndex kPiece = 5;
constexpr int kPieces = 10;

lt::tcp::endpoint address_of(int n) {
    return lt::tcp::endpoint(lt::make_address_v4("10.0.0." + std::to_string(n)), 6881);
}

lt::peer_info peer(int n, bool has_it, lt::peer_flags_t flags = {}, int kbs = 0) {
    lt::peer_info p{};
    p.set_endpoints(lt::tcp::endpoint{}, address_of(n));
    p.flags = flags;
    p.down_speed = kbs * 1024;
    p.pieces.resize(kPieces);
    if (has_it) p.pieces.set_bit(lt::piece_index_t{kPiece});
    return p;
}

lt::block_info block(lt::block_info::block_state_t state, int asker = 0) {
    lt::block_info b{};
    b.state = state;
    if (asker > 0) {
        b.set_peer(address_of(asker));
        b.num_peers = 1;
    }
    return b;
}

}  // namespace

TEST(WaitPieceTest, ThePeersThatHaveThePieceAndThoseThatSendIt) {
    const std::vector<lt::peer_info> peers{
        peer(1, true),                                // has it, sends
        peer(2, true, lt::peer_info::remote_choked),  // has it, chokes us
        peer(3, false, lt::peer_info::seed),          // a seed has every piece
        peer(4, false),                               // has not
        peer(5, true, lt::peer_info::snubbed),        // has it, too slow for libtorrent
    };

    const auto stats = wait_piece_stats(kPiece, 8, peers, {});

    EXPECT_EQ(stats.piece, kPiece);
    EXPECT_EQ(stats.peers, 4);
    EXPECT_EQ(stats.peers_open, 2);
}

TEST(WaitPieceTest, ItsBlocksAndWhoWasAskedForThem) {
    const std::vector<lt::peer_info> peers{peer(1, true, {}, 3), peer(3, true, {}, 35)};
    std::vector<lt::block_info> blocks{
        block(lt::block_info::finished),  block(lt::block_info::finished),
        block(lt::block_info::finished),  block(lt::block_info::writing),
        block(lt::block_info::requested, 1), block(lt::block_info::requested, 3),
        block(lt::block_info::requested, 1), block(lt::block_info::none),
    };
    lt::partial_piece_info other{};
    other.piece_index = lt::piece_index_t{kPiece + 1};
    lt::partial_piece_info mine{};
    mine.piece_index = lt::piece_index_t{kPiece};
    mine.blocks_in_piece = static_cast<int>(blocks.size());
    mine.blocks = blocks.data();

    const auto stats = wait_piece_stats(kPiece, 8, peers, {other, mine});

    EXPECT_EQ(stats.blocks, 8);
    EXPECT_EQ(stats.done, 4);
    EXPECT_EQ(stats.asked, 3);
    EXPECT_EQ(stats.free, 1);
    EXPECT_EQ(stats.askers, 2);
    EXPECT_EQ(stats.slowest_kbs, 3);
    EXPECT_EQ(stats.fastest_kbs, 35);
}

TEST(WaitPieceTest, APieceNobodyAskedForYetHasAllItsBlocksFree) {
    const auto stats = wait_piece_stats(kPiece, 512, {peer(1, true)}, {});

    EXPECT_EQ(stats.blocks, 512);
    EXPECT_EQ(stats.done, 0);
    EXPECT_EQ(stats.asked, 0);
    EXPECT_EQ(stats.free, 512);
    EXPECT_EQ(stats.askers, 0);
    EXPECT_EQ(stats.slowest_kbs, 0);
    EXPECT_EQ(stats.fastest_kbs, 0);
}

TEST(WaitPieceWatchTest, ItSaysSinceWhenThePieceGotNoBlock) {
    WaitPieceWatch watch;
    const auto start = WaitPieceWatch::Clock::time_point{} + 1h;

    EXPECT_EQ(watch.still_ms(kPiece, 10, start), 0);
    EXPECT_EQ(watch.still_ms(kPiece, 10, start + 2500ms), 2500);
    EXPECT_EQ(watch.still_ms(kPiece, 11, start + 3000ms), 0);
    EXPECT_EQ(watch.still_ms(kPiece, 11, start + 4000ms), 1000);
    EXPECT_EQ(watch.still_ms(kPiece + 1, 11, start + 5000ms), 0);
}
