#include <gtest/gtest.h>

#include <chrono>

#include "seekserve/wait_focus.hpp"

using namespace seekserve;
using namespace std::chrono_literals;

// A reader that waits for the same piece and gets no block of it for a while
// gets that piece asked first (app BUG-246, DEC-670), again after each such
// while, as long as a connected peer has it.
namespace {

const WaitFocusWatch::Clock::time_point t0{};

}  // namespace

TEST(WaitFocusWatchTest, WithoutATimeItIsOff) {
    WaitFocusWatch watch(0ms);

    EXPECT_FALSE(watch.due(5, 0, true, t0));
    EXPECT_FALSE(watch.due(5, 0, true, t0 + 1h));
}

TEST(WaitFocusWatchTest, APieceWithNoBlockForTheTimeIsDueAndAgainAfterEachTime) {
    WaitFocusWatch watch(2s);

    EXPECT_FALSE(watch.due(5, 0, true, t0));
    EXPECT_FALSE(watch.due(5, 0, true, t0 + 1999ms));
    EXPECT_TRUE(watch.due(5, 0, true, t0 + 2s));
    EXPECT_FALSE(watch.due(5, 0, true, t0 + 3s));
    EXPECT_FALSE(watch.due(5, 0, true, t0 + 3999ms));
    EXPECT_TRUE(watch.due(5, 0, true, t0 + 4s));
}

TEST(WaitFocusWatchTest, ABlockThatArrivesCountsAgain) {
    WaitFocusWatch watch(2s);

    EXPECT_FALSE(watch.due(5, 0, true, t0));
    EXPECT_FALSE(watch.due(5, 1, true, t0 + 1500ms));
    EXPECT_FALSE(watch.due(5, 1, true, t0 + 3499ms));
    EXPECT_TRUE(watch.due(5, 1, true, t0 + 3500ms));
}

TEST(WaitFocusWatchTest, AnotherPieceCountsAgain) {
    WaitFocusWatch watch(2s);

    EXPECT_FALSE(watch.due(5, 0, true, t0));
    EXPECT_FALSE(watch.due(6, 0, true, t0 + 1500ms));
    EXPECT_FALSE(watch.due(6, 0, true, t0 + 3499ms));
    EXPECT_TRUE(watch.due(6, 0, true, t0 + 3500ms));
}

TEST(WaitFocusWatchTest, AReaderThatStopsWaitingCountsAgain) {
    WaitFocusWatch watch(2s);

    EXPECT_FALSE(watch.due(5, 0, true, t0));
    EXPECT_FALSE(watch.due(kNoPiece, 0, false, t0 + 1s));
    EXPECT_FALSE(watch.due(5, 0, true, t0 + 1500ms));
    EXPECT_FALSE(watch.due(5, 0, true, t0 + 3499ms));
    EXPECT_TRUE(watch.due(5, 0, true, t0 + 3500ms));
}

// Asking first a piece nobody has would only cancel the other requests.
TEST(WaitFocusWatchTest, WithNobodyWhoHasThePieceItWaitsForSomeone) {
    WaitFocusWatch watch(2s);

    EXPECT_FALSE(watch.due(5, 0, false, t0));
    EXPECT_FALSE(watch.due(5, 0, false, t0 + 5s));
    EXPECT_TRUE(watch.due(5, 0, true, t0 + 6s));
    EXPECT_FALSE(watch.due(5, 0, true, t0 + 7s));
    EXPECT_TRUE(watch.due(5, 0, true, t0 + 8s));
}
