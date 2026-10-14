// seekserve's patch to libtorrent's part file (app BUG-202,
// patches/libtorrent/0001-part-file-export-a-slot-without-this-file.patch).

#include <gtest/gtest.h>

#include <libtorrent/aux_/part_file.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/units.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr int kPiece = 16 * 1024;

class LibtorrentPartFile : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("seekserve-part-file-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path dir_;
};

// Two files share piece 1: the first ends in its middle, the second starts
// there. The piece came while the second file was wanted and the first was
// not, so only the first file's half is in the part file. Exporting the
// second file finds nothing of it there: no error, nothing copied.
TEST_F(LibtorrentPartFile, AnExportSkipsASlotThatHoldsOnlyAnotherFilesPart) {
    lt::aux::part_file part(dir_.string(), "pack.parts", 4, kPiece);
    lt::error_code ec;
    const std::vector<char> first_half(kPiece / 2, 'a');
    part.write(first_half, lt::piece_index_t{1}, 0, ec);
    ASSERT_FALSE(ec) << ec.message();

    std::vector<std::pair<std::int64_t, std::size_t>> copied;
    part.export_file(
        [&](std::int64_t file_offset, lt::span<char> buf) {
            copied.emplace_back(file_offset, static_cast<std::size_t>(buf.size()));
        },
        kPiece + kPiece / 2, kPiece / 2 + kPiece, ec);

    EXPECT_FALSE(ec) << ec.message();
    EXPECT_TRUE(copied.empty());
}

// What the part file does have of the second file is still copied, at its
// place in the file. The half slot is not the last one here, so reading it
// does not reach the end of the part file: what was never written there comes
// out as zeros, which libtorrent, having checked that piece, downloads again.
TEST_F(LibtorrentPartFile, AnExportStillCopiesWhatThePartFileHasOfTheFile) {
    lt::aux::part_file part(dir_.string(), "pack.parts", 4, kPiece);
    lt::error_code ec;
    const std::vector<char> first_half(kPiece / 2, 'a');
    part.write(first_half, lt::piece_index_t{1}, 0, ec);
    ASSERT_FALSE(ec) << ec.message();
    const std::vector<char> whole(kPiece, 'b');
    part.write(whole, lt::piece_index_t{2}, 0, ec);
    ASSERT_FALSE(ec) << ec.message();

    std::vector<char> file(static_cast<std::size_t>(kPiece / 2 + kPiece), 'x');
    part.export_file(
        [&](std::int64_t file_offset, lt::span<char> buf) {
            std::copy(buf.begin(), buf.end(), file.begin() + file_offset);
        },
        kPiece + kPiece / 2, kPiece / 2 + kPiece, ec);

    EXPECT_FALSE(ec) << ec.message();
    EXPECT_EQ(std::vector<char>(file.begin() + kPiece / 2, file.end()), whole);
}

} // namespace
