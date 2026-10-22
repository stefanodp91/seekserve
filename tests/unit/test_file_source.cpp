// FileSource: a whole file on disk as a stream source, for a share of a file
// that is no torrent's any more (app WORK-37, DEC-627).

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "seekserve/error.hpp"
#include "seekserve/file_source.hpp"

namespace seekserve {
namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

constexpr int kSize = 300000;

std::uint8_t byte_at(std::int64_t pos) { return static_cast<std::uint8_t>((pos * 13 + 5) % 251); }

class FileSourceTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / "seekserve_test_file_source";
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        path_ = (dir_ / "film.mkv").string();
        std::ofstream out(path_, std::ios::binary);
        for (int i = 0; i < kSize; ++i) out.put(static_cast<char>(byte_at(i)));
    }
    void TearDown() override { fs::remove_all(dir_); }

    fs::path dir_;
    std::string path_;
};

TEST_F(FileSourceTest, ReadsAnyRangeOfTheFile) {
    auto source = FileSource::open(path_);
    ASSERT_TRUE(source) << source.error().message();
    auto& src = *source.value();
    EXPECT_EQ(src.file_size(), kSize);

    auto part = src.read(123456, 70000, 10ms);
    ASSERT_TRUE(part);
    ASSERT_EQ(part.value().size(), 70000u);
    for (int i = 0; i < 70000; ++i) ASSERT_EQ(part.value()[i], byte_at(123456 + i)) << i;

    // A read past the end gives what there is.
    auto tail = src.read(kSize - 10, 100, 10ms);
    ASSERT_TRUE(tail);
    EXPECT_EQ(tail.value().size(), 10u);
}

// There are no pieces: nothing for the server to report or to wait for.
TEST_F(FileSourceTest, HasNoPieces) {
    auto source = FileSource::open(path_);
    ASSERT_TRUE(source);
    EXPECT_EQ(source.value()->piece_at(0), 0);
    EXPECT_EQ(source.value()->piece_at(kSize - 1), 0);
    EXPECT_EQ(source.value()->bytes_to_piece_end(0), -1);
}

TEST_F(FileSourceTest, ReadsNothingOnceCancelled) {
    auto source = FileSource::open(path_);
    ASSERT_TRUE(source);
    source.value()->cancel();
    auto part = source.value()->read(0, 100, 10ms);
    ASSERT_FALSE(part);
    EXPECT_EQ(part.error(), make_error_code(errc::cancelled));
    EXPECT_EQ(source.value()->piece_at(0), -1);
}

// The file is held from open: renaming another file into its place does
// not change what is read.
TEST_F(FileSourceTest, KeepsTheFileItOpened) {
    auto source = FileSource::open(path_);
    ASSERT_TRUE(source);
    auto other = (dir_ / "other.bin").string();
    std::ofstream(other, std::ios::binary) << std::string(10, 'z');
    fs::rename(other, path_);

    auto part = source.value()->read(0, 4, 10ms);
    ASSERT_TRUE(part);
    EXPECT_EQ(part.value()[0], byte_at(0));
    EXPECT_EQ(source.value()->file_size(), kSize);
}

TEST_F(FileSourceTest, OpensOnlyAFileThatIsThere) {
    EXPECT_EQ(FileSource::open((dir_ / "missing.mkv").string()).error(),
              make_error_code(errc::file_not_found));
    EXPECT_EQ(FileSource::open(dir_.string()).error(), make_error_code(errc::invalid_argument));
}

}  // namespace
}  // namespace seekserve
