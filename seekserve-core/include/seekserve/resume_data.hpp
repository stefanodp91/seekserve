#pragma once

#include <vector>

#include <libtorrent/add_torrent_params.hpp>

namespace seekserve {

/// A torrent's resume data as the engine keeps it: libtorrent's bencoded
/// resume data without the addresses of other peers, which are never
/// written to the device.
std::vector<char> encode_resume_data(lt::add_torrent_params params);

} // namespace seekserve
