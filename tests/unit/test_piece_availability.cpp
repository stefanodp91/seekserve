#include <gtest/gtest.h>

#include "seekserve/piece_availability.hpp"
#include "seekserve/types.hpp"

namespace seekserve {
namespace {

// Test layout: 10 pieces, 1000 bytes each, last piece 500 bytes
// Total: 9*1000 + 500 = 9500 bytes

TEST(PieceAvailability, MarkCompleteAndQuery) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    EXPECT_FALSE(idx.is_complete(0));
    idx.mark_complete(0);
    EXPECT_TRUE(idx.is_complete(0));
    EXPECT_FALSE(idx.is_complete(1));
}

TEST(PieceAvailability, MarkCompleteIdempotent) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(5);
    idx.mark_complete(5);  // second call should be no-op
    EXPECT_TRUE(idx.is_complete(5));
    // Progress should count only once
    EXPECT_FLOAT_EQ(idx.progress(), 0.1f);
}

TEST(PieceAvailability, IsSpanCompleteAllAvailable) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(2);
    idx.mark_complete(3);
    idx.mark_complete(4);
    PieceSpan span{2, 4, 0, 1000};
    EXPECT_TRUE(idx.is_span_complete(span));
}

TEST(PieceAvailability, IsSpanCompleteMissingMiddle) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(2);
    // piece 3 NOT complete
    idx.mark_complete(4);
    PieceSpan span{2, 4, 0, 1000};
    EXPECT_FALSE(idx.is_span_complete(span));
}

TEST(PieceAvailability, IsSpanCompleteSinglePiece) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(7);
    PieceSpan span{7, 7, 100, 200};
    EXPECT_TRUE(idx.is_span_complete(span));
}

TEST(PieceAvailability, ContiguousFromSeries) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(3);
    idx.mark_complete(4);
    idx.mark_complete(5);
    EXPECT_EQ(idx.contiguous_from(3), 3);
}

TEST(PieceAvailability, ContiguousFromGap) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(3);
    idx.mark_complete(4);
    // piece 5 missing
    idx.mark_complete(6);
    EXPECT_EQ(idx.contiguous_from(3), 2);
}

TEST(PieceAvailability, ContiguousFromStart) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    EXPECT_EQ(idx.contiguous_from(0), 0);

    idx.mark_complete(0);
    idx.mark_complete(1);
    idx.mark_complete(2);
    EXPECT_EQ(idx.contiguous_from(0), 3);
}

TEST(PieceAvailability, ContiguousFromEnd) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(9);  // last piece
    EXPECT_EQ(idx.contiguous_from(9), 1);
}

TEST(PieceAvailability, ContiguousBytesFromIntraPiece) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(3);
    idx.mark_complete(4);
    // offset 200 in piece 3: remaining = 1000 - 200 = 800, plus piece 4 = 1000
    EXPECT_EQ(idx.contiguous_bytes_from(3, 200), 1800);
}

TEST(PieceAvailability, ContiguousBytesFromPieceStart) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(3);
    idx.mark_complete(4);
    idx.mark_complete(5);
    // offset 0 in piece 3: 1000 + 1000 + 1000 = 3000
    EXPECT_EQ(idx.contiguous_bytes_from(3, 0), 3000);
}

TEST(PieceAvailability, ContiguousBytesFromLastPiece) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(9);  // last piece, 500 bytes
    EXPECT_EQ(idx.contiguous_bytes_from(9, 0), 500);
    EXPECT_EQ(idx.contiguous_bytes_from(9, 100), 400);
}

TEST(PieceAvailability, ContiguousBytesNotAvailable) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    EXPECT_EQ(idx.contiguous_bytes_from(5, 0), 0);
}

TEST(PieceAvailability, ProgressZero) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    EXPECT_FLOAT_EQ(idx.progress(), 0.0f);
}

TEST(PieceAvailability, ProgressHalf) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    for (int i = 0; i < 5; ++i) idx.mark_complete(i);
    EXPECT_FLOAT_EQ(idx.progress(), 0.5f);
}

TEST(PieceAvailability, ProgressFull) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    for (int i = 0; i < 10; ++i) idx.mark_complete(i);
    EXPECT_FLOAT_EQ(idx.progress(), 1.0f);
}

TEST(PieceAvailability, ResetAndReinit) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    for (int i = 0; i < 10; ++i) idx.mark_complete(i);
    EXPECT_FLOAT_EQ(idx.progress(), 1.0f);

    idx.reset(5, 2000, 1000);
    EXPECT_FLOAT_EQ(idx.progress(), 0.0f);
    EXPECT_EQ(idx.num_pieces(), 5);
    EXPECT_FALSE(idx.is_complete(0));

    // Verify new geometry works
    idx.mark_complete(4);  // last piece, 1000 bytes
    EXPECT_EQ(idx.contiguous_bytes_from(4, 0), 1000);
}

TEST(PieceAvailability, OutOfBoundsIgnored) {
    PieceAvailabilityIndex idx(10, 1000, 500);
    idx.mark_complete(-1);   // should be no-op
    idx.mark_complete(10);   // should be no-op
    idx.mark_complete(100);  // should be no-op
    EXPECT_FALSE(idx.is_complete(-1));
    EXPECT_FALSE(idx.is_complete(10));
    EXPECT_FLOAT_EQ(idx.progress(), 0.0f);
}

TEST(PieceAvailability, DefaultConstructed) {
    PieceAvailabilityIndex idx;
    EXPECT_EQ(idx.num_pieces(), 0);
    EXPECT_FLOAT_EQ(idx.progress(), 0.0f);
    EXPECT_EQ(idx.contiguous_from(0), 0);
}

TEST(PieceAvailability, ProgressEmptyTorrent) {
    PieceAvailabilityIndex idx(0, 1000, 0);
    EXPECT_FLOAT_EQ(idx.progress(), 0.0f);
}

}  // namespace
}  // namespace seekserve
