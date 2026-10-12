// Resume data as the engine keeps it (app WORK-29, wave P2b): what libtorrent
// writes, without the addresses of other peers.

#include <gtest/gtest.h>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/bitfield.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/socket.hpp>

#include "seekserve/resume_data.hpp"

namespace seekserve {
namespace {

lt::add_torrent_params params_with_peers() {
    lt::add_torrent_params p;
    p.info_hashes.v1 = lt::sha1_hash("0123456789abcdefghij");
    p.name = "a.mkv";
    p.save_path = "/data/engine";
    p.trackers = {"http://tracker.invalid/announce"};
    p.have_pieces.resize(8, false);
    p.have_pieces.set_bit(lt::piece_index_t{1});
    p.have_pieces.set_bit(lt::piece_index_t{5});
    p.peers = {
        lt::tcp::endpoint(boost::asio::ip::make_address("203.0.113.7"), 51413),
        lt::tcp::endpoint(boost::asio::ip::make_address("2001:db8::7"), 6881),
    };
    p.banned_peers = {lt::tcp::endpoint(boost::asio::ip::make_address("198.51.100.9"), 6881)};
    return p;
}

lt::add_torrent_params decode(const std::vector<char>& data) {
    lt::error_code ec;
    auto p = lt::read_resume_data(data, ec);
    EXPECT_FALSE(ec) << ec.message();
    return p;
}

TEST(ResumeDataTest, KeepsWhatARestartNeeds) {
    auto in = params_with_peers();

    auto out = decode(encode_resume_data(in));

    EXPECT_EQ(out.info_hashes.v1, in.info_hashes.v1);
    EXPECT_EQ(out.name, "a.mkv");
    EXPECT_EQ(out.save_path, "/data/engine");
    EXPECT_EQ(out.trackers, in.trackers);
    ASSERT_EQ(out.have_pieces.size(), 8);
    EXPECT_TRUE(out.have_pieces.get_bit(lt::piece_index_t{1}));
    EXPECT_TRUE(out.have_pieces.get_bit(lt::piece_index_t{5}));
    EXPECT_FALSE(out.have_pieces.get_bit(lt::piece_index_t{0}));
}

// The addresses of the people a torrent was shared with stay off the device.
TEST(ResumeDataTest, HasNoPeerAddresses) {
    auto data = encode_resume_data(params_with_peers());

    auto out = decode(data);
    EXPECT_TRUE(out.peers.empty());
    EXPECT_TRUE(out.banned_peers.empty());

    // Not even as an empty list: the keys are not written at all.
    std::string text(data.begin(), data.end());
    EXPECT_EQ(text.find("5:peers"), std::string::npos);
    EXPECT_EQ(text.find("6:peers6"), std::string::npos);
    EXPECT_EQ(text.find("12:banned_peers"), std::string::npos);
    EXPECT_EQ(text.find("13:banned_peers6"), std::string::npos);
}

TEST(ResumeDataTest, ReadsBackWhatItWrote) {
    auto in = params_with_peers();

    auto out = decode_resume_data(encode_resume_data(in));

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->info_hashes.v1, in.info_hashes.v1);
    EXPECT_EQ(out->name, "a.mkv");
    EXPECT_TRUE(out->peers.empty());
}

// Whatever is in the database, the engine must start: bytes that are not
// resume data are nothing, and the torrent is restored another way.
TEST(ResumeDataTest, UnreadableBytesAreNothing) {
    EXPECT_FALSE(decode_resume_data({}).has_value());
    EXPECT_FALSE(decode_resume_data({'x', 'y', 'z'}).has_value());

    auto cut = encode_resume_data(params_with_peers());
    cut.resize(cut.size() / 2);
    EXPECT_FALSE(decode_resume_data(cut).has_value());

    // Well-formed bencoding that is not resume data.
    std::string other = "d3:fooi1ee";
    EXPECT_FALSE(decode_resume_data({other.begin(), other.end()}).has_value());
}

// A pack as libtorrent restores it: an episode, its subtitles and two more
// episodes, in this order, with whatever priorities the test gives them.
lt::add_torrent_params restored_pack(std::vector<lt::download_priority_t> file_priorities) {
    const int piece_size = 16 * 1024;
    std::vector<lt::create_file_entry> files;
    files.emplace_back("Show.S01/Show.S01E01.mkv", 3 * piece_size + 100);
    files.emplace_back("Show.S01/Show.S01E01.srt", 700);
    files.emplace_back("Show.S01/Show.S01E02.mkv", 5 * piece_size + 100);
    files.emplace_back("Show.S01/Show.S01E03.mkv", 2 * piece_size);
    lt::create_torrent ct(std::move(files), piece_size, lt::create_torrent::v1_only);
    for (lt::piece_index_t i{0}; static_cast<int>(i) < ct.num_pieces(); ++i) {
        ct.set_hash(i, lt::sha1_hash("0123456789abcdefghij"));
    }

    lt::add_torrent_params p;
    p.ti = lt::load_torrent_buffer(ct.generate_buf()).ti;
    p.file_priorities = std::move(file_priorities);
    // What deadlines leave behind: pieces at the top while they are due and
    // at the bottom once they are dropped, whatever their file says.
    p.piece_priorities.assign(static_cast<std::size_t>(p.ti->num_pieces()), lt::default_priority);
    p.piece_priorities[4] = lt::top_priority;
    p.piece_priorities[5] = lt::low_priority;
    return p;
}

using FilePriorities = std::vector<lt::download_priority_t>;

// The second episode was in a player and the third downloaded behind it: the
// player is gone, so only the download and the subtitles are still wanted,
// and no piece keeps the priority a deadline gave it.
TEST(ResumeDataTest, ARestartIsThePlayerLeaving) {
    auto p = restored_pack({lt::dont_download, lt::top_priority,
                            lt::top_priority, lt::default_priority});

    drop_reader_priorities(p);

    EXPECT_EQ(p.file_priorities,
              (FilePriorities{lt::dont_download, lt::default_priority,
                              lt::dont_download, lt::default_priority}));
    EXPECT_TRUE(p.piece_priorities.empty());
}

// One file wanted alone, as select_file leaves it, comes back wanted: who
// wanted it, a player or the queue, is the app's to say.
TEST(ResumeDataTest, AFileWantedAloneComesBackWanted) {
    const FilePriorities alone{lt::default_priority, lt::default_priority,
                               lt::dont_download, lt::dont_download};
    auto p = restored_pack(alone);

    drop_reader_priorities(p);

    EXPECT_EQ(p.file_priorities, alone);
    EXPECT_TRUE(p.piece_priorities.empty());
}

// A magnet whose metadata never came has no files to speak of.
TEST(ResumeDataTest, WithoutMetadataThereIsNoReaderToDrop) {
    auto p = params_with_peers();

    drop_reader_priorities(p);

    EXPECT_TRUE(p.file_priorities.empty());
    EXPECT_TRUE(p.piece_priorities.empty());
    EXPECT_EQ(p.name, "a.mkv");
}

} // namespace
} // namespace seekserve
