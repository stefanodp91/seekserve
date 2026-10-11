#include "seekserve/resume_data.hpp"

#include <libtorrent/write_resume_data.hpp>

namespace seekserve {

std::vector<char> encode_resume_data(lt::add_torrent_params params) {
    // libtorrent would write the peers it knows, to reconnect faster.
    params.peers.clear();
    params.banned_peers.clear();
    return lt::write_resume_data_buf(params);
}

} // namespace seekserve
