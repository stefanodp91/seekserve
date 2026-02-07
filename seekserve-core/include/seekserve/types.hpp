#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace seekserve {

using TorrentId = std::string;
using FileIndex = std::int32_t;
using PieceIndex = std::int32_t;

struct PieceSpan {
    PieceIndex first;
    PieceIndex last;         // inclusive
    int first_offset;        // byte offset into first piece
    int last_length;         // bytes needed from last piece
};

struct ByteRange {
    std::int64_t start;
    std::int64_t end;        // inclusive, per RFC 7233
};

struct FileInfo {
    FileIndex index;
    std::string path;
    std::int64_t size;
    std::int64_t offset_in_torrent;
    PieceIndex first_piece;
    PieceIndex end_piece;    // exclusive
};

enum class StreamMode {
    StreamingFirst,
    DownloadAssist,
    DownloadFirst
};

struct StreamStatus {
    TorrentId torrent_id;
    FileIndex file_index;
    StreamMode mode;
    double download_rate;
    double upload_rate;
    int peer_count;
    std::int64_t total_size;
    std::int64_t downloaded;
    std::int64_t contiguous_bytes;
    float progress;
    bool offline_ready;
};

} // namespace seekserve
