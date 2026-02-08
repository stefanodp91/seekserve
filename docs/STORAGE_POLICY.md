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

### Schema — `cache_entries`

```sql
CREATE TABLE IF NOT EXISTS cache_entries (
    torrent_id   TEXT NOT NULL,
    file_index   INTEGER NOT NULL,
    file_path    TEXT NOT NULL,
    file_size    INTEGER NOT NULL,
    progress     REAL DEFAULT 0.0,
    offline_ready INTEGER DEFAULT 0,
    last_access  INTEGER NOT NULL,
    added_at     INTEGER NOT NULL,
    PRIMARY KEY (torrent_id, file_index)
);
```

Timestamps (`last_access`, `added_at`) are stored as epoch seconds (Unix time). Values are always provided explicitly via bind parameters — no SQL defaults.

### Schema — `torrents`

Persists torrent URIs for auto-restore on engine restart.

```sql
CREATE TABLE IF NOT EXISTS torrents (
    torrent_id TEXT PRIMARY KEY,
    uri        TEXT NOT NULL,
    added_at   INTEGER NOT NULL
);
```

Methods:
- `save_torrent_uri(id, uri)` — `INSERT OR REPLACE` on add
- `remove_torrent_uri(id)` — `DELETE` on remove
- `list_torrent_uris()` — `SELECT ... ORDER BY added_at` for restore

### SQLite Configuration
- WAL mode enabled for concurrent reads/writes
- `INSERT OR IGNORE` for idempotent `on_torrent_added()`
- Thread-safe via mutex

### Lifecycle Events

| Event | Action |
|-------|--------|
| Torrent added | `INSERT OR IGNORE` entries for all files in `cache_entries` |
| Torrent added | `INSERT OR REPLACE` URI in `torrents` |
| Progress update | `UPDATE progress` in `cache_entries` (called every 1s tick) |
| File completed | `UPDATE offline_ready = 1` in `cache_entries` (on `file_completed_alert`) |
| File accessed | `UPDATE last_access` in `cache_entries` (on stream request) |
| Torrent removed | `DELETE` from `torrents`; `cache_entries` remain for offline access |
| Engine start | Auto-restore: read `torrents` table, re-add each URI to session |

### Quota Enforcement

When `max_storage_bytes > 0`, the cache enforces a storage quota:

1. Query total size of all cached files
2. While total > quota:
   - Select LRU entry (`ORDER BY last_access ASC`)
   - Delete entry from the SQLite database
3. Called after each `on_file_completed()`

**Note**: `enforce_quota()` only removes database entries. Physical files on disk are NOT deleted — they remain until libtorrent removes them via `remove_torrent(delete_files=true)`.

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
