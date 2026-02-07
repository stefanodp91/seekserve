# SeekServe

**Edge-first torrent streaming SDK with HTTP Range support, intelligent piece scheduling, and offline caching.**

SeekServe is a C++17 library that downloads torrent content and serves it locally via an HTTP Range-compliant server on loopback. Video players like VLC can connect to it and play, seek, and scrub through torrent video as if it were a regular HTTP stream. An intelligent piece scheduler ensures the right pieces are downloaded first for instant playback and fast seek.

---

## Table of Contents

- [Features](#features)
- [Architecture Overview](#architecture-overview)
- [High-Level Data Flow](#high-level-data-flow)
- [Module Dependency Graph](#module-dependency-graph)
- [Thread Model](#thread-model)
- [Streaming Pipeline](#streaming-pipeline)
- [Piece Scheduling Strategy](#piece-scheduling-strategy)
- [Project Structure](#project-structure)
- [Build & Run](#build--run)
- [Usage](#usage)
- [Testing the System](#testing-the-system)
- [C API (FFI)](#c-api-ffi)
- [REST Control API](#rest-control-api)
- [Configuration](#configuration)
- [Testing](#testing)
- [Documentation Index](#documentation-index)
- [License](#license)

---

## Features

- **HTTP Range streaming** (RFC 7233) — 200 OK, 206 Partial Content, 416 Range Not Satisfiable
- **Intelligent piece scheduler** — hot window, lookahead, seek boost, adaptive mode switching
- **Multi-file torrent support** — enumerate files, select one for streaming, correct per-file byte mapping
- **Offline cache** — SQLite-backed, LRU eviction, quota enforcement, offline-ready detection
- **C API** — opaque handle, JSON data exchange, callback events — ready for Flutter FFI / Dart `ffigen`
- **REST control API** — add/remove torrents, list files, select streams, monitor status via JSON
- **Hardened servers** — connection limits, socket timeouts, body size limits, token auth, CORS
- **Loopback-only binding** — secure by default, no network exposure
- **Cross-platform** — macOS, Linux; cross-compile scripts for iOS (arm64) and Android (arm64-v8a)
- **Sanitizer support** — ASan and TSan build presets out of the box

---

## Architecture Overview

```
+------------------------------------------------------------------+
|                        SeekServeEngine                            |
|                       (facade / entry point)                      |
+------------------------------------------------------------------+
|                                                                    |
|  +---------------------------+    +----------------------------+  |
|  |  TorrentSessionManager    |    |     MetadataCatalog        |  |
|  |  - lt::session lifecycle  |    |  - torrent -> file list    |  |
|  |  - add/remove torrents    |    |  - file selection          |  |
|  |  - handle registry        |    |  - priority management     |  |
|  |                           |    +----------------------------+  |
|  |  +---------------------+  |                                    |
|  |  |  AlertDispatcher    |  |    +----------------------------+  |
|  |  |  - dedicated thread |  |    |  PieceAvailabilityIndex    |  |
|  |  |  - pop_alerts loop  |  |    |  - vector<atomic<bool>>    |  |
|  |  |  - typed handlers   |  |    |  - lock-free, monotonic    |  |
|  |  +---------------------+  |    |  - contiguous byte calc    |  |
|  +---------------------------+    +----------------------------+  |
|                                                                    |
|  +---------------------------+    +----------------------------+  |
|  |   ByteRangeMapper        |    |  StreamingScheduler        |  |
|  |  - bytes -> piece spans  |    |  - set_piece_deadline()    |  |
|  |  - file_storage::map()   |    |  - hot/lookahead/seek      |  |
|  |  - per-file offset aware |    |  - 3 adaptive modes        |  |
|  +---------------------------+    +----------------------------+  |
|                                                                    |
|  +---------------------------+    +----------------------------+  |
|  |     ByteSource            |    |  OfflineCacheManager       |  |
|  |  - read(offset, len)     |    |  - SQLite WAL index        |  |
|  |  - cv wait for pieces    |    |  - LRU eviction            |  |
|  |  - direct disk read      |    |  - quota enforcement       |  |
|  |  - cancel() for seek     |    |  - offline-ready tracking  |  |
|  +---------------------------+    +----------------------------+  |
|                                                                    |
|  +---------------------------+    +----------------------------+  |
|  |   HttpRangeServer         |    |  ControlApiServer          |  |
|  |  - GET /stream/{id}/{fi} |    |  - REST JSON endpoints     |  |
|  |  - 206 Partial Content   |    |  - POST/GET/DELETE routes  |  |
|  |  - 64KB streaming chunks |    |  - CORS preflight          |  |
|  |  - token auth            |    |  - Bearer + query auth     |  |
|  |  - connection limits     |    |  - body size limits        |  |
|  +---------------------------+    +----------------------------+  |
|                                                                    |
+------------------------------------------------------------------+
          |                                          |
          v                                          v
   +-------------+                           +--------------+
   | VLC / Player |                          | curl / UI    |
   | HTTP Range   |                          | REST JSON    |
   +-------------+                           +--------------+
```

---

## High-Level Data Flow

```
  .torrent file                        Video Player (VLC)
  or magnet URI                              |
       |                                     | GET /stream/{id}/{fi}
       v                                     | Range: bytes=X-Y
+------+-------+                             |
| add_torrent() |                            v
+------+-------+              +-----------------------------+
       |                      |       HttpRangeServer       |
       v                      |  parse range, find source   |
+------+---------+            +---------+-------------------+
| lt::session    |                      |
| (BitTorrent)   |                      | notify scheduler
|  downloads     |                      v
|  pieces        |            +---------+-------------------+
+------+---------+            |    StreamingScheduler       |
       |                      |  set_piece_deadline() on    |
       | piece_finished       |  hot window + lookahead     |
       | alert                +---------+-------------------+
       v                                |
+------+---------+                      |
| AlertDispatcher|                      v
|  marks piece   |            +---------+-------------------+
|  complete in   |            |       ByteSource            |
|  availability  |  signals   |  read(offset, len)          |
|  index         +----------->|  waits for pieces if needed |
|  notifies      |    cv      |  reads from disk            |
|  ByteSource    |            +---------+-------------------+
+----------------+                      |
                                        | 64KB chunks
                                        v
                              +---------+-------------------+
                              |   TCP socket (loopback)     |
                              |   -> VLC renders video      |
                              +-----------------------------+
```

---

## Module Dependency Graph

```
                    seekserve-capi  (shared lib, C API)
                         |
                         | links
                         v
                    seekserve-serve (static lib)
                    /          \
                   /            \
                  v              v
          seekserve-core    Boost.Beast
          (static lib)      Boost.Asio
               |
               v
          libtorrent (submodule)
               |
               v
          Boost, OpenSSL
```

Build targets:

```
seekserve-core     -->  libseekserve-core.a
seekserve-serve    -->  libseekserve-serve.a    (depends on core)
seekserve-capi     -->  libseekserve.dylib/.so  (depends on serve)
seekserve-demo     -->  seekserve-demo           (depends on serve)
seekserve-*-tests  -->  test executables          (depends on serve + capi)
```

---

## Thread Model

```
+-------------------------------------------------------------------+
|                         THREAD DOMAINS                             |
+-------------------------------------------------------------------+
|                                                                     |
|  [1] libtorrent internal          (network I/O, disk I/O)          |
|      - never touched directly                                       |
|      - managed by lt::session                                       |
|                                                                     |
|  [2] AlertDispatcher              (dedicated thread)                |
|      - pop_alerts() loop, 100ms poll                                |
|      - dispatches typed handlers                                    |
|      - handlers must NOT block                                      |
|      +                                                              |
|      |  fires:                                                      |
|      |    add_torrent_alert    -> MetadataCatalog                   |
|      |    metadata_received    -> MetadataCatalog + Cache           |
|      |    piece_finished       -> Availability + ByteSource + Sched |
|      |    file_completed       -> OfflineCache                      |
|                                                                     |
|  [3] Boost.Asio io_context        (1 thread)                       |
|      - HTTP accept loops (async)                                    |
|      - tick timer (1s periodic)                                     |
|      - thread-per-connection for streams (detached std::thread)     |
|                                                                     |
|  [4] Caller thread                (user's thread)                   |
|      - creates SeekServeEngine                                      |
|      - calls add_torrent, select_file, start_server, etc.          |
|                                                                     |
+-------------------------------------------------------------------+

Synchronization:

  PieceAvailabilityIndex   vector<atomic<bool>>    lock-free (monotonic)
  MetadataCatalog          std::mutex              alert writes, user reads
  StreamingScheduler       std::mutex              alert + HTTP threads
  ByteSource::read()       condition_variable      HTTP waits, alert signals
  OfflineCacheManager      std::mutex              SQLite WAL underneath
  Engine states/events     std::mutex (x2)         separate for states_, event_cb_
```

---

## Streaming Pipeline

When VLC sends an HTTP Range request, the following pipeline executes:

```
  VLC Request: GET /stream/abc123/8?token=XXX  Range: bytes=1000000-1999999
         |
         v
  [1] HttpRangeServer::handle_connection()
         |
         +-- parse_target() -> torrent_id=abc123, file_index=8
         +-- constant_time_compare(token, expected)
         +-- parse_range_header("bytes=1000000-1999999", file_size)
         |
         v
  [2] RangeCallback -> StreamingScheduler::on_range_request()
         |
         +-- ByteRangeMapper::map({1000000, 1999999}) -> PieceSpan{p42..p46}
         +-- set_piece_deadline(p42, 100ms)  // hot window
         +-- set_piece_deadline(p43, 150ms)
         +-- set_piece_deadline(p44, 200ms)
         +-- set_piece_deadline(p45..p65, 2000ms+)  // lookahead
         |
         v
  [3] ByteSource::read(offset=1000000, len=65536)  // 64KB chunk
         |
         +-- ByteRangeMapper::map({1000000, 1065535}) -> PieceSpan
         +-- PieceAvailabilityIndex::is_span_complete() ?
         |     |
         |     +-- YES -> read from disk, return data
         |     +-- NO  -> condition_variable::wait(30s timeout)
         |                  (alert thread signals on piece_finished)
         |
         v
  [4] net::write(socket, data)  // raw Asio write, 64KB
         |
         +-- repeat [3]-[4] until range fully served
         |
         v
  [5] Connection complete (RAII ConnGuard decrements active_connections_)
```

---

## Piece Scheduling Strategy

```
  File on disk (linear byte order):
  [===============================================================]
   ^                                                              ^
   byte 0                                                    file_size

  Piece layout mapped from byte range:
  [p0][p1][p2][p3][p4][p5][p6][p7][p8][p9][p10][p11]...[pN]

  When playhead is at piece p4:

  Deadline zones:
  [  bg  ][  bg  ][ bg  ][ bg ][HOT ][HOT ][HOT ][LOOK][LOOK][LOOK][LOOK][ bg ]
   p0      p1      p2     p3    p4    p5    p6    p7    p8    p9    p10    p11
                                 ^
                             playhead
                           deadline: 100ms ------>  2000ms+ -------->
                           |<- hot_window ->|<----- lookahead ----->|

  On SEEK (playhead jumps from p4 to p30):

  [  bg  ]...[bg][BOOST][BOOST][HOT ][HOT ][HOT ][LOOK][LOOK]...[LOOK][ bg ]
                  p28    p29    p30   p31   p32   p33   p34       p50
                  |<-- seek_boost -->|<- hot_window ->|<-- lookahead -->|
                  extra pieces for 3 seconds

  Mode switching (evaluated every 1s tick):

    STREAMING_FIRST  --[stalls > 3]-->  DOWNLOAD_ASSIST
    DOWNLOAD_ASSIST  --[rate < 500KB/s]-->  DOWNLOAD_FIRST
    DOWNLOAD_FIRST   --[buffer >= 2MB && rate OK]-->  STREAMING_FIRST
```

---

## Project Structure

```
seekserve/
|
|-- CMakeLists.txt              Root build: project options, subdirs, libtorrent
|-- CMakePresets.json           Build presets: debug, release, asan, tsan
|-- vcpkg.json                  Dependency manifest (Boost, spdlog, sqlite3, etc.)
|-- setup.sh                    One-command bootstrap: vcpkg + submodules + build
|-- .gitignore                  Build artifacts, SQLite DBs, IDE files
|
|-- extern/
|   +-- libtorrent/             Git submodule (arvidn/libtorrent@master)
|
|-- fixtures/
|   +-- Sintel_archive.torrent  Test torrent (CC BY 3.0, 12 files, 4 MP4s)
|
|-- seekserve-core/             STATIC LIB: torrent engine + scheduling
|   |-- CMakeLists.txt
|   |-- include/seekserve/
|   |   |-- types.hpp               TorrentId, FileIndex, PieceSpan, ByteRange, FileInfo
|   |   |-- error.hpp               errc enum, Result<T> (variant-based), error category
|   |   |-- config.hpp              SessionConfig, SchedulerConfig, ServerConfig, CacheConfig
|   |   |-- session_manager.hpp     lt::session lifecycle, add/remove, handle registry
|   |   |-- alert_dispatcher.hpp    Dedicated thread, pop_alerts(), typed handler registry
|   |   |-- metadata_catalog.hpp    Torrent -> file enumeration, file selection, priority
|   |   |-- byte_range_mapper.hpp   HTTP byte ranges -> PieceSpan via file_storage::map_file()
|   |   |-- piece_availability.hpp  Lock-free atomic bitfield, contiguous byte calculation
|   |   |-- byte_source.hpp         read(offset, len) with cv wait, disk read, cancel
|   |   |-- streaming_scheduler.hpp Hot window, lookahead, seek boost, mode switching
|   |   +-- offline_cache.hpp       SQLite index, LRU eviction, quota, offline-ready
|   +-- src/
|       +-- *.cpp                   Implementations for all core modules
|
|-- seekserve-serve/            STATIC LIB: HTTP servers + engine facade
|   |-- CMakeLists.txt
|   |-- include/seekserve/
|   |   |-- engine.hpp              SeekServeEngine facade (single entry point)
|   |   |-- http_range_server.hpp   Beast HTTP, /stream/{id}/{fi}, Range/206
|   |   |-- control_api_server.hpp  Beast HTTP, REST JSON API, CORS
|   |   +-- range_parser.hpp        RFC 7233 Range header parser
|   +-- src/
|       |-- engine.cpp              Facade wiring: alerts -> modules, tick timer
|       |-- http_range_server.cpp   Streaming, auth, connection limits, timeouts
|       |-- control_api_server.cpp  REST endpoints, auth, body limits
|       |-- range_parser.cpp        bytes=start-end, bytes=start-, bytes=-suffix
|       +-- token_auth.cpp          Token generation + constant-time validation
|
|-- seekserve-capi/             SHARED LIB: C API for FFI (Flutter, Swift, etc.)
|   |-- CMakeLists.txt
|   |-- include/
|   |   +-- seekserve_c.h          Opaque handle, extern "C", error codes, callbacks
|   +-- src/
|       +-- seekserve_c.cpp         Bridge: JSON config, map_error, alloc_string
|
|-- seekserve-demo/             EXECUTABLE: CLI demo
|   |-- CMakeLists.txt
|   +-- src/
|       +-- main.cpp                Add torrent, list files, select, stream URL
|
|-- tests/
|   |-- CMakeLists.txt              3 test targets: unit, integration, capi
|   |-- unit/
|   |   |-- test_byte_range_mapper.cpp     Byte-to-piece mapping, cross-piece, multi-file
|   |   |-- test_piece_availability.cpp    Atomic bitfield, contiguous, progress, reset
|   |   |-- test_metadata_catalog.cpp      File enumeration, selection, priorities
|   |   |-- test_byte_source.cpp           Read, wait, cancel, timeout
|   |   |-- test_streaming_scheduler.cpp   Deadlines, seek boost, mode switching
|   |   |-- test_range_parser.cpp          RFC 7233 parsing (20+ cases)
|   |   |-- test_offline_cache.cpp         SQLite CRUD, LRU eviction, quota
|   |   +-- test_capi.cpp                  C API lifecycle, null guards, full lifecycle
|   +-- integration/
|       |-- test_session_lifecycle.cpp     lt::session + metadata + file selection
|       |-- test_http_range_server.cpp     HTTP GET/HEAD, 200/206/403/404/416
|       |-- test_end_to_end_stream.cpp     Control API + torrent + file selection
|       +-- test_stress.cpp                Concurrent connections, rapid seek, limits
|
|-- docs/                       Specification & design documents
|   |-- ARCHITECTURE.md            Module overview, data flow, thread model
|   |-- HTTP_RANGE_SPEC.md         Endpoint, range handling, response codes
|   |-- SCHEDULER_POLICY.md        Hot/lookahead/seek boost, modes, parameters
|   |-- STORAGE_POLICY.md          Piece storage, offline cache, SQLite schema
|   |-- SECURITY.md                Loopback, auth, limits, logging safety
|   +-- C_API.md                   Function reference, error codes, memory mgmt
|
|-- scripts/                    Cross-compilation
|   |-- build-ios.sh               iOS arm64 via vcpkg arm64-ios triplet
|   +-- build-android.sh           Android arm64-v8a via NDK toolchain
|
+-- PROGRESS.md                 Milestone checklist (M1-M8 complete)
```

---

## Build & Run

### Prerequisites

- CMake >= 3.21
- Ninja
- C++17 compiler (Clang or GCC)
- Git (for submodules + vcpkg)

### One-Command Setup

```bash
./setup.sh debug       # Full bootstrap: vcpkg + submodules + cmake + build
```

This will:
1. Install missing tools via Homebrew (macOS) or check availability (Linux)
2. Clone and bootstrap vcpkg at `~/vcpkg`
3. Initialize the libtorrent git submodule
4. Configure CMake with vcpkg toolchain
5. Build all targets

### Build Variants

```bash
./setup.sh debug       # Debug build (default)
./setup.sh release     # Optimized release build
./setup.sh asan        # Debug + AddressSanitizer
./setup.sh tsan        # Debug + ThreadSanitizer
```

### Manual CMake

```bash
cmake --preset debug
cmake --build --preset debug
```

### Run Demo

```bash
./build/debug/seekserve-demo/seekserve-demo fixtures/Sintel_archive.torrent
```

The demo will:
1. Load the torrent and display all files
2. Auto-select the smallest MP4
3. Print a stream URL you can paste into VLC

---

## Usage

### Typical Workflow

```
1. Create engine        ->  ss_engine_create(config_json)
2. Start HTTP servers   ->  ss_start_server(engine, 0, &port)
3. Add torrent          ->  ss_add_torrent(engine, "path.torrent", id_buf, 128)
4. Wait for metadata    ->  (event callback fires "metadata_received")
5. List files           ->  ss_list_files(engine, id, &json)
6. Select file          ->  ss_select_file(engine, id, file_index)
7. Get stream URL       ->  ss_get_stream_url(engine, id, file_index, &url)
8. Open in VLC          ->  vlc <url>
9. Cleanup              ->  ss_engine_destroy(engine)
```

### VLC Example

```bash
# Start the demo
./build/debug/seekserve-demo/seekserve-demo fixtures/Sintel_archive.torrent

# Output:
#   Stream URL: http://127.0.0.1:54321/stream/e4d37e62.../8?token=abc123
#
# Open in VLC:
vlc "http://127.0.0.1:54321/stream/e4d37e62.../8?token=abc123"
```

VLC will:
- Send `HEAD` to discover file size and `Accept-Ranges: bytes`
- Send `Range: bytes=0-` to start playback
- Send seek requests (`Range: bytes=N-`) when scrubbing

---

## Testing the System

### Test Torrent: Sintel (CC BY 3.0)

SeekServe ships with a reference torrent for end-to-end testing:

```
fixtures/Sintel_archive.torrent
```

**Sintel** is an open-source short film by the [Blender Foundation](https://durian.blender.org/), released under the [Creative Commons Attribution 3.0](https://creativecommons.org/licenses/by/3.0/) license. This makes it freely distributable and ideal for development and testing of streaming applications.

| Property | Value |
|----------|-------|
| Title | *Sintel* (2010) |
| Author | Blender Foundation / Durian Open Movie Project |
| License | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| Source | [archive.org/details/Sintel](https://archive.org/details/Sintel) |
| Infohash | `e4d37e62d14ba96d29b9e760148803b458aee5b6` |
| Files | 12 total (4 MP4, 1 MKV, 1 OGV, 1 AVI, subtitles, poster) |
| Test target | Index 8 — `sintel-2048-stereo_512kb.mp4` (73.8 MB) |

### Torrent File Contents

```
Index  Size       File
─────  ─────────  ──────────────────────────────────────────
  0       483 B   Sintel.de.srt
  1       549 B   Sintel.en.srt
  2       471 B   Sintel.es.srt
  3       487 B   Sintel.fr.srt
  4       459 B   Sintel.it.srt
  5       466 B   Sintel.nl.srt
  6       466 B   Sintel.pl.srt
  7       509 B   Sintel.pt.srt
  8    73.8 MB    sintel-2048-stereo_512kb.mp4    <-- test target
  9   296.1 MB    Sintel (Surround).mp4
 10   269.3 MB    Sintel.2010.720p.mkv
 11   129.5 KB    poster.jpg
```

The 512kb MP4 (index 8) is used as the primary test target because it is small enough for fast downloads while still being a fully playable H.264 video.

### Quick Functional Test

**1. Build and run the demo:**

```bash
./setup.sh debug
./build/debug/seekserve-demo/seekserve-demo fixtures/Sintel_archive.torrent
```

The demo will:
- Load the torrent and display all 12 files
- Auto-select index 8 (`sintel-2048-stereo_512kb.mp4`)
- Start the HTTP Range server on a random loopback port
- Print a stream URL with an auth token

**2. Open in VLC:**

```bash
# Copy the URL from the demo output, e.g.:
vlc "http://127.0.0.1:54321/stream/e4d37e62d14ba96d29b9e760148803b458aee5b6/8?token=abc123"
```

**3. Verify streaming behavior:**

```
+-- Playback starts within seconds (streaming-first mode)
+-- Seek forward/backward responds within 1-2s (seek boost activates)
+-- No full-file download required before playback begins
+-- Piece scheduler prioritizes hot window around playhead
```

### Manual HTTP Verification

You can also test the HTTP Range server directly with `curl`:

```bash
# HEAD request — verify Accept-Ranges and Content-Length
curl -I "http://127.0.0.1:PORT/stream/TORRENT_ID/8?token=TOKEN"

# Expected:
#   HTTP/1.1 200 OK
#   Accept-Ranges: bytes
#   Content-Length: 77395579
#   Content-Type: video/mp4

# Range request — first 1KB
curl -r 0-1023 -o /dev/null -w "%{http_code}" \
  "http://127.0.0.1:PORT/stream/TORRENT_ID/8?token=TOKEN"

# Expected: 206

# Invalid token — expect 403
curl -I "http://127.0.0.1:PORT/stream/TORRENT_ID/8?token=wrong"

# Expected: 403 Forbidden
```

### Control API Verification

```bash
TOKEN="your_auth_token"
BASE="http://127.0.0.1:CONTROL_PORT"

# List active torrents
curl -H "Authorization: Bearer $TOKEN" "$BASE/api/torrents"

# List files in a torrent
curl -H "Authorization: Bearer $TOKEN" "$BASE/api/torrents/TORRENT_ID/files"

# Get torrent status (download rate, peers, progress, scheduler mode)
curl -H "Authorization: Bearer $TOKEN" "$BASE/api/torrents/TORRENT_ID/status"

# Get stream URL for file index 8
curl -H "Authorization: Bearer $TOKEN" "$BASE/api/torrents/TORRENT_ID/stream-url"
```

### Automated Test Suite

```bash
# Run all 189 tests (no network required)
./build/debug/tests/seekserve-unit-tests          # 138 unit tests
./build/debug/tests/seekserve-integration-tests    # 32 integration tests
./build/debug/tests/seekserve-capi-tests           # 19 C API tests
```

Unit and integration tests use synthetic in-memory torrents — no actual BitTorrent network traffic is generated. The Sintel `.torrent` file is loaded for metadata parsing tests only (verifying file enumeration, piece mapping, and range calculations against known values).

### Sanitizer Verification

```bash
# AddressSanitizer — detects memory errors (use-after-free, buffer overflow, leaks)
./setup.sh asan
./build/asan/tests/seekserve-unit-tests
./build/asan/tests/seekserve-integration-tests
./build/asan/tests/seekserve-capi-tests

# ThreadSanitizer — detects data races and deadlocks
./setup.sh tsan
./build/tsan/tests/seekserve-unit-tests
./build/tsan/tests/seekserve-integration-tests
./build/tsan/tests/seekserve-capi-tests
```

### Stress Tests

The integration suite includes stress tests that verify server robustness:

```
StressTest.ConcurrentHttpConnections   20 threads sending Range requests simultaneously
StressTest.RapidSeekSimulation         50 sequential random-offset requests (simulates VLC scrubbing)
StressTest.ConnectionLimitEnforced     Verifies max_concurrent_streams rejects excess connections
```

### Offline Playback Test

```bash
# 1. Start demo, let the full file download (progress reaches 100%)
# 2. Disconnect from network (disable Wi-Fi / unplug ethernet)
# 3. The stream URL continues to work — ByteSource reads directly from disk
# 4. VLC can still play and seek through the complete file
```

---

## C API (FFI)

The C API (`seekserve_c.h`) provides a stable, FFI-friendly interface:

```c
// Lifecycle
SeekServeEngine* ss_engine_create(const char* config_json);
void             ss_engine_destroy(SeekServeEngine* engine);

// Torrents
ss_error_t ss_add_torrent(engine, uri, out_id, id_len);
ss_error_t ss_remove_torrent(engine, torrent_id, delete_files);

// Files
ss_error_t ss_list_files(engine, torrent_id, &out_json);
ss_error_t ss_select_file(engine, torrent_id, file_index);
ss_error_t ss_get_stream_url(engine, torrent_id, file_index, &out_url);
ss_error_t ss_get_status(engine, torrent_id, &out_json);

// Server
ss_error_t ss_start_server(engine, port, &out_port);
ss_error_t ss_stop_server(engine);

// Events
ss_error_t ss_set_event_callback(engine, callback_fn, user_data);

// Memory
void ss_free_string(char* str);  // MUST call on all library-allocated strings
```

Error codes: `SS_OK` (0), `SS_ERR_INVALID_ARG`, `SS_ERR_NOT_FOUND`, `SS_ERR_METADATA_PENDING`, `SS_ERR_TIMEOUT`, `SS_ERR_IO`, `SS_ERR_ALREADY_RUNNING`, `SS_ERR_CANCELLED`.

See [docs/C_API.md](docs/C_API.md) for the full reference.

---

## REST Control API

The Control API server provides JSON endpoints for remote control:

```
POST   /api/torrents                      Add torrent (body: {"uri":"..."})
GET    /api/torrents                      List all torrents
GET    /api/torrents/{id}/files           List files in torrent
POST   /api/torrents/{id}/files/{fi}/select   Select file for streaming
GET    /api/torrents/{id}/status          Torrent status + metrics
GET    /api/torrents/{id}/stream-url      Get stream URL for selected file
DELETE /api/torrents/{id}                 Remove torrent
GET    /api/cache                         List cached files
POST   /api/server/stop                   Graceful shutdown
```

Authentication: `Authorization: Bearer <token>` header or `?token=<token>` query parameter.

All responses include `Access-Control-Allow-Origin: *` for CORS.

---

## Configuration

Engine configuration is passed as JSON to `ss_engine_create()`:

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
    "extra_trackers": [
        "udp://tracker.opentrackr.org:1337/announce"
    ]
}
```

| Field | Default | Description |
|-------|---------|-------------|
| `save_path` | `./downloads` | Download directory |
| `auth_token` | (empty) | Auth token for HTTP servers |
| `stream_port` | `0` (auto) | HTTP Range server port |
| `control_port` | `0` (auto) | Control API server port |
| `max_storage_bytes` | `0` (unlimited) | Offline cache quota |
| `cache_db_path` | `seekserve_cache.db` | SQLite database path |
| `log_level` | `info` | `debug`, `info`, `warn`, `error` |
| `enable_webtorrent` | `false` | WebRTC-based peers (experimental) |
| `extra_trackers` | `[]` | Additional tracker URLs |

---

## Testing

### Run All Tests

```bash
# Unit tests (138 tests, no network)
./build/debug/tests/seekserve-unit-tests

# Integration tests (32 tests, local only)
./build/debug/tests/seekserve-integration-tests

# C API tests (19 tests)
./build/debug/tests/seekserve-capi-tests
```

### Test Breakdown

```
189 tests total
|
+-- Unit (138)
|   +-- ByteRangeMapper       Byte-to-piece mapping, cross-boundary, multi-file offset
|   +-- PieceAvailability     Atomic bitfield, contiguous calc, progress, reset
|   +-- MetadataCatalog       File enumeration, selection, priorities, Sintel torrent
|   +-- ByteSource            Read available/unavailable, wait, cancel, timeout
|   +-- StreamingScheduler    Deadlines, seek boost, budget, mode switching
|   +-- RangeParser           20+ cases: closed, open, suffix, malformed, edge cases
|   +-- OfflineCache          CRUD, LRU eviction, quota, persistence, Sintel files
|   +-- C API                 Lifecycle, null guards, buffer handling, full lifecycle
|
+-- Integration (32)
|   +-- SessionLifecycle      Real lt::session, metadata, file selection
|   +-- HttpRangeServer       HTTP GET/HEAD, 200/206/403/404/416, data verification
|   +-- EndToEndStream        Control API + torrent + file selection
|   +-- Stress                20-thread concurrency, 50-request seek, connection limits
|
+-- C API (19)
    +-- Engine lifecycle, add/remove torrent, list files, server start/stop
```

### Sanitizer Builds

```bash
./setup.sh asan    # Build with AddressSanitizer
./build/asan/tests/seekserve-unit-tests

./setup.sh tsan    # Build with ThreadSanitizer
./build/tsan/tests/seekserve-unit-tests
```

---

## Documentation Index

| Document | Description |
|----------|-------------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Module overview, data flow diagrams, thread model, design decisions |
| [docs/HTTP_RANGE_SPEC.md](docs/HTTP_RANGE_SPEC.md) | HTTP endpoint format, Range handling (RFC 7233), response codes, MIME types, streaming behavior |
| [docs/SCHEDULER_POLICY.md](docs/SCHEDULER_POLICY.md) | Hot window, lookahead, seek boost, deadline budget, adaptive mode switching, configuration |
| [docs/STORAGE_POLICY.md](docs/STORAGE_POLICY.md) | Piece storage, file selection, SQLite cache schema, LRU eviction, quota, offline playback |
| [docs/SECURITY.md](docs/SECURITY.md) | Loopback binding, token auth, connection limits, request limits, logging safety, thread safety |
| [docs/C_API.md](docs/C_API.md) | Complete C function reference, error codes, config JSON, memory management, thread safety |

---

## Dependencies

| Dependency | Source | Purpose |
|------------|--------|---------|
| [libtorrent](https://github.com/arvidn/libtorrent) | Git submodule (master) | BitTorrent protocol, piece management |
| Boost (Asio, Beast, System) | vcpkg | HTTP server, async I/O |
| [nlohmann/json](https://github.com/nlohmann/json) | vcpkg | JSON serialization |
| [spdlog](https://github.com/gabime/spdlog) | vcpkg | Structured logging |
| [SQLite3](https://www.sqlite.org/) | vcpkg | Offline cache persistence |
| [Google Test](https://github.com/google/googletest) | vcpkg | Unit and integration testing |

---

## Cross-Compilation

### iOS (arm64)

```bash
./scripts/build-ios.sh
# Output: build/ios-arm64/seekserve-capi/libseekserve.dylib
```

### Android (arm64-v8a)

```bash
export ANDROID_NDK_HOME=/path/to/ndk
./scripts/build-android.sh
# Output: build/android-arm64/seekserve-capi/libseekserve.so
```

Both scripts build only the C API shared library (no tests, no demo).

---

## Roadmap

- **Phase 1 (Complete)**: C++ SDK with all 8 milestones (M1-M8)
- **Phase 2 (Planned)**: Flutter plugin via FFI (`dart:ffi` + `ffigen`)
  - M9: Flutter plugin structure
  - M10: iOS integration (Podspec, static framework)
  - M11: Android integration (NDK, Gradle)
  - M12: Dart API layer (Streams, async/await)
  - M13: WebTorrent support (libdatachannel)

---

## License

SeekServe is proprietary software. All rights reserved.

Test torrent (`fixtures/Sintel_archive.torrent`) is [Sintel](https://durian.blender.org/) by the Blender Foundation, licensed under [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).
