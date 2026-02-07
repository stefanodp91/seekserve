#include <gtest/gtest.h>

#include "seekserve/range_parser.hpp"

namespace seekserve {
namespace {

static constexpr std::int64_t kFileSize = 10000;

// --- Valid ranges ---

TEST(RangeParser, ClosedRange) {
    auto r = parse_range_header("bytes=0-499", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 0);
    EXPECT_EQ(r->end, 499);
}

TEST(RangeParser, OpenEndedRange) {
    auto r = parse_range_header("bytes=500-", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 500);
    EXPECT_EQ(r->end, kFileSize - 1);
}

TEST(RangeParser, SuffixRange) {
    auto r = parse_range_header("bytes=-500", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, kFileSize - 500);
    EXPECT_EQ(r->end, kFileSize - 1);
}

TEST(RangeParser, SingleByte) {
    auto r = parse_range_header("bytes=0-0", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 0);
    EXPECT_EQ(r->end, 0);
}

TEST(RangeParser, LastByte) {
    auto r = parse_range_header("bytes=9999-9999", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 9999);
    EXPECT_EQ(r->end, 9999);
}

TEST(RangeParser, EntireFile) {
    auto r = parse_range_header("bytes=0-9999", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 0);
    EXPECT_EQ(r->end, 9999);
}

TEST(RangeParser, OpenEndedFromZero) {
    auto r = parse_range_header("bytes=0-", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 0);
    EXPECT_EQ(r->end, kFileSize - 1);
}

TEST(RangeParser, SuffixLargerThanFile) {
    // bytes=-20000 when file is 10000 → clamp to entire file
    auto r = parse_range_header("bytes=-20000", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 0);
    EXPECT_EQ(r->end, kFileSize - 1);
}

TEST(RangeParser, EndBeyondFileClamps) {
    auto r = parse_range_header("bytes=9000-99999", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 9000);
    EXPECT_EQ(r->end, kFileSize - 1);
}

TEST(RangeParser, MiddleRange) {
    auto r = parse_range_header("bytes=1000-1999", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 1000);
    EXPECT_EQ(r->end, 1999);
}

TEST(RangeParser, SuffixOne) {
    auto r = parse_range_header("bytes=-1", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, kFileSize - 1);
    EXPECT_EQ(r->end, kFileSize - 1);
}

// --- Invalid ranges ---

TEST(RangeParser, MalformedNoPrefix) {
    EXPECT_FALSE(parse_range_header("0-499", kFileSize).has_value());
}

TEST(RangeParser, MalformedWrongPrefix) {
    EXPECT_FALSE(parse_range_header("octets=0-499", kFileSize).has_value());
}

TEST(RangeParser, MalformedEmpty) {
    EXPECT_FALSE(parse_range_header("", kFileSize).has_value());
}

TEST(RangeParser, MalformedBytesOnly) {
    EXPECT_FALSE(parse_range_header("bytes=", kFileSize).has_value());
}

TEST(RangeParser, MalformedNoDash) {
    EXPECT_FALSE(parse_range_header("bytes=500", kFileSize).has_value());
}

TEST(RangeParser, InvertedRange) {
    EXPECT_FALSE(parse_range_header("bytes=500-100", kFileSize).has_value());
}

TEST(RangeParser, StartBeyondFile) {
    EXPECT_FALSE(parse_range_header("bytes=10000-10500", kFileSize).has_value());
}

TEST(RangeParser, OpenEndedBeyondFile) {
    EXPECT_FALSE(parse_range_header("bytes=10000-", kFileSize).has_value());
}

TEST(RangeParser, MultiRange) {
    EXPECT_FALSE(parse_range_header("bytes=0-100,200-300", kFileSize).has_value());
}

TEST(RangeParser, NegativeSuffix) {
    EXPECT_FALSE(parse_range_header("bytes=--100", kFileSize).has_value());
}

TEST(RangeParser, SuffixZero) {
    EXPECT_FALSE(parse_range_header("bytes=-0", kFileSize).has_value());
}

TEST(RangeParser, NonNumeric) {
    EXPECT_FALSE(parse_range_header("bytes=abc-def", kFileSize).has_value());
}

TEST(RangeParser, EmptyFileSize) {
    EXPECT_FALSE(parse_range_header("bytes=0-0", 0).has_value());
}

TEST(RangeParser, DashOnly) {
    EXPECT_FALSE(parse_range_header("bytes=-", kFileSize).has_value());
}

// --- Whitespace tolerance ---

TEST(RangeParser, WhitespaceAroundSpec) {
    auto r = parse_range_header("bytes= 100 - 200 ", kFileSize);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 100);
    EXPECT_EQ(r->end, 200);
}

}  // namespace
}  // namespace seekserve
