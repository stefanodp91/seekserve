# Changelog

## Unreleased

### Added — Shares: a file served to one device of the home network

#### `seekserve-serve`, `seekserve-capi`

- **`open_share` / `close_share`** (`ss_open_share`, `ss_close_share`): the file a player reads, served to one
  other device, a TV (app WORK-37, DEC-613), by a stream server of its own. It listens on the one address it is
  given (never a wildcard) and closes, without an answer, every connection that does not come from the one client
  address it is given (`ServerConfig::allowed_peer`). Its URL has a random share id and token (16 bytes each from
  `std::random_device`), never the torrent's info hash, which tells anyone what the video is, nor the engine's
  token. Its reads move the file's scheduler; it follows the file when it is selected again, answers 404 once the
  file is no longer the reader's, and lasts until `close_share` or `stop_server`. The control API stays on
  127.0.0.1. A stream URL on an IPv6 address has it in brackets.
- Tests: `EngineShareTest.*` (7), `HttpRangeServerLimitsTest.OnlyTheAllowedPeerIsServed`, `.TheAllowedPeerIsServed`,
  `.AnIpv6StreamUrlHasItsAddressInBrackets`, `CApiTest.ShareNullArgs`, `.ShareNotFound`,
  `.AShareOpensClosesAndTakesTheReadTimeoutKey`. Two deliberate faults (no address check; a share that does not
  follow its file) were each caught, then removed.
- `open_share` holds the shares' lock from its check that the servers run until the share is listed, so a
  `stop_server` or a new stream for the file at that moment cannot leave a share open after the stop or on the old
  stream (from an automated security review of the first version; not covered by a test).

### Fixed — Connections that hold a stream slot without moving

#### `seekserve-serve`, `seekserve-capi`

- **Stream server** (app BUG-88): `read_timeout` and `connection_timeout` never applied, because Asio's blocking
  calls wait in `poll()` whatever `SO_RCVTIMEO` and `SO_SNDTIMEO` say. A connection that sent no request, or a
  reader its client stopped reading, kept one of the `max_concurrent_streams` slots for good. Each connection's
  socket is now non-blocking underneath, and every wait for the client is a `poll()` in 500 ms slices that can
  end: the whole request has `read_timeout` to arrive, counted from the connection even if it comes a byte at a
  time; a write to the newest reader of a stream may wait `connection_timeout` (now 0 by default = no limit,
  which is what it was in fact, so a paused player keeps its connection); a reader that a newer request of its
  stream left behind is closed `stale_reader_timeout` (new, 0 = no limit) after the newer request took over,
  whatever its client reads meanwhile. A connection over the limit takes the
  place of the oldest reader left behind instead of being turned away: when the app's TV seeks it keeps up to four
  connections to the file (app WORK-37, DEC-609).
- C API keys `read_timeout`, `connection_timeout`, `stale_reader_timeout` (seconds), read after every other key.
- Tests: `HttpRangeServerLimitsTest.*` (9 for this, 7 seen failing first; the other 2 check that what was right
  stays so). Two of them (a request a byte at a time, a reader left behind that reads a little at a time) came
  from an automated security review of the first version, which measured each limit from the last byte.

### Fixed — What a player that leaves asked for

#### `seekserve-serve`

- **`want_files`** (`select_file`, `set_wanted_files`, `ss_set_wanted_files`): when the reader leaves, the requests it
  made and that have not come yet are cancelled (`cancel_requests`: a deadline set and taken away at once on a piece
  still wanted, which makes libtorrent cancel every request that is not time critical; the file priorities are set
  again afterwards). libtorrent kept them when the deadlines went or the file's priority dropped, and the peers went
  on sending: on the app's phone the file left kept coming for 5 to 10 seconds after the player closed, 3 with this
  (app BUG-201, P5-09, DEC-330).
- Test: `MultiFileTorrent.WhenThePlayerLeavesWhatItAskedForIsCancelled`, counting the cancels the seeder gets for the
  reader's pieces (none before the fix).

### Fixed — A torrent libtorrent put into error

#### `seekserve-serve`

- **Status** (`get_status_json`, `ss_get_status`): `torrent_error`, with `message` and `file` (libtorrent's index,
  or one of its negative values), when libtorrent put the torrent into error. libtorrent pauses such a torrent, and
  the status only said `paused` (app BUG-202, P6-04; app DEC-323). `error` still means a torrent the engine does
  not have.
- **`resume_torrent` / `start_torrent`** (`ss_resume_torrent`, `ss_start_torrent`): a torrent in error is checked
  again first (`force_recheck`), which clears the error and reads the files again. A plain resume left it in error,
  and `clear_error` alone checked the files against libtorrent's stat cache, which still held what made the error.
  The check lets the peers go; they come back after `min_reconnect_time`.
- Tests: `MultiFileTorrent.TheStatusSaysLibtorrentsError`, `.StartingATorrentInErrorTriesAgain`,
  `.ResumingATorrentInErrorTriesAgain` (a folder in a file's place puts the torrent into error).

## 0.2.0 - 2026-10-15

The first numbered release after 0.1.0: every change recorded below, up to `ac3c1a0`. The version is in `CMakeLists.txt`, `vcpkg.json`, `flutter_seekserve/pubspec.yaml` and `flutter_seekserve_ui/pubspec.yaml`.

### Added — Two wanted files per torrent

#### `seekserve-core` / `seekserve-serve`

- **`engine.hpp` / `engine.cpp`**: `set_wanted_files(id, reader, background)` asks for the file a player reads
  and the file that downloads behind it (`-1` for none). With both, the reader's file and the subtitles get
  libtorrent's top priority and the other the default one, so the video comes first and the file behind goes
  on by itself once the reader's is whole. The reader keeps its stream while only the file behind it changes;
  when it leaves, its stream ends, its deadlines are cleared and its file stops. `select_file(id, fi)` is still
  that file alone, with a new stream at every call.
- **Status** (`get_status_json`, `ss_get_status`): `files`, one entry for each file the torrent downloads, with
  `index`, `size`, `downloaded`, `complete` and, for the files asked for, `role` (`reader` or `background`).
  `selected_file` is the reader's file, or the background one when no player reads. `progress` still counts the
  wanted pieces of the whole torrent.
- **Resume data**: a torrent restored from its resume data comes back without the file a player was reading
  ahead of a download, and without the piece priorities deadlines left (`drop_reader_priorities`).

#### `seekserve-capi`

- **`seekserve_c.h` / `seekserve_c.cpp`**: new function `ss_set_wanted_files(engine, torrent_id, reader_file,
  background_file)` (19 functions).

#### Flutter FFI (`flutter_seekserve`)

- **`torrent_status.dart`**: `TorrentStatus.files` (`WantedFile`, `WantedFileRole`).
- **`seekserve_client.dart`**: `setWantedFiles(torrentId, readerFile:, backgroundFile:)`.
- **`bindings_generated.dart`**: manual FFI binding for `ss_set_wanted_files`; `native_header/seekserve_c.h`
  is the C header again.

### Fixed — Calls on a torrent that is being removed

- **`seekserve_c.cpp`**: no call lets a C++ exception out of the library. libtorrent throws on a handle whose
  torrent another thread removed between the engine's validity check and its call; a caller across the C
  boundary (the app's JNI, Dart FFI) cannot catch that, and the process would end. Such a torrent is now
  `SS_ERR_NOT_FOUND`; any other failure inside a call is `SS_ERR_IO`.
- **`engine.cpp`**: `get_status_json` answers `torrent not found` for a torrent removed while it asked, and
  choosing files (`select_file`, `set_wanted_files`) returns `torrent_not_found`.

---

### Added — Torrent Force Reannounce

#### `seekserve-serve`

- **`engine.hpp` / `engine.cpp`**: nuovo metodo `force_reannounce(const TorrentId& id)` che chiama
  `lt::torrent_handle::force_reannounce()` bypassando il backoff esponenziale di libtorrent verso i tracker.
  Utile quando SOCKS5/Tor è attivo e DHT è disabilitato: alla prima connessione i circuiti Tor non sono
  ancora cached; un reannounce periodico garantisce che il tracker announce avvenga non appena i circuiti
  sono disponibili.

#### `seekserve-capi`

- **`seekserve_c.h` / `seekserve_c.cpp`**: nuova funzione C API `ss_force_reannounce(engine, torrent_id)`.

#### Flutter FFI (`flutter_seekserve`)

- **`seekserve_client.dart`**: metodo `forceReannounce(String torrentId)`.
- **`bindings_generated.dart`**: binding FFI manuale per `ss_force_reannounce`.

---

### Added — Video Player: Local File Playback

#### Flutter UI (`flutter_seekserve_ui`)

- `SsVideoPlayer`: skip HTTP probe for `file://` URLs — `media_kit` handles
  local files natively, so the stream-availability check is unnecessary and
  would fail on non-HTTP schemes.

---

### Added — Video Player: Title and Back Button

#### Flutter UI (`flutter_seekserve_ui`)

- `SsVideoPlayer` / `_PlayerOverlay`:
  - `title` parameter — optional title displayed in the top-left corner of the
    overlay, with ellipsis overflow for long text.
  - `onBack` parameter — optional callback that renders a back arrow button in
    the top-left corner; triggers `_onInteraction()` to reset the auto-hide timer.
  - Top bar layout changed: back arrow + title on the left, track selector
    buttons on the right (with `Spacer` when no title is provided).

---

### Fix — use-after-free in HttpRangeServer (arm64/Android 16 crash)

#### `seekserve-serve`

- **`http_range_server.hpp`:** `HttpRangeServer` now inherits from
  `std::enable_shared_from_this<HttpRangeServer>` to allow safe capture in
  detached threads.
- **`http_range_server.cpp` — `do_accept()`:** the async-accept lambda and the
  detached `handle_connection` thread now capture a `shared_ptr<HttpRangeServer>`
  via `shared_from_this()` instead of a raw `this` pointer. This prevents a
  use-after-free (Data Abort, ISS 0x06) on arm64 when `stop_server()` destroys
  the `HttpRangeServer` while an active connection thread still holds references
  to `sources_mu_`, `sources_`, `range_callback_`, and `auth_token_`.

#### `seekserve-serve` (engine)

- **`engine.hpp` / `engine.cpp`:** `http_server_` changed from
  `unique_ptr<HttpRangeServer>` to `shared_ptr<HttpRangeServer>` so that
  `shared_from_this()` is valid at construction time.

**Impact:** real use-after-free bug eliminated. Android 16 ships with MTE (Memory
Tagging Extensions) enabled on Pixel 9 Pro which reliably detects dangling pointer
accesses that silently succeeded on older devices. Note: the "Connection refused"
symptom observed on Pixel 9 Pro (Android 16, arm64-v8a) during testing was
ultimately traced to a separate Android 16 bug in `libfuse_jni.so`
(`NodeTracker::CheckTracked()`) triggered when the `:torrent_service` process opened
files on the FUSE-backed external storage path — fixed on the app side by switching
`save_path` to internal storage (`filesDir`).

---

### Security — libtorrent session hardening and shared auth utilities

#### `seekserve-core`

- **0.0.0.0 listen interface restored:** `listen_interfaces` reverted to
  `0.0.0.0:port,[::0]:port` — loopback-only binding broke DHT and outbound
  peer connections. LAN exposure is mitigated by separately disabling UPnP/NAT-PMP/LSD.
- **LAN discovery disabled:** UPnP, NAT-PMP, and LSD are explicitly disabled to
  prevent broadcasting the engine's presence on the local network.
- **Peer encryption: `pe_enabled` (not forced):** both inbound and outbound policies
  set to `pe_enabled` with `pe_rc4` preferred; plaintext accepted as fallback.
  `pe_forced` was rejecting ~15–25% of peers (old clients, seedboxes without MSE)
  causing very low download speeds. Privacy guaranteed by `anonymous_mode`,
  spoofed `user_agent`/`peer_fingerprint`, and DoH/Tor at the transport layer.
- **Anonymous mode:** `anonymous_mode = true` suppresses the client version in the
  BEP-10 extension handshake, uses a generic user-agent toward HTTP trackers, and
  omits private IPs from announces.
- **User-agent spoofing:** `user_agent` overridden to `"Mozilla/5.0"` to prevent
  client fingerprinting at the tracker layer.
- **BEP-10 version suppression:** `handshake_client_version` set to `""` — the
  version field is omitted entirely from the extension handshake.
- **Peer ID spoofing:** `peer_fingerprint` set to `"-UT3600-"` (µTorrent 3.6.0.0),
  replacing the default `-lt{ver}-` prefix that identifies libtorrent.

#### `seekserve-serve`

- **`auth_utils.hpp` (new):** shared header in `include/seekserve/` exposing
  `constant_time_compare()` and `extract_token()` as `inline` functions in the
  `seekserve` namespace, removing duplication between `control_api_server.cpp`
  and `http_range_server.cpp`.
- **`control_api_server.cpp`:** removed local `constant_time_compare` and
  `extract_token` implementations; now includes `auth_utils.hpp`.
- **`http_range_server.cpp`:** same deduplication; auth check updated to use
  `extract_token(req)` from the shared header.

---

### Changed — Android build: configurable ABI list

The Android build scripts (`build-android.sh`, `build-flutter-natives.sh`) now
read the target ABIs from the `SEEKSERVE_ANDROID_ABIS` environment variable
(space-separated). Default is `arm64-v8a armeabi-v7a`, dropping x86_64 (emulator
only, ~208 MB unstripped) from the default set.

To include x86_64:
```
SEEKSERVE_ANDROID_ABIS="arm64-v8a armeabi-v7a x86_64" ./scripts/build-flutter-natives.sh android
```

### Fixed — Android build script: cross-platform NDK detection

`scripts/build-flutter-natives.sh` hardcoded a macOS-only path
(`$HOME/Library/Android/sdk/ndk/`) and a `darwin-x86_64` prebuilt directory
for `llvm-strip`. On Linux the `ls` glob returned exit code 1, causing the
script to abort (due to `set -euo pipefail`) after a successful compilation,
before the `.so` files were copied to the output directory.

The NDK detection now follows the same priority order used by
`build-android.sh`: `ANDROID_NDK_HOME` env var → `ANDROID_HOME/ndk/` →
macOS SDK fallback. The `llvm-strip` prebuilt directory is selected based on
the host OS (`linux-x86_64` or `darwin-x86_64`), and stripping is silently
skipped when the tool is not found.

### Added — Video Player: Audio & Subtitle Track Selector

In-player track selection for audio streams and subtitles (both embedded and
external sidecar files from the torrent).

#### Flutter UI (`flutter_seekserve_ui`)

- `SsTrackSelector` — bottom-sheet modal listing audio/subtitle tracks with
  the currently active one highlighted; exported from the package.
- `SsVideoPlayer` / `SsVideoControls`:
  - Audio-track button (top-right overlay) — visible when ≥ 2 audio tracks
    exist; opens `SsTrackSelector` and calls `player.setAudioTrack()`.
  - Subtitle button (top-right overlay) — visible when embedded subtitle
    tracks or external subtitle files are present; supports:
    - "Off" option (sets `SubtitleTrack.no()`).
    - Embedded tracks (by container ID).
    - External `.srt`/`.vtt`/`.ass`/`.ssa` files streamed over HTTP.
  - `torrentFiles`, `streamUrlBuilder`, `onFileSelectRequested` parameters
    added to `SsVideoPlayer` and `SsVideoControls` to supply file metadata
    and URL generation for external subtitle loading.
  - Format-retry: on "failed to recognize file format" / "invalid data"
    errors (torrent not yet buffered), the player silently retries after
    4 seconds instead of showing an error.

#### Flutter Plugin (`flutter_seekserve`)

- `FileInfo.isSubtitle` — true for `.srt`, `.vtt`, `.ass`, `.ssa`.
- `FileInfo.isAudio` — true for `.mp3`, `.flac`, `.ogg`, `.wav`, `.aac`.

---

### Added — Torrent Pause/Resume

Full-stack pause/resume support across the entire SDK, from the C++ engine to the Flutter UI.

#### C++ Engine (`seekserve-serve`)

- `SeekServeEngine::pause_torrent(id)` / `resume_torrent(id)` — delegates to
  `lt::torrent_handle::pause()` / `resume()` and fires SSE events
  (`torrent_paused`, `torrent_resumed`).
- `"paused"` boolean field added to status JSON output (`get_status_json()`)
  using `lt::torrent_flags::paused`.

#### Control API (`seekserve-serve`)

- `POST /api/torrents/{id}/pause` — pauses a torrent.
- `POST /api/torrents/{id}/resume` — resumes a paused torrent.
- `GET /api/torrents/{id}/status` response now includes `"paused": true|false`.

#### C API (`seekserve-capi`)

- `ss_pause_torrent(engine, torrent_id)` — pauses a torrent's download and upload.
- `ss_resume_torrent(engine, torrent_id)` — resumes a previously paused torrent.

#### Flutter Plugin (`flutter_seekserve`)

- `SeekServeClient.pauseTorrent(String)` / `resumeTorrent(String)` via FFI.
- `TorrentStatus.isPaused` field (parsed from `json['paused']`, defaults to `false`).
- `TorrentPaused` / `TorrentResumed` event classes in `SeekServeEvent`.

#### Flutter UI (`flutter_seekserve_ui`)

- `SsTorrentManager.pauseTorrent()` / `resumeTorrent()` / `togglePause()`.
- Play/pause icon button on `SsTorrentTile` (torrent list).
- Pause/Resume button on `SsTorrentDetail` (detail view).
- Badge shows `"PAUSED"` with dedicated color when torrent is paused.

#### Flutter App (`flutter_seekserve_app`)

- `onTogglePause` callback wired in `HomeScreen` and `TorrentDetailScreen`.

#### Tests

- 6 new C API tests: null engine, null id, not found, and full
  pause→verify→resume→verify lifecycle.
- 4 new Dart model tests: `isPaused` parsing and `TorrentPaused`/`TorrentResumed`
  event parsing.

### Fixed — Use-After-Free Crash (SIGSEGV in `is_span_complete`)

Crash on Android (Pixel 4, API 33) inside
`PieceAvailabilityIndex::is_span_complete()`, called from `ByteSource::read()`
on an HTTP connection thread during active streaming.

**Root cause:** `ByteSource` holds non-owning references (`&`) to
`PieceAvailabilityIndex` and `ByteRangeMapper`, which are owned by
`TorrentState`. When `remove_torrent()` destroyed the `TorrentState`, the
`shared_ptr<ByteSource>` still held by `HttpRangeServer` kept the `ByteSource`
alive with dangling references. An HTTP thread calling `read()` would then
dereference destroyed objects.

**Fix:**

- **`HttpRangeServer`**: added `remove_byte_source(torrent_id, file_index)` and
  `remove_byte_sources_for_torrent(torrent_id)` to safely cancel and remove
  sources before their backing state is destroyed. `stop()` now clears the
  sources map after cancelling.
- **`SeekServeEngine::remove_torrent()`**: removes ByteSources from
  `HttpRangeServer` *before* erasing the `TorrentState`, ensuring no HTTP thread
  can access destroyed `avail_` or `mapper_` references.
- **`SeekServeEngine::select_file()`**: cleans up the previous file selection's
  ByteSource from `HttpRangeServer` before replacing the `TorrentState`.
- **`ByteSource::read()`**: reordered the condition-variable predicate to check
  `cancelled_` *before* `avail_.is_span_complete()`, closing a race window where
  a waking thread could access a dangling `avail_` reference before seeing the
  cancellation flag.

#### Files changed

| File | Change |
|------|--------|
| `seekserve-serve/include/seekserve/http_range_server.hpp` | Added `remove_byte_source()`, `remove_byte_sources_for_torrent()` |
| `seekserve-serve/src/http_range_server.cpp` | Implemented removal methods; `sources_.clear()` in `stop()` |
| `seekserve-serve/src/engine.cpp` | Reordered `remove_torrent()`; added old-source cleanup in `select_file()` |
| `seekserve-core/src/byte_source.cpp` | Predicate reorder: check `cancelled_` before `avail_` |
