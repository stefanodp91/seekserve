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

// A connection just established, with nothing carried from earlier ones.
ConnectionShare started(PeerShareBook& book) {
    ConnectionShare conn;
    book.start(conn, peer(0, {}, {}, 0, 0));
    return conn;
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

TEST(PeerShareTest, TheFirstReadingCountsWhatAConnectionGaveSinceItOpened) {
    PeerShareBook book;
    book.set_wanted(kWanted);
    ConnectionShare seed = started(book);
    ConnectionShare wanting = started(book);
    book.read(seed, peer(1, {}, lt::peer_info::seed, 1000, 0));
    book.read(wanting, peer(2, {2}, lt::peer_info::remote_interested, 300, 700));

    const auto shares = book.shares();
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::whole)].peers, 1);
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::whole)].received, 1000);
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::wanting)].received, 300);
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::wanting)].sent, 700);
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::idle)].peers, 0);
}

TEST(PeerShareTest, ALaterReadingCountsOnlyTheIncrements) {
    PeerShareBook book;
    ConnectionShare conn = started(book);
    book.read(conn, peer(1, {}, lt::peer_info::seed, 1000, 0));
    book.read(conn, peer(1, {}, lt::peer_info::seed, 1500, 0));

    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::whole)].received, 1500);
    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::whole)].peers, 1);
}

// App BUG-792: through the SOCKS5 proxy with proxy_hostnames, libtorrent gives
// every web seed the address 0.0.0.0 and the port of its URL, so two web seeds
// on the same port share one remote address. Each connection counts once: the
// one that grows is never taken for the other at a reading.
TEST(PeerShareTest, TwoConnectionsWithTheSameRemoteAddressCountOnce) {
    const lt::tcp::endpoint seeds(lt::address_v4{}, 443);
    auto web_seed = [&](std::int64_t gave) {
        lt::peer_info p = peer(0, {}, lt::peer_info::seed, gave, 0);
        p.set_endpoints(lt::tcp::endpoint{}, seeds);
        return p;
    };
    PeerShareBook book;
    ConnectionShare giving = started(book);
    ConnectionShare connecting = started(book);
    // One web seed gives, the other is still connecting through the proxy.
    for (std::int64_t gave : {1000, 1500, 2000}) {
        book.read(giving, web_seed(gave));
        book.read(connecting, web_seed(0));
    }

    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::whole)].received, 2000);
    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::whole)].peers, 2);
}

// App BUG-792: what a connection gave after its last reading counts when it
// closes; libtorrent closes the seeds when the torrent has what it wants.
TEST(PeerShareTest, WhatAConnectionGaveBeforeClosingCounts) {
    PeerShareBook book;
    ConnectionShare conn = started(book);
    book.read(conn, peer(1, {}, lt::peer_info::seed, 1000, 10));
    const auto last = peer(1, {}, lt::peer_info::seed, 1600, 30);
    book.close(conn, &last);

    const auto whole = book.shares()[static_cast<int>(PeerGroup::whole)];
    EXPECT_EQ(whole.received, 1600);
    EXPECT_EQ(whole.sent, 30);
    EXPECT_EQ(whole.peers, 0);
}

TEST(PeerShareTest, AConnectionClosedWithoutALastReadingKeepsWhatItGave) {
    PeerShareBook book;
    ConnectionShare conn = started(book);
    book.read(conn, peer(1, {2}, {}, 1000, 0));
    book.close(conn, nullptr);
    // A reading after it closed adds nothing.
    book.read(conn, peer(1, {2}, {}, 5000, 0));

    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::idle)].received, 1000);
    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::idle)].peers, 0);
}

TEST(PeerShareTest, BytesGoToThePeersGroupAtTheReading) {
    PeerShareBook book;
    book.set_wanted(kWanted);
    ConnectionShare conn = started(book);
    // Interested in pieces of ours at first, then whole: what came later
    // counts as from a peer with everything.
    book.read(conn, peer(1, {2}, lt::peer_info::remote_interested, 100, 100));
    book.read(conn, peer(1, {2, 3, 4}, {}, 400, 100));

    const auto shares = book.shares();
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::wanting)].received, 100);
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::wanting)].sent, 100);
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::whole)].received, 300);
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::wanting)].peers, 0);
    EXPECT_EQ(shares[static_cast<int>(PeerGroup::whole)].peers, 1);
}

// Before the engine says which pieces the torrent wants, only a seed is
// whole: with no wanted piece every peer would have all of them.
TEST(PeerShareTest, BeforeTheWantedPiecesOnlyASeedIsWhole) {
    PeerShareBook book;
    ConnectionShare seed = started(book);
    ConnectionShare partial = started(book);
    book.read(seed, peer(1, {}, lt::peer_info::seed, 100, 0));
    book.read(partial, peer(2, {2, 3, 4}, {}, 50, 0));

    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::whole)].received, 100);
    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::idle)].received, 50);
}

TEST(PeerShareTest, TheStatusSaysEachGroupsCountsAndNoPeer) {
    PeerShareBook book;
    book.set_wanted(kWanted);
    ConnectionShare seed = started(book);
    ConnectionShare wanting = started(book);
    book.read(seed, peer(1, {}, lt::peer_info::seed, 1000, 0));
    book.read(wanting, peer(2, {2}, lt::peer_info::remote_interested, 300, 700));
    nlohmann::json status;
    put_peer_share(status, book.shares());

    ASSERT_TRUE(status.contains("peer_share"));
    const auto& share = status["peer_share"];
    EXPECT_EQ(share["whole"]["peers"], 1);
    EXPECT_EQ(share["whole"]["received"], 1000);
    EXPECT_EQ(share["wanting"]["sent"], 700);
    EXPECT_EQ(share["idle"]["peers"], 0);
    EXPECT_EQ(status.dump().find("10.0.0."), std::string::npos);
}

// App BUG-792, diagnosis: what the book saw of the connections and of the
// torrent's pauses, to tell a connection read by two readers from a torrent
// whose own count lost bytes. Identities only, never an address.
TEST(PeerShareTest, TheDiagnosisCountsReadersAndASecondReaderOfOneConnection) {
    PeerShareBook book;
    int first = 0;
    int second = 0;
    ConnectionShare a = started(book);
    ConnectionShare b = started(book);
    ConnectionShare again = started(book);
    book.open(a, &first);
    book.open(b, &second);
    book.open(again, &first);
    book.close(a, nullptr);
    book.paused();
    book.resumed();

    const auto diag = book.diag();
    EXPECT_EQ(diag.readers_opened, 3);
    EXPECT_EQ(diag.readers_closed, 1);
    EXPECT_EQ(diag.readers_twice, 1);
    EXPECT_EQ(diag.pauses, 1);
    EXPECT_EQ(diag.resumes, 1);
}

TEST(PeerShareTest, TheStatusCarriesTheDiagnosisNextToTheGroups) {
    PeerShareBook book;
    int connection = 0;
    ConnectionShare conn = started(book);
    book.open(conn, &connection);
    PeerShareDiag diag = book.diag();
    diag.total_payload_download = 1234;
    nlohmann::json status;
    put_peer_share(status, book.shares(), &diag);

    EXPECT_EQ(status["peer_share"]["diag"]["readers_opened"], 1);
    EXPECT_EQ(status["peer_share"]["diag"]["total_payload_download"], 1234);
    EXPECT_TRUE(status["peer_share"].contains("whole"));
}

// App BUG-792: libtorrent starts a connection to a peer it was connected to
// before at that peer's earlier totals (torrent_peer::prev_amount_download).
// What it carries was counted with the earlier connections: a connection
// counts from where it stood when it was established.
TEST(PeerShareTest, AConnectionCountsFromWhereItStarted) {
    PeerShareBook book;
    ConnectionShare conn;
    book.start(conn, peer(1, {}, lt::peer_info::seed, 5000, 300));
    book.read(conn, peer(1, {}, lt::peer_info::seed, 5600, 300));
    const auto last = peer(1, {}, lt::peer_info::seed, 6000, 350);
    book.close(conn, &last);

    const auto whole = book.shares()[static_cast<int>(PeerGroup::whole)];
    EXPECT_EQ(whole.received, 1000);
    EXPECT_EQ(whole.sent, 50);
}

// A connection never established (a failed attempt) got nothing: what its
// totals carry from earlier connections does not count.
TEST(PeerShareTest, AConnectionNeverEstablishedCountsNothing) {
    PeerShareBook book;
    ConnectionShare conn;
    book.read(conn, peer(1, {}, lt::peer_info::seed, 5000, 0));
    const auto last = peer(1, {}, lt::peer_info::seed, 5000, 0);
    book.close(conn, &last);

    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::whole)].received, 0);
    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::whole)].peers, 0);
}

TEST(PeerShareTest, AConnectionStartsOnce) {
    PeerShareBook book;
    ConnectionShare conn;
    book.start(conn, peer(1, {}, lt::peer_info::seed, 1000, 0));
    book.start(conn, peer(1, {}, lt::peer_info::seed, 1800, 0));
    book.read(conn, peer(1, {}, lt::peer_info::seed, 2000, 0));

    EXPECT_EQ(book.shares()[static_cast<int>(PeerGroup::whole)].received, 1000);
}
