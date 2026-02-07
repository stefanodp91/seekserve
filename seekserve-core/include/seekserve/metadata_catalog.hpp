#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "seekserve/types.hpp"
#include "seekserve/error.hpp"

namespace seekserve {

class MetadataCatalog {
public:
    MetadataCatalog() = default;

    void on_metadata_received(const TorrentId& id,
                              std::shared_ptr<const lt::torrent_info> ti);
    void remove(const TorrentId& id);

    Result<std::vector<FileInfo>> list_files(const TorrentId& id) const;
    Result<FileInfo> get_file(const TorrentId& id, FileIndex fi) const;
    Result<void> select_file(const TorrentId& id, FileIndex fi,
                             lt::torrent_handle& handle);

    std::optional<FileIndex> selected_file(const TorrentId& id) const;
    std::shared_ptr<const lt::torrent_info> torrent_info(const TorrentId& id) const;
    bool has_metadata(const TorrentId& id) const;

private:
    struct TorrentEntry {
        std::shared_ptr<const lt::torrent_info> info;
        std::vector<FileInfo> files;
        std::optional<FileIndex> selected;
    };

    std::unordered_map<TorrentId, TorrentEntry> catalog_;
    mutable std::mutex mu_;
};

} // namespace seekserve
