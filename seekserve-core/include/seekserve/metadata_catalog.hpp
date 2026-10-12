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

/// A subtitle file a player loads next to its video (.srt, .vtt, .ass, .ssa).
bool is_subtitle_file(const std::string& path);

/// The priority of each file of a torrent that wants two of them at most:
/// `reader`, the file a player reads, and `background`, the file that
/// downloads behind it (-1 for none; the same file twice is the reader).
/// With a file behind it the reader comes first; alone, either has the
/// priority of a download. The subtitles ride with the first of the two,
/// and no other file is downloaded.
std::vector<lt::download_priority_t> wanted_file_priorities(
    const std::vector<FileInfo>& files, FileIndex reader, FileIndex background);

struct WantedFiles {
    FileIndex reader{-1};
    FileIndex background{-1};
};

class MetadataCatalog {
public:
    MetadataCatalog() = default;

    void on_metadata_received(const TorrentId& id,
                              std::shared_ptr<const lt::torrent_info> ti);
    void remove(const TorrentId& id);

    Result<std::vector<FileInfo>> list_files(const TorrentId& id) const;
    Result<FileInfo> get_file(const TorrentId& id, FileIndex fi) const;
    // The file `fi` alone, as a player's.
    Result<void> select_file(const TorrentId& id, FileIndex fi,
                             lt::torrent_handle& handle);
    // The two files the torrent wants from now on (wanted_file_priorities);
    // an index that is not a file of the torrent changes nothing.
    Result<void> set_wanted_files(const TorrentId& id, FileIndex reader,
                                  FileIndex background, lt::torrent_handle& handle);

    // The reader's file, or the one that downloads when no player reads.
    std::optional<FileIndex> selected_file(const TorrentId& id) const;
    // The file a player reads and the one behind it, -1 where there is none.
    WantedFiles wanted_files(const TorrentId& id) const;
    std::shared_ptr<const lt::torrent_info> torrent_info(const TorrentId& id) const;
    bool has_metadata(const TorrentId& id) const;

private:
    struct TorrentEntry {
        std::shared_ptr<const lt::torrent_info> info;
        std::vector<FileInfo> files;
        WantedFiles wanted;
    };

    std::unordered_map<TorrentId, TorrentEntry> catalog_;
    mutable std::mutex mu_;
};

} // namespace seekserve
