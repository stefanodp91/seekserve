# Changelog

## Unreleased

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
