#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "seekserve/piece_reader.hpp"

using namespace seekserve;
using namespace std::chrono_literals;

// Whole pieces read through libtorrent's read_piece (app BUG-236, DEC-657).
namespace {

PieceBuffer piece_of(int size, char fill) {
    PieceBuffer b;
    b.data.reset(new char[static_cast<std::size_t>(size)]);
    std::fill(b.data.get(), b.data.get() + size, fill);
    b.size = size;
    return b;
}

}  // namespace

TEST(TorrentPieceReaderTest, ItAsksLibtorrentAndReturnsItsAnswer) {
    std::vector<PieceIndex> asked;
    TorrentPieceReader* self = nullptr;
    TorrentPieceReader reader([&](PieceIndex p) {
        asked.push_back(p);
        self->on_read(p, piece_of(10, 'x'));
    });
    self = &reader;

    auto got = reader.read(7, 1s);

    ASSERT_TRUE(got.ok()) << got.error().message();
    EXPECT_EQ(got.value().size, 10);
    EXPECT_EQ(got.value().data[0], 'x');
    EXPECT_EQ(asked, std::vector<PieceIndex>{7});
}

TEST(TorrentPieceReaderTest, TwoReadersOfOnePieceAskOnce) {
    std::atomic<int> asks{0};
    TorrentPieceReader reader([&](PieceIndex) { ++asks; });

    std::atomic<int> ok{0};
    auto wait_for_it = [&] {
        if (reader.read(2, 2s).ok()) ++ok;
    };
    std::thread a(wait_for_it);
    std::thread b(wait_for_it);
    std::this_thread::sleep_for(100ms);
    reader.on_read(2, piece_of(5, 'y'));
    a.join();
    b.join();

    EXPECT_EQ(asks.load(), 1);
    EXPECT_EQ(ok.load(), 2);
}

TEST(TorrentPieceReaderTest, APieceLibtorrentDoesNotHaveIsNotAvailable) {
    TorrentPieceReader* self = nullptr;
    TorrentPieceReader reader([&](PieceIndex p) {
        self->on_read(p, make_error_code(errc::piece_not_available));
    });
    self = &reader;

    auto got = reader.read(1, 1s);

    ASSERT_FALSE(got.ok());
    EXPECT_EQ(got.error(), make_error_code(errc::piece_not_available));
}

TEST(TorrentPieceReaderTest, NoAnswerInTimeIsATimeout) {
    TorrentPieceReader reader([](PieceIndex) {});

    auto got = reader.read(1, 50ms);

    ASSERT_FALSE(got.ok());
    EXPECT_EQ(got.error(), make_error_code(errc::timeout_waiting_for_piece));
}

TEST(TorrentPieceReaderTest, CancelWakesTheReader) {
    TorrentPieceReader reader([](PieceIndex) {});
    std::thread canceller([&] {
        std::this_thread::sleep_for(50ms);
        reader.cancel();
    });

    auto got = reader.read(1, 5s);
    canceller.join();

    ASSERT_FALSE(got.ok());
    EXPECT_EQ(got.error(), make_error_code(errc::cancelled));
}

// The server waits for a piece in slices of half a second; reading a big
// piece can take longer: the answer that comes after a slice is over is the
// next read's, with no second request when it came before that read (app
// BUG-236, DEC-657).
TEST(TorrentPieceReaderTest, ALateAnswerIsTheNextReadsWithOneRequest) {
    std::atomic<int> asks{0};
    TorrentPieceReader reader([&](PieceIndex) { ++asks; });

    ASSERT_FALSE(reader.read(5, 30ms).ok());
    reader.on_read(5, piece_of(8, 'z'));
    auto got = reader.read(5, 30ms);

    ASSERT_TRUE(got.ok()) << got.error().message();
    EXPECT_EQ(got.value().data[0], 'z');
    EXPECT_EQ(asks.load(), 1);
}

// An answer that never comes must not stop the stream for good (app
// BUG-238, DEC-659): a read that finds the piece still unanswered, with
// nobody else waiting for it, asks libtorrent again.
TEST(TorrentPieceReaderTest, AReadOfAPieceWithNoAnswerAsksAgain) {
    std::atomic<int> asks{0};
    TorrentPieceReader reader([&](PieceIndex) { ++asks; });

    ASSERT_FALSE(reader.read(5, 30ms).ok());
    ASSERT_FALSE(reader.read(5, 30ms).ok());
    EXPECT_EQ(asks.load(), 2);

    reader.on_read(5, piece_of(8, 'q'));
    auto got = reader.read(5, 30ms);
    ASSERT_TRUE(got.ok()) << got.error().message();
    EXPECT_EQ(got.value().data[0], 'q');
    EXPECT_EQ(asks.load(), 2);
}

// While one reader waits for a piece, another that comes asks no more.
TEST(TorrentPieceReaderTest, AReadWhileAnotherWaitsAsksNoMore) {
    std::atomic<int> asks{0};
    TorrentPieceReader reader([&](PieceIndex) { ++asks; });
    std::thread first([&] { reader.read(5, 1s); });
    while (asks.load() == 0) std::this_thread::sleep_for(1ms);

    ASSERT_FALSE(reader.read(5, 30ms).ok());
    reader.on_read(5, piece_of(4, 'w'));
    first.join();

    EXPECT_EQ(asks.load(), 1);
}

// A piece asked twice has two answers: the one that comes after the read
// took the first is nobody's, and the next read asks again.
TEST(TorrentPieceReaderTest, TheSecondAnswerOfAPieceAskedTwiceIsHarmless) {
    std::atomic<int> asks{0};
    TorrentPieceReader reader([&](PieceIndex) { ++asks; });
    ASSERT_FALSE(reader.read(5, 1ms).ok());
    ASSERT_FALSE(reader.read(5, 1ms).ok());

    reader.on_read(5, piece_of(4, 'a'));
    auto got = reader.read(5, 30ms);
    ASSERT_TRUE(got.ok()) << got.error().message();
    EXPECT_EQ(got.value().data[0], 'a');
    reader.on_read(5, piece_of(4, 'a'));

    EXPECT_FALSE(reader.read(5, 1ms).ok());
    EXPECT_EQ(asks.load(), 3);
}

TEST(TorrentPieceReaderTest, AnErrorNobodyWaitsForIsNotKept) {
    std::atomic<int> asks{0};
    TorrentPieceReader reader([&](PieceIndex) { ++asks; });

    ASSERT_FALSE(reader.read(5, 30ms).ok());
    reader.on_read(5, make_error_code(errc::piece_not_available));
    auto got = reader.read(5, 30ms);

    EXPECT_EQ(got.error(), make_error_code(errc::timeout_waiting_for_piece));
    EXPECT_EQ(asks.load(), 2);
}

TEST(TorrentPieceReaderTest, OnlyAFewAnswersNobodyTookAreKept) {
    std::atomic<int> asks{0};
    TorrentPieceReader reader([&](PieceIndex) { ++asks; });
    const int count = TorrentPieceReader::kMaxIdlePieces + 1;
    for (PieceIndex p = 0; p < count; ++p) {
        ASSERT_FALSE(reader.read(p, 1ms).ok());
        reader.on_read(p, piece_of(4, 'k'));
    }

    // The oldest went: it is asked again; the newest is still there.
    EXPECT_FALSE(reader.read(0, 1ms).ok());
    EXPECT_TRUE(reader.read(count - 1, 1ms).ok());
    EXPECT_EQ(asks.load(), count + 1);
}

// What the reader did, for the app's debug log (app BUG-238, DEC-660).
TEST(TorrentPieceReaderTest, ItCountsTheRequestsTheAnswersAndTheErrors) {
    TorrentPieceReader* self = nullptr;
    bool fail = false;
    TorrentPieceReader reader([&](PieceIndex p) {
        if (fail) self->on_read(p, make_error_code(errc::piece_not_available), "invalid piece index");
    });
    self = &reader;

    ASSERT_FALSE(reader.read(1, 1ms).ok());   // asked
    ASSERT_FALSE(reader.read(1, 1ms).ok());   // asked again
    reader.on_read(1, piece_of(4, 'a'));      // late: nobody waits
    ASSERT_TRUE(reader.read(1, 1ms).ok());    // taken from memory
    fail = true;
    ASSERT_FALSE(reader.read(2, 1ms).ok());   // an error at once

    const auto stats = reader.stats();
    EXPECT_EQ(stats.asks, 3);
    EXPECT_EQ(stats.repeats, 1);
    EXPECT_EQ(stats.answers, 1);
    EXPECT_EQ(stats.errors, 1);
    EXPECT_EQ(stats.late, 1);
    EXPECT_EQ(stats.last_error, "invalid piece index");
}

TEST(TorrentPieceReaderTest, ItSaysWhatWaitsForAnAnswerAndSinceWhen) {
    auto now = TorrentPieceReader::Clock::time_point{} + 1h;
    TorrentPieceReader reader([](PieceIndex) {}, [&] { return now; });

    ASSERT_FALSE(reader.read(3, 1ms).ok());
    now += 2500ms;
    ASSERT_FALSE(reader.read(4, 1ms).ok());
    auto stats = reader.stats();
    EXPECT_EQ(stats.waiting, 2);
    EXPECT_EQ(stats.oldest_wait_ms, 2500);

    reader.on_read(3, piece_of(4, 'b'));
    reader.on_read(4, piece_of(4, 'c'));
    stats = reader.stats();
    EXPECT_EQ(stats.waiting, 0);
    EXPECT_EQ(stats.oldest_wait_ms, 0);
}

