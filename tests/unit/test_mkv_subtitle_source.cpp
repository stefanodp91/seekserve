// MkvSubtitleSource: the stream a TV share serves with subtitles inside (app
// WORK-37 step 7): prepared from the head and the Cues of a file whose
// pieces may still be missing, then read from the original and the new bytes.

#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <string>

#include "mkv_test_support.hpp"
#include "seekserve/mkv_subtitle_layout.hpp"
#include "seekserve/mkv_subtitle_source.hpp"
#include "seekserve/mp4_mkv_file.hpp"
#include "seekserve/mp4_structure.hpp"

namespace seekserve {
namespace {

using namespace mkvtest;
using namespace std::chrono_literals;

constexpr std::int64_t kPiece = 16384;

// A file in memory whose pieces arrive when the test says so.
class PieceSource final : public StreamSource {
public:
    explicit PieceSource(Bytes data) : data_(std::move(data)) {}

    void lose(std::int64_t from, std::int64_t to) {
        for (auto p = from / kPiece; p <= to / kPiece; ++p) missing_.insert(p);
    }
    void arrive() { missing_.clear(); }

    Result<std::vector<std::uint8_t>> read(std::int64_t offset, std::int64_t len,
                                           std::chrono::milliseconds) override {
        if (cancelled) return make_error_code(errc::cancelled);
        auto end = std::min<std::int64_t>(offset + len, static_cast<std::int64_t>(data_.size()));
        for (auto p = offset / kPiece; p <= (end - 1) / kPiece; ++p) {
            if (missing_.count(p)) return make_error_code(errc::timeout_waiting_for_piece);
        }
        return Bytes(data_.begin() + offset, data_.begin() + end);
    }
    PieceIndex piece_at(std::int64_t offset) const override { return static_cast<PieceIndex>(offset / kPiece); }
    std::int64_t bytes_to_piece_end(std::int64_t offset) const override { return kPiece - offset % kPiece; }
    void cancel() override { cancelled = true; }
    std::int64_t file_size() const override { return static_cast<std::int64_t>(data_.size()); }

    bool cancelled = false;

private:
    Bytes data_;
    std::set<std::int64_t> missing_;
};

std::shared_ptr<const SubtitleTrack> some_lines() {
    SubtitleTrack t;
    t.language = "ita";
    for (int i = 0; i < 9; ++i) t.cues.push_back({i * 2000 + 300, i * 2000 + 1800, "Riga " + std::to_string(i)});
    return std::make_shared<const SubtitleTrack>(checked_subtitle_track(t).value());
}

Bytes read_all(StreamSource& s, std::int64_t chunk) {
    Bytes out;
    for (std::int64_t at = 0; at < s.file_size();) {
        auto got = s.read(at, chunk, 0ms);
        EXPECT_TRUE(got);
        if (!got || got.value().empty()) break;
        out.insert(out.end(), got.value().begin(), got.value().end());
        at += static_cast<std::int64_t>(got.value().size());
    }
    return out;
}

Bytes expected(const Bytes& file, const SubtitleTrack& track) {
    std::string why;
    auto s = read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why).value();
    auto layout = build_mkv_subtitle_layout(s, track);
    Bytes out;
    for (const auto& p : layout.pieces) {
        if (p.file_offset >= 0) out.insert(out.end(), file.begin() + p.file_offset, file.begin() + p.file_offset + p.length);
        else out.insert(out.end(), p.bytes->begin(), p.bytes->end());
    }
    return out;
}

Bytes fixture() { return read_file(std::string(SEEKSERVE_FIXTURE_DIR) + "/subtitles_base.mkv"); }

TEST(MkvSubtitleSource, ItWaitsForTheHeadAndTheCuesThenServesTheRebuiltFile) {
    auto file = fixture();
    auto inner = std::make_shared<PieceSource>(file);
    auto size = static_cast<std::int64_t>(file.size());
    inner->lose(size - 500, size - 1);  // the Cues, at the end
    MkvSubtitleSource source(inner, some_lines());

    EXPECT_EQ(source.state(), SubtitleShareState::pending);
    EXPECT_EQ(source.prepare(0ms).error(), make_error_code(errc::timeout_waiting_for_piece));
    auto waiting = source.preparing_range();
    ASSERT_TRUE(waiting);
    EXPECT_GE(waiting->end, size - 500) << "it says which bytes it waits for";
    EXPECT_EQ(source.read(0, 100, 0ms).error(), make_error_code(errc::timeout_waiting_for_piece));

    inner->arrive();
    ASSERT_TRUE(source.prepare(0ms));
    EXPECT_EQ(source.state(), SubtitleShareState::ready);
    EXPECT_FALSE(source.preparing_range());
    EXPECT_EQ(read_all(source, 7000), expected(file, *some_lines()));
    EXPECT_EQ(source.file_size(), static_cast<std::int64_t>(expected(file, *some_lines()).size()));
}

TEST(MkvSubtitleSource, AMissingPieceInTheMiddleSendsWhatIsThereFirst) {
    auto file = fixture();
    auto inner = std::make_shared<PieceSource>(file);
    MkvSubtitleSource source(inner, some_lines());
    ASSERT_TRUE(source.prepare(0ms));
    auto whole = expected(file, *some_lines());

    inner->lose(3 * kPiece, 3 * kPiece);
    Bytes got;
    std::error_code stop;
    while (got.size() < whole.size()) {
        auto part = source.read(static_cast<std::int64_t>(got.size()), 4096, 0ms);
        if (!part) { stop = part.error(); break; }
        got.insert(got.end(), part.value().begin(), part.value().end());
    }
    EXPECT_EQ(stop, make_error_code(errc::timeout_waiting_for_piece));
    EXPECT_GE(got.size(), static_cast<std::size_t>(2 * kPiece)) << "what is there goes out";
    EXPECT_LT(got.size(), static_cast<std::size_t>(4 * kPiece));
    EXPECT_TRUE(std::equal(got.begin(), got.end(), whole.begin()));
}

TEST(MkvSubtitleSource, NewBytesCountAsTheOriginalByteAfterThemForTheScheduler) {
    auto file = fixture();
    auto inner = std::make_shared<PieceSource>(file);
    MkvSubtitleSource source(inner, some_lines());
    ASSERT_TRUE(source.prepare(0ms));
    std::string why;
    auto s = read_mkv_structure(reader_of(file), static_cast<std::int64_t>(file.size()), why).value();
    auto layout = build_mkv_subtitle_layout(s, *some_lines());

    int checked = 0;
    for (std::size_t i = 0; i + 1 < layout.pieces.size(); ++i) {
        const auto& p = layout.pieces[i];
        if (p.file_offset >= 0 || layout.pieces[i + 1].file_offset < 0) continue;
        auto next = layout.pieces[i + 1].file_offset;
        EXPECT_EQ(source.file_offset(p.at), next);
        EXPECT_EQ(source.piece_at(p.at), static_cast<PieceIndex>(next / kPiece));
        EXPECT_EQ(source.bytes_to_piece_end(p.at), p.length) << "new bytes end with their piece";
        ++checked;
    }
    EXPECT_GT(checked, 3);
}

TEST(MkvSubtitleSource, AFileItCannotRebuildIsServedAsItIs) {
    Bytes text(40000);
    for (std::size_t i = 0; i < text.size(); ++i) text[i] = static_cast<std::uint8_t>(i * 7);
    auto inner = std::make_shared<PieceSource>(text);
    MkvSubtitleSource source(inner, some_lines());
    ASSERT_TRUE(source.prepare(0ms));
    EXPECT_EQ(source.state(), SubtitleShareState::unsupported);
    EXPECT_EQ(source.reason(), "neither a Matroska nor an MP4 file");
    EXPECT_EQ(source.file_size(), 40000);
    EXPECT_EQ(read_all(source, 9000), text);
}

Bytes mp4_fixture() { return read_file(std::string(SEEKSERVE_FIXTURE_DIR) + "/mp4_avc_aac.mp4"); }

// An MP4 is served as the Matroska file rebuilt from it (app WORK-37 step
// 7d): a TV that does not show an MP4's text tracks shows the lines.
TEST(MkvSubtitleSource, AnMp4IsServedAsAMatroskaFileWithTheLines) {
    auto file = mp4_fixture();
    auto inner = std::make_shared<PieceSource>(file);
    auto size = static_cast<std::int64_t>(file.size());
    inner->lose(size - 3000, size - 1);  // the moov, at the end
    MkvSubtitleSource source(inner, some_lines());
    EXPECT_EQ(source.prepare(0ms).error(), make_error_code(errc::timeout_waiting_for_piece));
    auto waiting = source.preparing_range();
    ASSERT_TRUE(waiting);
    EXPECT_GE(waiting->end, size - 3000) << "it waits for the moov";

    inner->arrive();
    ASSERT_TRUE(source.prepare(0ms));
    EXPECT_EQ(source.state(), SubtitleShareState::ready);
    EXPECT_EQ(source.content_type(), std::optional<std::string>("video/x-matroska"));

    std::string why;
    auto mp4 = read_mp4_structure(reader_of(file), size, why).value();
    auto rebuilt = Mp4MkvFile::build(mp4, *some_lines(), why);
    ASSERT_TRUE(rebuilt);
    EXPECT_EQ(source.file_size(), rebuilt->size());
    auto got = read_all(source, 5000);
    ASSERT_EQ(static_cast<std::int64_t>(got.size()), rebuilt->size());
    auto m = read_mkv_structure(reader_of(got), static_cast<std::int64_t>(got.size()), why);
    ASSERT_TRUE(m) << why;
    auto clusters = clusters_of(got, m.value());
    int lines = 0;
    for (const auto& c : clusters) {
        for (const auto& b : c.blocks) lines += b.track == rebuilt->subtitle_track() ? 1 : 0;
    }
    EXPECT_EQ(lines, 9);
    // For the scheduler, its bytes stand for the MP4's.
    EXPECT_EQ(source.file_offset(source.file_size() - 1), size - 1) << "the Cues for the end";
    EXPECT_LT(source.file_offset(0), 100) << "the head for the first sample";
}

// A Matroska file stays one: the type the server says comes from its name.
TEST(MkvSubtitleSource, AMatroskaFileKeepsTheTypeOfItsName) {
    auto inner = std::make_shared<PieceSource>(fixture());
    MkvSubtitleSource source(inner, some_lines());
    EXPECT_FALSE(source.content_type());
    ASSERT_TRUE(source.prepare(0ms));
    EXPECT_FALSE(source.content_type());
}

// An MP4 this cannot rebuild is served as it is, with the reason.
TEST(MkvSubtitleSource, AFragmentedMp4IsServedAsItIs) {
    auto file = read_file(std::string(SEEKSERVE_FIXTURE_DIR) + "/mp4_fragmented.mp4");
    auto inner = std::make_shared<PieceSource>(file);
    MkvSubtitleSource source(inner, some_lines());
    ASSERT_TRUE(source.prepare(0ms));
    EXPECT_EQ(source.state(), SubtitleShareState::unsupported);
    EXPECT_EQ(source.reason(), "a fragmented MP4");
    EXPECT_FALSE(source.content_type());
    EXPECT_EQ(read_all(source, 9000), file);
}

TEST(MkvSubtitleSource, CancellingStopsItAndTheSourceItWraps) {
    auto inner = std::make_shared<PieceSource>(fixture());
    MkvSubtitleSource source(inner, some_lines());
    source.cancel();
    EXPECT_TRUE(inner->cancelled);
    EXPECT_EQ(source.prepare(0ms).error(), make_error_code(errc::cancelled));
    EXPECT_EQ(source.read(0, 10, 0ms).error(), make_error_code(errc::cancelled));
    EXPECT_EQ(source.piece_at(0), -1);
}

}  // namespace
}  // namespace seekserve
