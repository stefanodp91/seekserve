#include <gtest/gtest.h>

#include <chrono>

#include "seekserve/wait_focus.hpp"

using namespace seekserve;
using namespace std::chrono_literals;

// A reader that waits for the same piece too long gets the start with
// priority again from that piece (app BUG-246, DEC-669).
namespace {

const WaitFocusWatch::Clock::time_point t0{};

}  // namespace

TEST(WaitFocusWatchTest, WithoutATimeItIsOff) {
    WaitFocusWatch watch(0ms);

    EXPECT_FALSE(watch.due(5, t0));
    EXPECT_FALSE(watch.due(5, t0 + 1h));
}

TEST(WaitFocusWatchTest, AWaitForTheSamePieceIsDueOnceAtItsTime) {
    WaitFocusWatch watch(2s);

    EXPECT_FALSE(watch.due(5, t0));
    EXPECT_FALSE(watch.due(5, t0 + 1999ms));
    EXPECT_TRUE(watch.due(5, t0 + 2s));
    EXPECT_FALSE(watch.due(5, t0 + 3s));
    EXPECT_FALSE(watch.due(5, t0 + 1min));
}

TEST(WaitFocusWatchTest, AnotherPieceCountsAgain) {
    WaitFocusWatch watch(2s);

    EXPECT_FALSE(watch.due(5, t0));
    EXPECT_FALSE(watch.due(6, t0 + 1500ms));
    EXPECT_FALSE(watch.due(6, t0 + 3499ms));
    EXPECT_TRUE(watch.due(6, t0 + 3500ms));
}

TEST(WaitFocusWatchTest, AReaderThatStopsWaitingCountsAgain) {
    WaitFocusWatch watch(2s);

    EXPECT_FALSE(watch.due(5, t0));
    EXPECT_FALSE(watch.due(kNoPiece, t0 + 1s));
    EXPECT_FALSE(watch.due(5, t0 + 1500ms));
    EXPECT_FALSE(watch.due(5, t0 + 3499ms));
    EXPECT_TRUE(watch.due(5, t0 + 3500ms));
}

TEST(WaitFocusWatchTest, ANewWaitForThePieceOfTheLastOneIsDueAgain) {
    WaitFocusWatch watch(2s);

    EXPECT_FALSE(watch.due(5, t0));
    EXPECT_TRUE(watch.due(5, t0 + 2s));
    EXPECT_FALSE(watch.due(kNoPiece, t0 + 3s));
    EXPECT_FALSE(watch.due(5, t0 + 4s));
    EXPECT_TRUE(watch.due(5, t0 + 6s));
}
