#include "seekserve/resume_data.hpp"
#include "seekserve/metadata_catalog.hpp"

#include <algorithm>

#include <libtorrent/download_priority.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>

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

void drop_reader_priorities(lt::add_torrent_params& params) {
    // Piece priorities are only ever what deadlines left: the top for a
    // piece that was due, the bottom for one that was dropped. libtorrent
    // applies them over the files' own, so a piece of a file nobody wants
    // would come back wanted.
    params.piece_priorities.clear();

    if (!params.ti) return;
    const auto& layout = params.ti->layout();
    const auto count = std::min(params.file_priorities.size(),
                                static_cast<std::size_t>(layout.num_files()));
    for (std::size_t i = 0; i < count; ++i) {
        // Above a download's priority: the reader's file, when another one
        // downloaded behind it, and the subtitles that rode with it.
        if (params.file_priorities[i] <= lt::default_priority) continue;
        const auto path = layout.file_path(lt::file_index_t{static_cast<int>(i)});
        params.file_priorities[i] =
            is_subtitle_file(path) ? lt::default_priority : lt::dont_download;
    }
}

} // namespace seekserve
