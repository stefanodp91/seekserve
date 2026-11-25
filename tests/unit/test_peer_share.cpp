#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <libtorrent/address.hpp>
#include <libtorrent/peer_info.hpp>

#include "seekserve/peer_share.hpp"
#include "../../seekserve-serve/src/read_numbers.hpp"

using namespace seekserve;

// Who the connected peers are to us and what each group gave and got, for
// what our sharing could buy (app DEC-995, DEC-996).
namespace {

constexpr int kPieces = 10;
const std::vector<PieceIndex> kWanted{2, 3, 4};

lt::tcp::endpoint address_of(int n) {
    return lt::tcp::endpoint(lt::make_address_v4("10.0.0." + std::to_string(n)), 6881);
}

lt::peer_info peer(int n, std::vector<PieceIndex> has, lt::peer_flags_t flags = {},
                   std::int64_t gave = 0, std::int64_t got = 0) {
    lt::peer_info p{};
    p.set_endpoints(lt::tcp::endpoint{}, address_of(n));
    p.flags = flags;
    p.total_download = gave;
    p.total_upload = got;
    p.pieces.resize(kPieces);
    for (auto piece : has) p.pieces.set_bit(lt::piece_index_t{piece});
    return p;
}

}  // namespace

TEST(PeerShareTest, TheGroupOfAPeer) {
    EXPECT_EQ(peer_group(peer(1, {}, lt::peer_info::seed), kWanted), PeerGroup::whole);
    // A peer of a pack with our episode whole, without the seed flag.
    EXPECT_EQ(peer_group(peer(2, {2, 3, 4}), kWanted), PeerGroup::whole);
    EXPECT_EQ(peer_group(peer(3, {2}, lt::peer_info::remote_interested), kWanted),
              PeerGroup::wanting);
    EXPECT_EQ(peer_group(peer(4, {2}), kWanted), PeerGroup::idle);
    // No bitfield yet: not whole.
    lt::peer_info unknown{};
    unknown.set_endpoints(lt::tcp::endpoint{}, address_of(5));
    EXPECT_EQ(peer_group(unknown, kWanted), PeerGroup::idle);
}

TEST(PeerShareTest, TheFirstReadingCountsWhatEachPeerGaveSinceItConnected) {
    PeerShareTally tally;
    tally.read({peer(1, {}, lt::peer_info::seed, 1000, 0),
                peer(2, {2}, lt::peer_info::remote_interested, 300, 700)},
               kWanted);

    EXPECT_EQ(tally.share(PeerGroup::whole).peers, 1);
    EXPECT_EQ(tally.share(PeerGroup::whole).received, 1000);
    EXPECT_EQ(tally.share(PeerGroup::wanting).received, 300);
    EXPECT_EQ(tally.share(PeerGroup::wanting).sent, 700);
    EXPECT_EQ(tally.share(PeerGroup::idle).peers, 0);
}

TEST(PeerShareTest, ALaterReadingCountsOnlyTheIncrements) {
    PeerShareTally tally;
    tally.read({peer(1, {}, lt::peer_info::seed, 1000, 0)}, kWanted);
    tally.read({peer(1, {}, lt::peer_info::seed, 1500, 0)}, kWanted);

    EXPECT_EQ(tally.share(PeerGroup::whole).received, 1500);
}

TEST(PeerShareTest, APeerConnectedAgainCountsItsNewTotals) {
    PeerShareTally tally;
    tally.read({peer(1, {2}, {}, 1000, 500)}, kWanted);
    tally.read({peer(1, {2}, {}, 200, 50)}, kWanted);

    EXPECT_EQ(tally.share(PeerGroup::idle).received, 1200);
    EXPECT_EQ(tally.share(PeerGroup::idle).sent, 550);
}

TEST(PeerShareTest, APeerGoneIsForgottenButWhatItGaveStays) {
    PeerShareTally tally;
    tally.read({peer(1, {2}, {}, 1000, 0), peer(2, {}, lt::peer_info::seed, 400, 0)}, kWanted);
    tally.read({peer(2, {}, lt::peer_info::seed, 600, 0)}, kWanted);

    EXPECT_EQ(tally.remembered(), 1u);
    EXPECT_EQ(tally.share(PeerGroup::idle).peers, 0);
    EXPECT_EQ(tally.share(PeerGroup::idle).received, 1000);
    EXPECT_EQ(tally.share(PeerGroup::whole).peers, 1);
    EXPECT_EQ(tally.share(PeerGroup::whole).received, 600);
}

TEST(PeerShareTest, BytesGoToThePeersGroupAtTheReading) {
    PeerShareTally tally;
    // Interested in pieces of ours at first, then whole: what came later
    // counts as from a peer with everything.
    tally.read({peer(1, {2}, lt::peer_info::remote_interested, 100, 100)}, kWanted);
    tally.read({peer(1, {2, 3, 4}, {}, 400, 100)}, kWanted);

    EXPECT_EQ(tally.share(PeerGroup::wanting).received, 100);
    EXPECT_EQ(tally.share(PeerGroup::wanting).sent, 100);
    EXPECT_EQ(tally.share(PeerGroup::whole).received, 300);
    EXPECT_EQ(tally.share(PeerGroup::wanting).peers, 0);
}

TEST(PeerShareTest, TheStatusSaysEachGroupsCountsAndNoPeer) {
    PeerShareTally tally;
    tally.read({peer(1, {}, lt::peer_info::seed, 1000, 0),
                peer(2, {2}, lt::peer_info::remote_interested, 300, 700)},
               kWanted);
    nlohmann::json status;
    put_peer_share(status, tally);

    ASSERT_TRUE(status.contains("peer_share"));
    const auto& share = status["peer_share"];
    EXPECT_EQ(share["whole"]["peers"], 1);
    EXPECT_EQ(share["whole"]["received"], 1000);
    EXPECT_EQ(share["wanting"]["sent"], 700);
    EXPECT_EQ(share["idle"]["peers"], 0);
    EXPECT_EQ(status.dump().find("10.0.0."), std::string::npos);
}
