# SeekServe Architecture

## Overview

SeekServe is a C++ SDK for torrent-based video streaming over HTTP. It downloads torrent content, serves it via an HTTP Range server on loopback, and provides intelligent piece scheduling for smooth playback. Supports both standard BitTorrent and WebTorrent (WebRTC) peers via libtorrent master + libdatachannel.

## Modules

```
SeekServeEngine (facade)
├── TorrentSessionManager     → owns lt::session, add/remove torrents
│   └── AlertDispatcher       → dedicated thread, pop_alerts() loop
├── MetadataCatalog           → torrent → file list, file selection
├── PieceAvailabilityIndex    → lock-free atomic bitfield per torrent
├── ByteRangeMapper           → HTTP byte ranges → piece spans
├── StreamingScheduler        → set_piece_deadline() for hot/lookahead/seek
├── ByteSource                → read(offset, len) with piece-wait blocking
├── OfflineCacheManager       → SQLite index (cache_entries + torrents), LRU quota, torrent URI persistence
├── HttpRangeServer           → Beast HTTP, /stream/{id}/{fileIndex}
└── ControlApiServer          → Beast HTTP, REST JSON API
```

## Libraries

| Library | Module | Purpose |
|---|---|---|
| `seekserve-core` | Static lib | Torrent engine, scheduling, availability, mapping, offline cache |
| `seekserve-serve` | Static lib | HTTP servers, engine facade |
| `seekserve-capi` | Shared lib (.so/.dylib) | C API for FFI (Flutter, etc.) |
| `seekserve-demo` | Executable | CLI demo |

## Dependencies

| Library | Source | Purpose |
|---|---|---|
| libtorrent | Git submodule (`extern/libtorrent`, master branch) | Torrent protocol, piece management |
| libdatachannel | Submodule of libtorrent | WebRTC data channels for WebTorrent |
| Boost (individual) | vcpkg | Asio, Beast, utility libs |
| SQLite3 | vcpkg | Offline cache database |
| spdlog | vcpkg | Logging |
| nlohmann_json | vcpkg | JSON serialization |
| OpenSSL | vcpkg (cross-compile) / Homebrew (macOS) | TLS for WebTorrent/libdatachannel |

## Thread Model

### 4 Thread Domains

1. **libtorrent internal** — network I/O, disk I/O. Never touched directly.
2. **AlertDispatcher thread** — Dedicated thread running `pop_alerts()` in a loop. Dispatches typed alerts to registered handlers. Handlers must not block.
3. **Boost.Asio io_context** — Runs HTTP servers (accept loops), tick timer. Thread-per-connection for long-running streams.
4. **Caller thread** — Creates engine, calls public API.

### Synchronization

| Component | Guard | Pattern |
|---|---|---|
| PieceAvailabilityIndex | `vector<atomic<bool>>` | Lock-free: pieces only go false→true |
| MetadataCatalog | `std::mutex` | Alert thread writes, user thread reads |
| StreamingScheduler | `std::mutex` | Alert + HTTP threads both access |
| ByteSource::read() | `condition_variable` | HTTP thread waits, alert thread signals |
| OfflineCacheManager | `std::mutex` | SQLite WAL mode for concurrent access |
| Engine states/events | `std::mutex` | Separate mutexes for states_ and event_cb_ |

## Data Flow

### Streaming Request

1. Player sends `GET /stream/{id}/{fi}?token=XXX` with `Range: bytes=X-Y`
2. HttpRangeServer parses range, looks up ByteSource
3. RangeCallback notifies StreamingScheduler → sets piece deadlines
4. ByteSource::read() maps bytes to pieces, waits for availability
5. PieceAvailabilityIndex checked (lock-free atomics)
6. If pieces available: read from disk, stream 64KB chunks
7. If not: condition_variable waits until piece_finished_alert fires

### Piece Completion

1. libtorrent downloads piece, fires `piece_finished_alert`
2. AlertDispatcher calls handler on its dedicated thread
3. Handler: `PieceAvailabilityIndex::mark_complete()` (atomic)
4. Handler: `ByteSource::notify_piece_complete()` (cv notify)
5. Handler: `StreamingScheduler::on_piece_complete()` (update state)

### Torrent Persistence

1. `add_torrent()` saves URI to SQLite `torrents` table
2. `remove_torrent()` deletes URI from `torrents` table before session removal
3. Engine constructor reads `torrents` table and re-adds each URI to session
4. Failed restores are cleaned up (stale entries removed from DB)

## Design Decisions

- **Thread-per-connection**: VLC sends concurrent requests during seek. Async Beast would block on ByteSource::read() anyway.
- **Lock-free availability**: Pieces only transition false→true (monotonic). Atomic compare-exchange is sufficient.
- **Direct disk reads**: Once piece_finished_alert fires, bytes are on disk. No need for read_piece() round-trip.
- **Separate io_context**: Engine owns its own io_context rather than sharing with libtorrent, for clean lifecycle.
- **removed_ids_ set**: Guards alert handlers against late alerts for already-removed torrents, avoiding race with session manager.
- **libtorrent master**: Required for WebTorrent support (WebRTC via libdatachannel). Uses ABI v100 with breaking API changes vs RC_2_0.
- **Backend-centralized persistence**: Torrent URIs persisted in C++ SQLite, not Flutter/Dart. Works for all consumers (Flutter, REST, CLI).
