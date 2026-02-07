# Storage & Offline Cache Policy

## Piece Storage

Pieces are stored by libtorrent at the configured `save_path`. File layout follows the torrent's internal structure. Once `piece_finished_alert` fires, bytes are available for direct disk reads.

## File Selection

In multi-file torrents, only the selected file is downloaded:
- Selected file: `default_priority` (4)
- All other files: `dont_download` (0)
- Shared boundary pieces (overlapping files) are still downloaded

## Offline Cache Manager

SQLite-based index tracking downloaded files for offline playback.

### Schema

```sql
CREATE TABLE cache_entries (
    torrent_id   TEXT NOT NULL,
    file_index   INTEGER NOT NULL,
    file_path    TEXT NOT NULL,
    file_size    INTEGER NOT NULL DEFAULT 0,
    progress     REAL NOT NULL DEFAULT 0.0,
    offline_ready INTEGER NOT NULL DEFAULT 0,
    last_access  TEXT NOT NULL DEFAULT (datetime('now')),
    added_at     TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (torrent_id, file_index)
);
```

### SQLite Configuration
- WAL mode enabled for concurrent reads/writes
- `INSERT OR IGNORE` for idempotent `on_torrent_added()`
- Thread-safe via mutex

### Lifecycle Events

| Event | Action |
|-------|--------|
| Torrent added | `INSERT OR IGNORE` entries for all files |
| Progress update | `UPDATE progress` (called every 1s tick) |
| File completed | `UPDATE offline_ready = 1` (on `file_completed_alert`) |
| File accessed | `UPDATE last_access` (on stream request) |
| Torrent removed | Entries remain for offline access |

### Quota Enforcement

When `max_storage_bytes > 0`, the cache enforces a storage quota:

1. Query total size of all cached files
2. While total > quota:
   - Select LRU entry (`ORDER BY last_access ASC`)
   - Delete entry and its files from disk
3. Called after each `on_file_completed()`

### Offline Playback

A file is offline-ready when:
- `offline_ready = 1` in cache_entries
- All pieces for the file exist on disk

The HTTP Range Server can serve offline-ready files without any active torrent session.

## Configuration

```cpp
struct CacheConfig {
    std::string db_path = "seekserve_cache.db";
    int64_t max_storage_bytes = 0;  // 0 = unlimited
    std::chrono::hours ttl{24 * 30}; // 30 days
    bool offline_download_enabled = true;
};
```
