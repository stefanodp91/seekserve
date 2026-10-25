#include <gtest/gtest.h>

#include "seekserve/piece_cache.hpp"

using namespace seekserve;

// The pieces read through libtorrent, kept up to a number of bytes (app
// BUG-236, DEC-657).
namespace {

PieceBuffer piece_of(int size, char fill) {
    PieceBuffer b;
    b.data.reset(new char[static_cast<std::size_t>(size)]);
    std::fill(b.data.get(), b.data.get() + size, fill);
    b.size = size;
    return b;
}

}  // namespace

TEST(PieceCacheTest, APieceKeptComesBackShared) {
    PieceCache cache(1000);
    auto piece = piece_of(100, 'a');
    cache.put(3, piece);

    auto got = cache.get(3);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->data.get(), piece.data.get());
    EXPECT_EQ(got->size, 100);
    EXPECT_FALSE(cache.get(4).has_value());
}

TEST(PieceCacheTest, OverTheLimitTheLeastRecentlyUsedGoes) {
    PieceCache cache(250);
    cache.put(1, piece_of(100, 'a'));
    cache.put(2, piece_of(100, 'b'));
    ASSERT_TRUE(cache.get(1).has_value());  // 1 used after 2
    cache.put(3, piece_of(100, 'c'));

    EXPECT_TRUE(cache.get(1).has_value());
    EXPECT_FALSE(cache.get(2).has_value());
    EXPECT_TRUE(cache.get(3).has_value());
    EXPECT_EQ(cache.bytes(), 200);
}

TEST(PieceCacheTest, APieceBiggerThanTheLimitStillFits) {
    PieceCache cache(100);
    cache.put(1, piece_of(50, 'a'));
    cache.put(2, piece_of(400, 'b'));

    EXPECT_FALSE(cache.get(1).has_value());
    EXPECT_TRUE(cache.get(2).has_value());
    EXPECT_EQ(cache.bytes(), 400);
}

TEST(PieceCacheTest, ClearedItHoldsNothing) {
    PieceCache cache(1000);
    cache.put(1, piece_of(100, 'a'));
    cache.clear();

    EXPECT_FALSE(cache.get(1).has_value());
    EXPECT_EQ(cache.bytes(), 0);
}
