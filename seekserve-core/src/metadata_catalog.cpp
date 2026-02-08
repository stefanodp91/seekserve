#include "seekserve/metadata_catalog.hpp"

#include <libtorrent/file_storage.hpp>

#include <spdlog/spdlog.h>

namespace seekserve {

void MetadataCatalog::on_metadata_received(const TorrentId& id,
                                           std::shared_ptr<const lt::torrent_info> ti) {
    std::lock_guard lock(mu_);

    if (catalog_.count(id)) return;

    TorrentEntry entry;
    entry.info = ti;

    const auto& fs = ti->layout();
    entry.files.reserve(fs.num_files());

    for (lt::file_index_t i{0}; static_cast<int>(i) < fs.num_files(); ++i) {
        FileInfo fi;
        fi.index = static_cast<FileIndex>(i);
        fi.path = fs.file_path(i);
        fi.size = fs.file_size(i);
        fi.offset_in_torrent = fs.file_offset(i);

        auto first_req = fs.map_file(i, 0, 1);
        fi.first_piece = static_cast<PieceIndex>(first_req.piece);

        if (fi.size > 0) {
            auto last_req = fs.map_file(i, fi.size - 1, 1);
            fi.end_piece = static_cast<PieceIndex>(last_req.piece) + 1;
        } else {
            fi.end_piece = fi.first_piece;
        }

        entry.files.push_back(std::move(fi));
    }

    spdlog::info("MetadataCatalog: registered {} files for torrent {}",
                 entry.files.size(), id);

    catalog_[id] = std::move(entry);
}

void MetadataCatalog::remove(const TorrentId& id) {
    std::lock_guard lock(mu_);
    catalog_.erase(id);
}

Result<std::vector<FileInfo>> MetadataCatalog::list_files(const TorrentId& id) const {
    std::lock_guard lock(mu_);
    auto it = catalog_.find(id);
    if (it == catalog_.end()) {
        return make_error_code(errc::metadata_not_ready);
    }
    return it->second.files;
}

Result<FileInfo> MetadataCatalog::get_file(const TorrentId& id, FileIndex fi) const {
    std::lock_guard lock(mu_);
    auto it = catalog_.find(id);
    if (it == catalog_.end()) {
        return make_error_code(errc::metadata_not_ready);
    }
    if (fi < 0 || fi >= static_cast<FileIndex>(it->second.files.size())) {
        return make_error_code(errc::file_not_found);
    }
    return it->second.files[fi];
}

Result<void> MetadataCatalog::select_file(const TorrentId& id, FileIndex fi,
                                          lt::torrent_handle& handle) {
    std::lock_guard lock(mu_);
    auto it = catalog_.find(id);
    if (it == catalog_.end()) {
        return make_error_code(errc::metadata_not_ready);
    }
    if (fi < 0 || fi >= static_cast<FileIndex>(it->second.files.size())) {
        return make_error_code(errc::file_not_found);
    }

    auto& entry = it->second;
    const int num_files = static_cast<int>(entry.files.size());

    std::vector<lt::download_priority_t> priorities(num_files, lt::dont_download);
    priorities[fi] = lt::default_priority;
    handle.prioritize_files(priorities);

    entry.selected = fi;

    spdlog::info("MetadataCatalog: selected file {} '{}' for torrent {}",
                 fi, entry.files[fi].path, id);

    return Result<void>{};
}

std::optional<FileIndex> MetadataCatalog::selected_file(const TorrentId& id) const {
    std::lock_guard lock(mu_);
    auto it = catalog_.find(id);
    if (it == catalog_.end()) return std::nullopt;
    return it->second.selected;
}

std::shared_ptr<const lt::torrent_info> MetadataCatalog::torrent_info(const TorrentId& id) const {
    std::lock_guard lock(mu_);
    auto it = catalog_.find(id);
    if (it == catalog_.end()) return nullptr;
    return it->second.info;
}

bool MetadataCatalog::has_metadata(const TorrentId& id) const {
    std::lock_guard lock(mu_);
    return catalog_.count(id) > 0;
}

} // namespace seekserve
