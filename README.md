# SeekServe

**Edge-first torrent streaming SDK with HTTP Range support, intelligent piece scheduling, offline caching, WebTorrent, and a Flutter plugin.**

SeekServe is a C++17 library that downloads torrent content and serves it locally via an HTTP Range-compliant server on loopback. Video players like VLC can connect to it and play, seek, and scrub through torrent video as if it were a regular HTTP stream. An intelligent piece scheduler ensures the right pieces are downloaded first for instant playback and fast seek.

The SDK includes a Flutter plugin (`flutter_seekserve`) with FFI bindings, pre-built native libraries for iOS and Android, a high-level Dart API with async events, and an example app with video playback.

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
- [Flutter Plugin](#flutter-plugin)
- [Usage](#usage)
- [Testing the System](#testing-the-system)
- [C API (FFI)](#c-api-ffi)
- [REST Control API](#rest-control-api)
- [Configuration](#configuration)
- [Testing](#testing)
- [Cross-Compilation](#cross-compilation)
- [WebTorrent](#webtorrent)
- [Documentation Index](#documentation-index)
- [Dependencies](#dependencies)
- [Milestone Status](#milestone-status)
- [License](#license)

---

## Features

### C++ SDK (Phase 1)
- **HTTP Range streaming** (RFC 7233) — 200 OK, 206 Partial Content, 416 Range Not Satisfiable
- **Intelligent piece scheduler** — hot window, lookahead, seek boost, adaptive mode switching
- **Multi-file torrent support** — enumerate files, select one for streaming, correct per-file byte mapping
- **Offline cache** — SQLite-backed, LRU eviction, quota enforcement, offline-ready detection
- **C API** — opaque handle, JSON data exchange, callback events — ready for Flutter FFI / Dart `ffigen`
- **REST control API** — add/remove torrents, list files, select streams, monitor status via JSON
- **Hardened servers** — connection limits, socket timeouts, body size limits, token auth, CORS
- **Loopback-only binding** — secure by default, no network exposure
- **Sanitizer support** — ASan and TSan build presets out of the box
- **189 tests** — 138 unit + 32 integration + 19 C API

### Flutter Plugin (Phase 2)
- **FFI bindings** — auto-generated via `ffigen` from `seekserve_c.h`
- **Native builds** — pre-built XCFramework (iOS) and `.so` (Android arm64-v8a, armeabi-v7a, x86_64)
- **Dart API** — `SeekServeClient` with async methods, `Stream<SeekServeEvent>` for real-time events
- **Event system** — `NativeCallable.listener` bridges C callbacks to Dart isolate
- **Example app** — torrent input, file selection, `media_kit` video player, live status display
- **14 Dart unit tests** for model classes

### WebTorrent (M13)
- **WebRTC peer connections** via libdatachannel (libtorrent master branch)
- **STUN server** configured automatically (`stun.l.google.com:19302`)
- **WebTorrent trackers** supported via `extra_trackers` config (e.g. `wss://tracker.webtorrent.dev`)
- **Compile-time toggle** — `SEEKSERVE_ENABLE_WEBTORRENT=ON/OFF`, graceful fallback to standard BitTorrent

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
          ^
          |
   +------+----------+
   | Flutter Plugin   |
   | (dart:ffi)       |
   | SeekServeClient  |
   | media_kit player |
   +-----------------+
```

---

## High-Level Data Flow

```
  .torrent file                        Video Player (VLC / media_kit)
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
| (BitTorrent    |                      | notify scheduler
|  + WebTorrent) |                      v
|  downloads     |            +---------+-------------------+
|  pieces        |            |    StreamingScheduler       |
+------+---------+            |  set_piece_deadline() on    |
       |                      |  hot window + lookahead     |
       | piece_finished       +---------+-------------------+
       | alert                          |
       v                                v
+------+---------+            +---------+-------------------+
| AlertDispatcher|            |       ByteSource            |
|  marks piece   |  signals   |  read(offset, len)          |
|  complete in   +----------->|  waits for pieces if needed |
|  availability  |    cv      |  reads from disk            |
|  index         |            +---------+-------------------+
+----------------+                      |
                                        | 64KB chunks
                                        v
                              +---------+-------------------+
                              |   TCP socket (loopback)     |
                              |   -> Player renders video   |
                              +-----------------------------+
```

---

## Module Dependency Graph

```
              Flutter App (Dart)
                    |
                    | dart:ffi
                    v
              flutter_seekserve (plugin)
                    |
                    | NativeCallable.listener
                    v
              seekserve-capi  (shared/static lib, C API)
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
    libtorrent (submodule, master branch)
    /          \
   v            v
 Boost       libdatachannel (WebTorrent)
 OpenSSL     /       |       \
           libjuice usrsctp  plog
```

Build targets:

```
seekserve-core     -->  libseekserve-core.a
seekserve-serve    -->  libseekserve-serve.a    (depends on core)
seekserve-capi     -->  libseekserve.dylib/.so  (depends on serve)
                   -->  libseekserve.a           (iOS static, with SEEKSERVE_CAPI_STATIC=ON)
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
|      - includes WebRTC data channels when WebTorrent is ON         |
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
|  [4] Caller thread                (user's thread / Dart isolate)   |
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

When a player sends an HTTP Range request, the following pipeline executes:

```
  Player Request: GET /stream/abc123/8?token=XXX  Range: bytes=1000000-1999999
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
|-- vcpkg.json                  Dependency manifest (Boost, spdlog, sqlite3, OpenSSL, etc.)
|-- setup.sh                    One-command bootstrap: vcpkg + submodules + build
|-- .gitmodules                 libtorrent submodule (master branch)
|
|-- extern/
|   +-- libtorrent/             Git submodule (arvidn/libtorrent@master)
|       +-- deps/
|           +-- libdatachannel/ WebRTC Data Channels (WebTorrent)
|           +-- libjuice/       ICE / NAT traversal
|           +-- usrsctp/        SCTP protocol
|           +-- plog/           Logging (header-only)
|
|-- seekserve-core/             STATIC LIB: torrent engine + scheduling
|   |-- include/seekserve/
|   |   |-- types.hpp               TorrentId, FileIndex, PieceSpan, ByteRange, FileInfo
|   |   |-- error.hpp               errc enum, Result<T>, error category
|   |   |-- config.hpp              SessionConfig, SchedulerConfig, ServerConfig, CacheConfig
|   |   |-- session_manager.hpp     lt::session lifecycle, add/remove, handle registry
|   |   |-- alert_dispatcher.hpp    Dedicated thread, pop_alerts(), typed handler registry
|   |   |-- metadata_catalog.hpp    Torrent -> file list, file selection, priority
|   |   |-- byte_range_mapper.hpp   HTTP byte ranges -> PieceSpan via file_storage::map_file()
|   |   |-- piece_availability.hpp  Lock-free atomic bitfield, contiguous byte calc
|   |   |-- byte_source.hpp         read(offset, len) with cv wait, disk read, cancel
|   |   |-- streaming_scheduler.hpp Hot window, lookahead, seek boost, mode switching
|   |   +-- offline_cache.hpp       SQLite index, LRU eviction, quota, offline-ready
|   +-- src/*.cpp
|
|-- seekserve-serve/            STATIC LIB: HTTP servers + engine facade
|   |-- include/seekserve/
|   |   |-- engine.hpp              SeekServeEngine facade (single entry point)
|   |   |-- http_range_server.hpp   Beast HTTP, /stream/{id}/{fi}, Range/206
|   |   |-- control_api_server.hpp  Beast HTTP, REST JSON API, CORS
|   |   +-- range_parser.hpp        RFC 7233 Range header parser
|   +-- src/*.cpp
|
|-- seekserve-capi/             SHARED/STATIC LIB: C API for FFI
|   |-- include/seekserve_c.h       Opaque handle, extern "C", error codes, callbacks
|   +-- src/seekserve_c.cpp         Bridge: JSON config, map_error, alloc_string
|
|-- seekserve-demo/             EXECUTABLE: CLI demo
|   +-- src/main.cpp                Add torrent, list files, select, stream URL
|
|-- tests/                      189 tests (138 unit + 32 integration + 19 C API)
|   |-- unit/                       ByteRangeMapper, PieceAvailability, Scheduler, etc.
|   +-- integration/                HTTP server, session lifecycle, stress tests
|
|-- flutter_seekserve/          FLUTTER PLUGIN: Dart API + native bindings
|   |-- pubspec.yaml                ffiPlugin: true, dependencies
|   |-- ffigen.yaml                 Config for dart run ffigen
|   |-- native_header/
|   |   +-- seekserve_c.h           Copy of C API header (ffigen input)
|   |-- lib/
|   |   |-- seekserve.dart          Public barrel export
|   |   +-- src/
|   |       |-- bindings_generated.dart  Auto-generated FFI bindings (DO NOT EDIT)
|   |       |-- native_library.dart      Platform DynamicLibrary loader
|   |       |-- seekserve_client.dart    High-level Dart API
|   |       |-- seekserve_exception.dart Error code mapping
|   |       +-- models/
|   |           |-- file_info.dart       FileInfo with isVideo, extension
|   |           |-- torrent_status.dart  TorrentStatus with all metrics
|   |           |-- seekserve_config.dart Config -> JSON serialization
|   |           +-- seekserve_event.dart  Sealed class: MetadataReceived, FileCompleted, etc.
|   |-- ios/
|   |   |-- flutter_seekserve.podspec    Vendored XCFramework, static_framework
|   |   +-- Frameworks/                  Pre-built seekserve.xcframework (gitignored)
|   |-- android/
|   |   |-- build.gradle                 minSdk=28, abiFilters, jniLibs
|   |   +-- src/main/jniLibs/            Pre-built .so files (gitignored)
|   |       |-- arm64-v8a/libseekserve.so
|   |       |-- armeabi-v7a/libseekserve.so
|   |       +-- x86_64/libseekserve.so
|   |-- test/
|   |   +-- models_test.dart             14 unit tests for Dart models
|   +-- example/                     EXAMPLE APP
|       |-- lib/
|       |   |-- main.dart                MediaKit init, Provider setup
|       |   |-- providers/
|       |   |   +-- seekserve_provider.dart  State management, polling, events
|       |   |-- screens/
|       |   |   |-- home_screen.dart     Torrent URL input, active torrent list
|       |   |   |-- file_selection_screen.dart  File list, tap to stream
|       |   |   +-- player_screen.dart   media_kit video player + status overlay
|       |   +-- widgets/
|       |       |-- torrent_card.dart    Progress bar, rates, peer count
|       |       +-- status_bar.dart      Download/upload rates, mode badge
|       |-- ios/                         Xcode project, Info.plist (ATS localhost)
|       +-- android/                     Gradle, network_security_config.xml
|
|-- scripts/
|   |-- build-ios.sh                 iOS arm64 (device + simulator) -> XCFramework
|   |-- build-android.sh            Android 3 ABIs via NDK -> .so files
|   +-- build-flutter-natives.sh    Orchestrator: build + copy to plugin
|
|-- triplets/
|   +-- arm-neon-android.cmake       Custom vcpkg triplet (NDK 29 NEON=ON)
|
|-- docs/                            Specification & design documents
|   |-- ARCHITECTURE.md, HTTP_RANGE_SPEC.md, SCHEDULER_POLICY.md
|   |-- STORAGE_POLICY.md, SECURITY.md, C_API.md
|
|-- fixtures/
|   +-- Sintel_archive.torrent       Test torrent (CC BY 3.0, 12 files, 4 MP4s)
|
+-- PROGRESS.md                      Detailed milestone checklist (M1-M13)
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
3. Initialize the libtorrent git submodule + recursive deps (libdatachannel for WebTorrent)
4. Detect WebTorrent availability (libdatachannel present -> `WEBTORRENT=ON`)
5. Configure CMake with vcpkg toolchain
6. Build all targets (core, serve, capi, demo, tests)

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

### Run Tests

```bash
cd build/debug && ctest    # All 189 tests
```

---

## Flutter Plugin

### Overview

The `flutter_seekserve` plugin wraps the C API via `dart:ffi`, providing a high-level Dart API with async events. It supports iOS and Android with pre-built native libraries.

### Building Native Libraries

Native libraries are **not committed** to the repository (they are 200+ MB). Build them with:

```bash
# Build for all platforms
scripts/build-flutter-natives.sh all

# Or build individually
scripts/build-flutter-natives.sh ios       # XCFramework (device + simulator)
scripts/build-flutter-natives.sh android   # 3 ABIs (.so files, stripped)
```

This builds the C++ SDK with all dependencies (libtorrent, Boost, OpenSSL, libdatachannel) and copies the artifacts to the plugin directories:
- iOS: `flutter_seekserve/ios/Frameworks/seekserve.xcframework`
- Android: `flutter_seekserve/android/src/main/jniLibs/{abi}/libseekserve.so`

### Library Sizes

| Platform | Architecture | Size (stripped) |
|----------|-------------|----------------|
| iOS | arm64 (device) | ~85 MB (static, combined with all deps) |
| iOS | arm64 (simulator) | ~85 MB |
| Android | arm64-v8a | ~15 MB |
| Android | armeabi-v7a | ~10 MB |
| Android | x86_64 | ~15 MB |

### Dart API

```dart
import 'package:flutter_seekserve/seekserve.dart';

// Create client with config
final client = SeekServeClient(config: SeekServeConfig(
  savePath: '/path/to/downloads',
  authToken: 'my_secret',
  enableWebtorrent: true,
  extraTrackers: ['wss://tracker.webtorrent.dev'],
));

// Start HTTP servers
final port = await client.startServer();

// Add torrent
final torrentId = await client.addTorrent('magnet:?xt=urn:btih:...');

// Listen for events
client.events.listen((event) {
  switch (event) {
    case MetadataReceived(:final torrentId):
      print('Metadata ready for $torrentId');
    case FileCompleted(:final torrentId, :final fileIndex):
      print('File $fileIndex completed');
    case TorrentError(:final message):
      print('Error: $message');
  }
});

// List files and select one
final files = await client.listFiles(torrentId);
final videoFile = files.firstWhere((f) => f.isVideo);
await client.selectFile(torrentId, videoFile.index);

// Get stream URL for video player
final url = await client.getStreamUrl(torrentId, videoFile.index);
// -> http://127.0.0.1:54321/stream/{id}/{fi}?token=...

// Monitor progress
final status = await client.getStatus(torrentId);
print('Progress: ${(status.progress * 100).toStringAsFixed(1)}%');
print('Peers: ${status.numPeers}, Rate: ${status.downloadRate} B/s');

// Cleanup
client.dispose();
```

### Example App

The example app demonstrates all plugin capabilities:

```bash
cd flutter_seekserve/example

# iOS (requires native libs built first)
flutter build ios --no-codesign

# Android (requires native libs built first)
flutter build apk
```

**Screens:**
1. **Home** — paste torrent URL/magnet, see active torrents with progress, rates, peer count
2. **File Selection** — browse all files, video files highlighted, tap to stream
3. **Player** — `media_kit` video player with live status bar (progress, rates, peers, mode)

### Flutter Tests

```bash
cd flutter_seekserve
flutter test          # 14 Dart unit tests
flutter analyze       # Static analysis
```

### Requirements

| Platform | Minimum |
|----------|---------|
| iOS | 15.0 |
| Android | API 28 (Android 9) |
| Dart | 3.2+ |
| Flutter | 3.19+ |

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

**Sintel** is an open-source short film by the [Blender Foundation](https://durian.blender.org/), released under [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).

| Property | Value |
|----------|-------|
| Infohash | `e4d37e62d14ba96d29b9e760148803b458aee5b6` |
| Files | 12 total (4 MP4, 1 MKV, 1 OGV, 1 AVI, subtitles, poster) |
| Test target | Index 8 — `sintel-2048-stereo_512kb.mp4` (73.8 MB) |

```
Index  Size       File
-----  ---------  ------------------------------------------
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

### Quick Functional Test

```bash
# 1. Build and run the demo
./setup.sh debug
./build/debug/seekserve-demo/seekserve-demo fixtures/Sintel_archive.torrent

# 2. Open the stream URL in VLC
vlc "http://127.0.0.1:PORT/stream/TORRENT_ID/8?token=TOKEN"

# 3. Verify:
#    - Playback starts within seconds (streaming-first mode)
#    - Seek forward/backward responds within 1-2s (seek boost activates)
#    - Piece scheduler prioritizes hot window around playhead
```

### Manual HTTP Verification

```bash
# HEAD request
curl -I "http://127.0.0.1:PORT/stream/TORRENT_ID/8?token=TOKEN"
# -> 200 OK, Accept-Ranges: bytes, Content-Length: 77395579, Content-Type: video/mp4

# Range request (first 1KB)
curl -r 0-1023 -o /dev/null -w "%{http_code}" \
  "http://127.0.0.1:PORT/stream/TORRENT_ID/8?token=TOKEN"
# -> 206

# Invalid token
curl -I "http://127.0.0.1:PORT/stream/TORRENT_ID/8?token=wrong"
# -> 403 Forbidden
```

### Control API Verification

```bash
TOKEN="your_auth_token"
BASE="http://127.0.0.1:CONTROL_PORT"

curl -H "Authorization: Bearer $TOKEN" "$BASE/api/torrents"
curl -H "Authorization: Bearer $TOKEN" "$BASE/api/torrents/TORRENT_ID/files"
curl -H "Authorization: Bearer $TOKEN" "$BASE/api/torrents/TORRENT_ID/status"
curl -H "Authorization: Bearer $TOKEN" "$BASE/api/torrents/TORRENT_ID/stream-url"
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

Engine configuration is passed as JSON to `ss_engine_create()` (C API) or via `SeekServeConfig` (Dart):

```json
{
    "save_path": "./downloads",
    "auth_token": "my_secret_token",
    "stream_port": 0,
    "control_port": 0,
    "max_storage_bytes": 0,
    "cache_db_path": "seekserve_cache.db",
    "log_level": "info",
    "enable_webtorrent": true,
    "extra_trackers": [
        "udp://tracker.opentrackr.org:1337/announce",
        "wss://tracker.webtorrent.dev"
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
| `enable_webtorrent` | `false` | Enable WebRTC-based BitTorrent peers |
| `extra_trackers` | `[]` | Additional tracker URLs (including `wss://` for WebTorrent) |

---

## Testing

### Run All Tests

```bash
cd build/debug && ctest          # All 189 tests at once

# Or individually:
./build/debug/tests/seekserve-unit-tests          # 138 unit tests
./build/debug/tests/seekserve-integration-tests    # 32 integration tests
./build/debug/tests/seekserve-capi-tests           # 19 C API tests
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
./setup.sh asan    # AddressSanitizer
./build/asan/tests/seekserve-unit-tests

./setup.sh tsan    # ThreadSanitizer
./build/tsan/tests/seekserve-unit-tests
```

### Flutter Tests

```bash
cd flutter_seekserve
flutter test          # 14 Dart unit tests for model classes
flutter analyze       # Static analysis (0 issues)
```

---

## Cross-Compilation

### iOS (arm64 device + arm64 simulator)

```bash
scripts/build-ios.sh
```

Builds a static XCFramework combining 22+ static libraries (libtorrent, Boost, OpenSSL, libdatachannel, seekserve) per architecture slice. Output:

```
build/ios-xcframework/seekserve.xcframework
```

### Android (arm64-v8a, armeabi-v7a, x86_64)

```bash
export ANDROID_NDK_HOME=/path/to/ndk   # or auto-detected from ~/Library/Android/sdk/ndk/
scripts/build-android.sh
```

Builds shared libraries for 3 ABIs. Output:

```
build/android-arm64-v8a/seekserve-capi/libseekserve.so
build/android-armeabi-v7a/seekserve-capi/libseekserve.so
build/android-x86_64/seekserve-capi/libseekserve.so
```

### Flutter Native Build (all-in-one)

```bash
scripts/build-flutter-natives.sh all     # iOS + Android
scripts/build-flutter-natives.sh ios     # iOS only
scripts/build-flutter-natives.sh android # Android only
```

Builds for all platforms, strips Android `.so` files (109 MB -> 6-15 MB), and copies artifacts into the Flutter plugin directories.

### Cross-Compile Notes

- **iOS**: uses vcpkg `arm64-ios` / `arm64-ios-simulator` triplets. Static library (`SEEKSERVE_CAPI_STATIC=ON`).
- **Android**: uses vcpkg `arm64-android` / `arm-neon-android` / `x64-android` triplets. Shared library.
- **Android NDK 29**: requires `ANDROID_ARM_NEON=ON` for armeabi-v7a (custom triplet `triplets/arm-neon-android.cmake`).
- **Android minSdk 28**: required because Boost.Asio uses `std::aligned_alloc` (API 28+).
- **WebTorrent ON**: both scripts pass `SEEKSERVE_ENABLE_WEBTORRENT=ON`. OpenSSL is in vcpkg.json for cross-compile.

---

## WebTorrent

SeekServe supports WebTorrent (WebRTC-based BitTorrent) via libtorrent's master branch and libdatachannel.

### How It Works

When WebTorrent is enabled:
1. libtorrent is compiled with `TORRENT_USE_RTC` define
2. The STUN server (`stun.l.google.com:19302`) is configured for NAT traversal
3. WebTorrent trackers (e.g. `wss://tracker.webtorrent.dev`) can be added via `extra_trackers`
4. The SDK can connect to both standard BitTorrent peers AND WebRTC-based browser peers

### Enabling WebTorrent

**Build time** (CMake):
```bash
-DSEEKSERVE_ENABLE_WEBTORRENT=ON    # Default when libdatachannel is detected
```

**Runtime** (config JSON):
```json
{
    "enable_webtorrent": true,
    "extra_trackers": ["wss://tracker.webtorrent.dev"]
}
```

**Dart API:**
```dart
SeekServeConfig(
  enableWebtorrent: true,
  extraTrackers: ['wss://tracker.webtorrent.dev'],
)
```

### WebTorrent Build Requirements

WebTorrent pulls additional dependencies via recursive git submodules:
- **libdatachannel** — WebRTC Data Channels
- **libjuice** — ICE / NAT traversal
- **usrsctp** — SCTP protocol
- **plog** — Logging (header-only)

These are automatically initialized by `setup.sh` (`git submodule update --init --recursive --force`).

### Disabling WebTorrent

```bash
-DSEEKSERVE_ENABLE_WEBTORRENT=OFF
```

The SDK falls back to standard BitTorrent only. Binary size decreases by ~30%.

---

## Documentation Index

| Document | Description |
|----------|-------------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Module overview, data flow, thread model, design decisions |
| [docs/HTTP_RANGE_SPEC.md](docs/HTTP_RANGE_SPEC.md) | HTTP endpoint format, Range handling (RFC 7233), response codes, MIME types |
| [docs/SCHEDULER_POLICY.md](docs/SCHEDULER_POLICY.md) | Hot window, lookahead, seek boost, deadline budget, mode switching |
| [docs/STORAGE_POLICY.md](docs/STORAGE_POLICY.md) | Piece storage, file selection, SQLite cache, LRU eviction, quota |
| [docs/SECURITY.md](docs/SECURITY.md) | Loopback binding, token auth, connection limits, logging safety |
| [docs/C_API.md](docs/C_API.md) | C function reference, error codes, config JSON, memory management |
| [PROGRESS.md](PROGRESS.md) | Detailed milestone checklist with every task (M1-M13) |

---

## Dependencies

| Dependency | Source | Purpose |
|------------|--------|---------|
| [libtorrent](https://github.com/arvidn/libtorrent) | Git submodule (master) | BitTorrent protocol, piece management, WebTorrent |
| [libdatachannel](https://github.com/nicknacknow/libdatachannel) | Nested submodule (via libtorrent) | WebRTC Data Channels for WebTorrent |
| Boost (Asio, Beast, JSON, System) | vcpkg | HTTP server, async I/O, JSON parsing |
| [OpenSSL](https://www.openssl.org/) | vcpkg | TLS for libtorrent + libdatachannel |
| [nlohmann/json](https://github.com/nlohmann/json) | vcpkg | JSON serialization (C++ side) |
| [spdlog](https://github.com/gabime/spdlog) | vcpkg | Structured logging |
| [SQLite3](https://www.sqlite.org/) | vcpkg | Offline cache persistence |
| [Google Test](https://github.com/google/googletest) | vcpkg | Unit and integration testing |
| [media_kit](https://github.com/media-kit/media-kit) | pub.dev (example app) | Video player for Flutter (libmpv-based) |

---

## Milestone Status

| Milestone | Description | Status |
|-----------|-------------|--------|
| **M1** | Project skeleton + libtorrent integration | Complete |
| **M2** | ByteRangeMapper + PieceAvailability + unit tests | Complete |
| **M3** | ByteSource (read with cv wait, cancel) | Complete |
| **M4** | HTTP Range Server (RFC 7233, Beast, multi-file) | Complete |
| **M5** | Streaming Scheduler (hot window, seek boost, modes) | Complete |
| **M6** | Control API + Offline Cache (REST, SQLite, LRU) | Complete |
| **M7** | C API Layer (opaque handle, JSON, callbacks) | Complete |
| **M8** | Hardening (timeouts, limits, sanitizers, docs) | Complete |
| **M9** | Flutter plugin scaffold + FFI bindings | Complete |
| **M10** | Native library builds (iOS XCFramework + Android 3 ABIs) | Complete |
| **M11** | Dart API layer + NativeCallable event system | Complete |
| **M12** | Example app (media_kit player, file selection, status) | Complete |
| **M13** | WebTorrent support (libtorrent master, libdatachannel) | Complete |

**189 C++ tests** (138 unit + 32 integration + 19 C API) + **14 Dart tests** = **203 total tests**.

See [PROGRESS.md](PROGRESS.md) for the detailed task-by-task checklist.

---

## License

SeekServe is proprietary software. All rights reserved.

Test torrent (`fixtures/Sintel_archive.torrent`) is [Sintel](https://durian.blender.org/) by the Blender Foundation, licensed under [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).
