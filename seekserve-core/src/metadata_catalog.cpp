#include "seekserve/metadata_catalog.hpp"

#include <libtorrent/file_storage.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>

namespace seekserve {

namespace {

// Subtitle files the player can load next to the video (.srt, .vtt, .ass, .ssa).
bool is_subtitle(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "srt" || ext == "vtt" || ext == "ass" || ext == "ssa";
}

} // namespace

bool is_subtitle_file(const std::string& path) {
    return is_subtitle(path);
}

std::vector<lt::download_priority_t> wanted_file_priorities(
    const std::vector<FileInfo>& files, FileIndex reader, FileIndex background) {
    const auto count = static_cast<FileIndex>(files.size());
    const bool reads = reader >= 0 && reader < count;
    const bool downloads = background >= 0 && background < count && background != reader;

    std::vector<lt::download_priority_t> priorities(files.size(), lt::dont_download);
    if (!reads && !downloads) return priorities;

    // libtorrent asks for the pieces of a higher priority first: with a file
    // behind it the reader's is ahead, and its last byte comes before the
    // other file's first. Alone, a file is a download like any other.
    const auto first = reads && downloads ? lt::top_priority : lt::default_priority;
    // Subtitles come along with the video: they are a few KB, and the
    // player loads them from disk instead of selecting them, which would
    // stop the video from downloading.
    for (FileIndex i = 0; i < count; ++i) {
        if (is_subtitle(files[static_cast<std::size_t>(i)].path)) {
            priorities[static_cast<std::size_t>(i)] = first;
        }
    }
    if (downloads) priorities[static_cast<std::size_t>(background)] = lt::default_priority;
    if (reads) priorities[static_cast<std::size_t>(reader)] = first;
    return priorities;
}

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
    // To set_wanted_files -1 is "no file"; here it is no file of the torrent.
    if (fi < 0) {
        return make_error_code(has_metadata(id) ? errc::file_not_found
                                                : errc::metadata_not_ready);
    }
    return set_wanted_files(id, fi, -1, handle);
}

Result<void> MetadataCatalog::set_wanted_files(const TorrentId& id, FileIndex reader,
                                               FileIndex background,
                                               lt::torrent_handle& handle) {
    std::lock_guard lock(mu_);
    auto it = catalog_.find(id);
    if (it == catalog_.end()) {
        return make_error_code(errc::metadata_not_ready);
    }

    auto& entry = it->second;
    const auto count = static_cast<FileIndex>(entry.files.size());
    // -1 is no file; anything else has to be one of the torrent's.
    if (reader < -1 || reader >= count || background < -1 || background >= count) {
        return make_error_code(errc::file_not_found);
    }
    if (background == reader) background = -1;

    handle.prioritize_files(wanted_file_priorities(entry.files, reader, background));
    entry.wanted = WantedFiles{reader, background};

    spdlog::info("MetadataCatalog: torrent {} wants file {} for a reader and file {} behind it",
                 id, reader, background);

    return Result<void>{};
}

std::optional<FileIndex> MetadataCatalog::selected_file(const TorrentId& id) const {
    const auto wanted = wanted_files(id);
    if (wanted.reader >= 0) return wanted.reader;
    if (wanted.background >= 0) return wanted.background;
    return std::nullopt;
}

WantedFiles MetadataCatalog::wanted_files(const TorrentId& id) const {
    std::lock_guard lock(mu_);
    auto it = catalog_.find(id);
    if (it == catalog_.end()) return {};
    return it->second.wanted;
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
