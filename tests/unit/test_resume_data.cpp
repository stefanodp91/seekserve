// Resume data as the engine keeps it (app WORK-29, wave P2b): what libtorrent
// writes, without the addresses of other peers.

#include <gtest/gtest.h>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/bitfield.hpp>
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

} // namespace
} // namespace seekserve
