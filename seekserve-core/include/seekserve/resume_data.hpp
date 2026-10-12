#pragma once

#include <optional>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>

namespace seekserve {

/// A torrent's resume data as the engine keeps it: libtorrent's bencoded
/// resume data without the addresses of other peers, which are never
/// written to the device.
std::vector<char> encode_resume_data(lt::add_torrent_params params);

/// What encode_resume_data wrote, to add the torrent again from; nothing
/// when the bytes are not resume data (empty, cut short, damaged).
std::optional<lt::add_torrent_params> decode_resume_data(const std::vector<char>& data);

/// No player survives a restart: a torrent comes back without the file one
/// was reading ahead of a download (wanted_file_priorities), and without
/// the piece priorities its deadlines left. A file wanted alone stays.
void drop_reader_priorities(lt::add_torrent_params& params);

} // namespace seekserve
