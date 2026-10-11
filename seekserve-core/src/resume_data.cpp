#include "seekserve/resume_data.hpp"

#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>

namespace seekserve {

std::vector<char> encode_resume_data(lt::add_torrent_params params) {
    // libtorrent would write the peers it knows, to reconnect faster.
    params.peers.clear();
    params.banned_peers.clear();
    return lt::write_resume_data_buf(params);
}

std::optional<lt::add_torrent_params> decode_resume_data(const std::vector<char>& data) {
    if (data.empty()) return std::nullopt;
    lt::error_code ec;
    auto params = lt::read_resume_data(data, ec);
    if (ec) return std::nullopt;
    return params;
}

} // namespace seekserve
