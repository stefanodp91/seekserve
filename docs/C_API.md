# C API Reference

## Overview

The C API (`seekserve_c.h`) provides an opaque handle-based interface for embedding SeekServe in any language with C FFI support (Dart/Flutter, Swift, Kotlin, etc.).

## Engine Lifecycle

```c
// Create engine with JSON config (or NULL/empty for defaults)
SeekServeEngine* ss_engine_create(const char* config_json);

// Destroy engine (stops servers, frees all resources)
void ss_engine_destroy(SeekServeEngine* engine);
```

### Config JSON

```json
{
    "save_path": "./downloads",
    "auth_token": "my_secret_token",
    "stream_port": 0,
    "control_port": 0,
    "max_storage_bytes": 0,
    "cache_db_path": "seekserve_cache.db",
    "log_level": "info",
    "enable_webtorrent": false,
    "extra_trackers": ["udp://tracker.example.com:6969"],
    "ca_cert_file": "/path/to/ca-bundle.pem"
}
```

All fields are optional. Unknown fields are ignored.

The stream servers take three more keys, in seconds (app BUG-88): `read_timeout` (default
30) for the whole request to arrive; `connection_timeout` (default 0 = no limit) for a write
to the newest reader of a stream; `stale_reader_timeout` (default 0 = no limit) for a reader
that a newer request of its stream left behind. See `HTTP_RANGE_SPEC.md`.

`ca_cert_file` is a PEM file with the CAs that HTTPS trackers and web seeds are
checked against, on top of OpenSSL's default locations. On Android it is
needed: the default locations do not exist there, and OpenSSL ignores
`SSL_CERT_FILE` in app processes, so without it every HTTPS tracker fails with
"certificate verify failed".

## Torrent Management

```c
// Add torrent from .torrent file path or magnet URI
// out_torrent_id: buffer for hex torrent ID (40 chars + null)
ss_error_t ss_add_torrent(SeekServeEngine* engine, const char* uri,
                           char* out_torrent_id, int32_t out_torrent_id_len);

// Remove torrent (optionally delete downloaded files)
ss_error_t ss_remove_torrent(SeekServeEngine* engine, const char* torrent_id,
                              bool delete_files);

// Pause / Resume torrent download and upload
ss_error_t ss_pause_torrent(SeekServeEngine* engine, const char* torrent_id);
ss_error_t ss_resume_torrent(SeekServeEngine* engine, const char* torrent_id);

// Force immediate reannounce to all trackers, bypassing libtorrent's exponential
// backoff. Useful when SOCKS5/Tor is active (DHT disabled) and the initial tracker
// announce fails because Tor circuits are not yet cached.
ss_error_t ss_force_reannounce(SeekServeEngine* engine, const char* torrent_id);

// List active torrent IDs (returns JSON array: ["id1","id2",...])
ss_error_t ss_list_torrents(SeekServeEngine* engine, char** out_json);
```

Torrents are automatically persisted to SQLite. On engine creation, previously added torrents are restored from the database.

## File Operations

```c
// List files in torrent (returns JSON: {"files":[{index,path,size,...}]})
ss_error_t ss_list_files(SeekServeEngine* engine, const char* torrent_id,
                          char** out_json);

// Select file for streaming (sets download priority)
ss_error_t ss_select_file(SeekServeEngine* engine, const char* torrent_id,
                           int32_t file_index);

// The two files the torrent wants from now on: the one a player reads and
// the one that downloads behind it (-1 for none)
ss_error_t ss_set_wanted_files(SeekServeEngine* engine, const char* torrent_id,
                                int32_t reader_file, int32_t background_file);

// Get HTTP stream URL for selected file
ss_error_t ss_get_stream_url(SeekServeEngine* engine, const char* torrent_id,
                              int32_t file_index, char** out_url);
```

`ss_select_file` asks for one file alone: every other file of the torrent stops, and the file gets a new stream each time. A pack that downloads one episode while a player reads another asks for both with `ss_set_wanted_files`: `reader_file` has the stream and the deadlines and comes first (its pieces are asked for before the other file's), `background_file` goes on behind it and takes over by itself once the reader's file is whole; `-1` is no file, for either or for both, and the same index twice is the reader. A player keeps its stream while only `background_file` changes; when `reader_file` changes or goes to `-1`, its stream ends and its file stops unless it is the new `background_file`. A torrent or a file that is not there is `SS_ERR_NOT_FOUND` and changes nothing; before the metadata, `SS_ERR_METADATA_PENDING`. After a restart with `resume_data` no file is a reader's: the one that was comes back unwanted when another downloaded behind it, and wanted as it was when it was alone.

## Status

```c
// Get torrent status as JSON
ss_error_t ss_get_status(SeekServeEngine* engine, const char* torrent_id,
                          char** out_json);
```

Status JSON includes: `torrent_id`, `name`, `progress`, `download_rate`, `upload_rate`, `num_peers`, `num_seeds`, `state`, `has_metadata`, `selected_file`, `offline_ready`, `files`, `stream_mode`, `playhead_piece`, `active_deadlines`.

`files` is there once the metadata is known: one entry for each file the torrent downloads (every file until one is selected, then the wanted files and the subtitle files), `{"index", "size", "downloaded", "complete"}`, with `"role"` on the files the engine was asked for: `"reader"` for the one a player reads, which has the stream and the deadlines, and `"background"` for the one that downloads behind it. `ss_select_file` asks for a reader alone, `ss_set_wanted_files` for the two together, and then the reader's file comes first. `selected_file` is the reader's file, or the background one when no player reads; `stream_mode`, `playhead_piece` and `active_deadlines` are there only with a reader. `downloaded` is the bytes of that file that are there, the blocks of unfinished pieces included; `complete` is true when every piece the file has a byte in is checked, which is when `file_completed` fires, and goes back to false if the pieces are lost (a file deleted behind the engine, seen at the next check). `progress` counts the wanted pieces of the whole torrent, which with two wanted files is neither file's own number, and `offline_ready` is a stored note that the selected file completed once: for one file, read its entry in `files`.

## Server Control

```c
// Start HTTP servers (port=0 for auto-assign)
ss_error_t ss_start_server(SeekServeEngine* engine, uint16_t port,
                            uint16_t* out_port);

// Stop HTTP servers, and every share
ss_error_t ss_stop_server(SeekServeEngine* engine);
```

### Shares

```c
// Serve the file a player reads to one device (a TV), on one address of a
// network: out_json = {"share_id": "...", "url": "http://<bind_address>:<port>/..."}
ss_error_t ss_open_share(SeekServeEngine* engine, const char* torrent_id,
                         int32_t file_index, const char* bind_address,
                         const char* peer_address, char** out_json);

// The same for a whole file on disk that is no torrent's (a complete
// download), inside save_path only
ss_error_t ss_open_file_share(SeekServeEngine* engine, const char* file_path,
                              const char* bind_address, const char* peer_address,
                              char** out_json);

// Close it and its connections
ss_error_t ss_close_share(SeekServeEngine* engine, const char* share_id);
```

A share is a stream server of its own (app WORK-37): it listens on `bind_address` only
(never `0.0.0.0` or `::`), serves `peer_address` only, and its URL has a random path and
token, never the torrent's id. The file must be the one a player reads
(`ss_select_file`, `ss_set_wanted_files`); the share follows it when it is selected again.
`ss_open_file_share` needs no torrent and no peer (app DEC-627): `file_path` must be an absolute
path to a regular file inside `save_path`, links and `..` resolved; a relative path, a path
outside, a folder are `SS_ERR_INVALID_ARG`, a path with nothing there `SS_ERR_NOT_FOUND`.
Errors of `ss_open_share`: `SS_ERR_INVALID_ARG` (missing argument, an address that is not one, a wildcard),
`SS_ERR_NOT_FOUND` (no such torrent, a file no player reads, no such share), `SS_ERR_IO`
(servers not started, an address that cannot be listened on). See `HTTP_RANGE_SPEC.md`.

## Event Callback

```c
typedef void (*ss_event_callback_t)(const char* event_json, void* user_data);

// Set async event callback (pass NULL to clear)
ss_error_t ss_set_event_callback(SeekServeEngine* engine,
                                  ss_event_callback_t callback, void* user_data);
```

Events are JSON: `{"type":"metadata_received","data":{"torrent_id":"..."}}`.

## Memory Management

```c
// Free strings allocated by the library (ss_list_torrents, ss_list_files, ss_get_stream_url, ss_get_status)
void ss_free_string(char* str);
```

**Important**: All `char**` out parameters return library-allocated strings. Callers **must** call `ss_free_string()` on these strings to avoid memory leaks. Passing `NULL` to `ss_free_string()` is safe (`delete[] nullptr` is a no-op per C++ standard).

## Error Codes

No call lets a C++ exception out of the library: a caller across the C boundary (JNI, Dart FFI) could not catch it. A torrent removed by another thread while a call was working on it is `SS_ERR_NOT_FOUND`, as if it had not been there; any other failure inside a call is `SS_ERR_IO`.

| Code | Value | Meaning |
|------|-------|---------|
| `SS_OK` | 0 | Success |
| `SS_ERR_INVALID_ARG` | -1 | Null pointer or invalid argument |
| `SS_ERR_NOT_FOUND` | -2 | Torrent or file not found |
| `SS_ERR_METADATA_PENDING` | -3 | Metadata not yet received |
| `SS_ERR_TIMEOUT` | -4 | Operation timed out |
| `SS_ERR_IO` | -5 | I/O error |
| `SS_ERR_ALREADY_RUNNING` | -6 | Server already running |
| `SS_ERR_CANCELLED` | -7 | Operation cancelled |

## Null Safety

All functions accepting `SeekServeEngine*` return `SS_ERR_INVALID_ARG` when passed NULL. `ss_engine_destroy(NULL)` is a safe no-op (`delete nullptr`). `ss_free_string(NULL)` is a safe no-op (`delete[] nullptr`).

## Thread Safety

The C API is thread-safe. All functions can be called from any thread. Event callbacks are invoked from internal threads — callers should not block in callbacks.
