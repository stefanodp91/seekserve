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
    "extra_trackers": ["udp://tracker.example.com:6969"]
}
```

All fields are optional. Unknown fields are ignored.

## Torrent Management

```c
// Add torrent from .torrent file path or magnet URI
// out_torrent_id: buffer for hex torrent ID (40 chars + null)
ss_error_t ss_add_torrent(SeekServeEngine* engine, const char* uri,
                           char* out_torrent_id, int32_t out_torrent_id_len);

// Remove torrent (optionally delete downloaded files)
ss_error_t ss_remove_torrent(SeekServeEngine* engine, const char* torrent_id,
                              bool delete_files);

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

// Get HTTP stream URL for selected file
ss_error_t ss_get_stream_url(SeekServeEngine* engine, const char* torrent_id,
                              int32_t file_index, char** out_url);
```

## Status

```c
// Get torrent status as JSON
ss_error_t ss_get_status(SeekServeEngine* engine, const char* torrent_id,
                          char** out_json);
```

Status JSON includes: `torrent_id`, `name`, `progress`, `download_rate`, `upload_rate`, `num_peers`, `num_seeds`, `state`, `has_metadata`, `selected_file`, `offline_ready`, `stream_mode`, `playhead_piece`, `active_deadlines`.

## Server Control

```c
// Start HTTP servers (port=0 for auto-assign)
ss_error_t ss_start_server(SeekServeEngine* engine, uint16_t port,
                            uint16_t* out_port);

// Stop HTTP servers
ss_error_t ss_stop_server(SeekServeEngine* engine);
```

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
