# Changelog

## Unreleased

### Added — The piece a stuck reader waits for, asked first

#### `seekserve-core`, `seekserve-serve` and `seekserve-capi`

- **`SchedulerConfig::wait_focus_after`** (0 by default: as before; C API key `"wait_focus_after"`, seconds): a reader
  that waits for the same piece and gets no block of it that long, with a connected peer that has it, gets that piece
  asked first: the engine clears the deadlines and sets only that piece's, so libtorrent cancels the peers' requests of
  other pieces; again after each such while (`WaitFocusWatch`). The status says `wait_kicks`. libtorrent asks a peer
  for a piece with a deadline only while the peer's queue is short, and through Tor the queue of pieces not due kept a
  piece a peer had from being asked for 75 s on the app's phone (app BUG-246, DEC-670). In `413484e` the key began the
  start with priority again from that piece (app DEC-669), which did not help on the phone: a piece with a deadline
  has the top priority already.

#### `flutter_seekserve`

- **`TorrentStatus.waitKicks`** reads the engine's `wait_kicks` (null from an older library), for the app's debug log
  (app BUG-246, DEC-671).

### Fixed — The player that stands on a seek after its first frame

#### `flutter_seekserve_ui`

- **`SsVideoPlayer.unstickAfter`** (null by default: as before) opens a local stream again, once, when the player has
  stood on a seek for that long after its first frame (`SsPlayerUnstick`, read from `SsPlayerMotion`,
  `ssReadPlayerMotion`). On Android media_kit switches the video output after the first frame and seeks; when mpv is
  reading ahead into a missing piece, the seeks queue behind that read and the screen stays black with the data on the
  phone (app BUG-246, DEC-669). The diagnostics get the stages `visible` and `unstuck` (`ssIsVisibleOutput`).

### Added — The video player's diagnostics

#### `flutter_seekserve_ui`

- **`SsVideoPlayer.diagnostics`** (`SsPlayerDiagnostics`, null by default) gives an app, for its debug log, mpv's
  messages and the player's errors with no address (`ssRedactPlayerLog`), the stages of each player with their times
  and a word when one is late (`SsPlayerLife`, `SsPlayerStage`), the player's state on request (`SsPlayerProbe`,
  `SsPlayerState`) and software decoding; `SsCappedLog` caps the lines (app BUG-238, BUG-239, DEC-662).

### Added — The piece a reader waits for, in the status

#### `seekserve-core` and `seekserve-serve`

- **`get_status_json`** (and `ss_get_status`), while a reader waits for a download, says the first piece missing from
  its position (app BUG-238, BUG-228, DEC-661): `wait_piece`, `wait_piece_peers`, `wait_piece_peers_open`,
  `wait_piece_blocks`, `wait_piece_done`, `wait_piece_asked`, `wait_piece_free`, `wait_piece_askers`,
  `wait_piece_slowest_kbs`, `wait_piece_fastest_kbs`, `wait_piece_still_ms` (`wait_piece_stats()`,
  `WaitPieceWatch`). No address goes out.

#### `flutter_seekserve`

- **`WaitPiece`**, from `TorrentStatus.waitPiece`: the same numbers, null when no reader waits.

### Added — What the piece reads did, in the status

#### `seekserve-core` and `seekserve-serve`

- **`get_status_json`** (and `ss_get_status`) says what the reads of a stream did, for the app's debug log (app
  BUG-238, DEC-660): `piece_asks`, `piece_repeats`, `piece_answers`, `piece_errors`, `piece_last_error`,
  `piece_late`, `piece_waiting`, `piece_oldest_wait_ms` (`TorrentPieceReader::stats()`); `disk_reads`,
  `read_timeouts`, `readers_waiting_download`, `readers_waiting_answer` (`ByteSource::stats()`); `range_requests`
  (`HttpRangeServer::requests()`); `alerts_dropped` (libtorrent's `alerts_dropped_alert`). No address, no file name.

#### `flutter_seekserve`

- **`StreamReads`**, from `TorrentStatus.reads`: the same numbers, null from an older library.

### Fixed — A piece read that never answers no longer stops the stream

#### `seekserve-core`

- **`TorrentPieceReader`** kept a request to `read_piece` on its way until it was answered (since the fix of the late
  answers): an answer that never came stopped the stream for good, and on the app's Pixel 4 a player waited with the
  whole file on disk (app BUG-238, DEC-659). A read that finds the piece still unanswered, with nobody else waiting
  for it, now asks again; a late answer is still the next read's.
- Tests: `TorrentPieceReaderTest.AReadOfAPieceWithNoAnswerAsksAgain`, `.AReadWhileAnotherWaitsAsksNoMore`,
  `.TheSecondAnswerOfAPieceAskedTwiceIsHarmless`.

### Added — The piece length and the start with priority in the status

#### `seekserve-serve`

- **`get_status_json`** (and `ss_get_status`) says `piece_length`, the bytes of each piece of the torrent (once it has
  its metadata), and `startup_focus`, whether the start of a stream with priority still holds the other pieces back
  (with a reader only): the app's debug log tells why the first bytes of a stream take long (app BUG-228).
- Test: `MultiFileTorrent.TheStatusSaysThePieceLengthAndTheStartWithPriority`.

#### `flutter_seekserve`

- **`TorrentStatus`** reads `pieceLength`, `startupFocus`, `pieceReads` and `pieceCacheHits`, null from an older
  library.

### Fixed — A piece read through libtorrent that takes longer than a wait

#### `seekserve-core`

- **`TorrentPieceReader`** forgot a request when a read stopped waiting, threw away the answer that came later and
  asked again at the next read: the stream server waits in slices of half a second, and a big piece (up to 16 MB in a
  pack) can take longer to read, so the read never ended and the player never started (seen on the app's Pixel 4
  with a pack of ten 1.9 GB episodes, app BUG-236, DEC-657). Now a request stays on its way and a late answer is the
  next read's, with no second request; an error nobody waits for is not kept (a later read, once the piece is there
  again, asks again); at most `kMaxIdlePieces` (8) pieces with nobody waiting are kept; `cancel()` lets them all go.
- Tests: `TorrentPieceReaderTest.ALateAnswerIsTheNextReadsWithOneRequest`, `.AReadWhileARequestIsOnItsWayAsksNoMore`,
  `.OnlyAFewAnswersNobodyTookAreKept`, seen failing first, and `.AnErrorNobodyWaitsForIsNotKept`.

### Fixed — A piece that arrives during a stream is read through libtorrent

#### `seekserve-core`, `seekserve-serve`, `seekserve-capi`

- **`ByteSource`** read a piece from the file as soon as libtorrent said it complete, but libtorrent checks a piece's
  hash from its store buffer (`mmap_disk_io`), before the piece is written to the file: read at that moment, the
  start of a file of a pack (the part of the piece shared with an unwanted file before it, which also goes to the part
  file) came back as zeros (app BUG-236, DEC-657; in the test pack 11,308 wrong bytes, right one second later).
  Now the pieces that arrive during the stream are read through libtorrent (`TorrentPieceReader`,
  `torrent_handle::read_piece` and its `read_piece_alert`, which also looks in the store buffer); the pieces that were
  there when the stream started are read from the file as before. A piece read is kept for the next chunks
  (`PieceCache`, up to `ServerConfig::piece_cache_bytes`, 16 MB by default, always at least one piece); a piece
  libtorrent says it does not have goes back to waiting (`PieceAvailabilityIndex::mark_incomplete`).
- The status of a torrent with a stream gets `piece_reads` and `piece_cache_hits`. The C API reads
  `"piece_cache_bytes"`, last.
- Tests: `PieceCacheTest.*` (4), `TorrentPieceReaderTest.*` (5), `ByteSourcePiecesTest.*` (5), seen failing against
  empty implementations first (but for the ones true before too); `MultiFileTorrent.TheStartOfAFileOfAPackIsRightWhileItDownloads`,
  seen failing first, two runs in two; `MultiFileTorrent.AStartWithPriorityGivesTheRightBytesAndTheWholeFile` reads
  from the start again.

### Added — The start of a stream with priority

#### `seekserve-core`, `seekserve-serve`, `seekserve-capi`

- **`StartupFocus`** (app BUG-228, DEC-656): when a reader starts or jumps farther than the hot window
  (`StreamingScheduler::on_range_request` now says so), the pieces of its first `startup_focus_bytes` go to the top
  priority and every other wanted piece to the lowest, never to zero (with nothing else wanted libtorrent would call
  the torrent finished, and the app's queue a download complete). It ends when those bytes are there or
  `startup_focus_timeout` passed (the tick checks), and the files' priorities are set again. Through Tor the peers'
  queues fill with pieces of the file that are not due, and the pieces a player waits for came one at a time behind
  them: on the app's phone 1.5 MB in 60 s with 13–14 peers and up to 334 kB/s.
- `SchedulerConfig::startup_focus_bytes` (default 0 = off, as before) and `startup_focus_timeout` (default 30 s); in
  the C API the keys `"startup_focus_bytes"` and `"startup_focus_timeout"` (seconds), read after every other key.
- Tests: `StartupFocusTest.*` (8) and `StreamingSchedulerTest.TheFirstRequestAndAFarJumpAreANewPlace`, seen failing
  against empty implementations first; `MultiFileTorrent.AStartWithPriorityGivesTheRightBytesAndTheWholeFile`.
- Found on the way: app BUG-236, fixed above.

### Fixed — Closing a share no longer stops the player's stream

#### `seekserve-serve`

- **`close_share`** of a torrent's share left the player of the phone without data for ever: the share borrows the
  stream (`ByteSource`) of the file a player reads, and stopping the share's server cancelled it for the player too,
  who then got no byte and retried in a loop (app WORK-37, seen on a Pixel 9 Pro taking the film back from the TV).
  The share's server now lets the stream go without cancelling it (`HttpRangeServer::detach_byte_source`); a file
  share still cancels its own `FileSource`.
- Tests: `EngineShareTest.ClosingAShareLeavesThePlayersStream`. Not run here (no toolchain in the session that
  wrote it): to run on the Mac.

### Added — Shares of a file on disk

#### `seekserve-core`, `seekserve-serve`, `seekserve-capi`

- **`open_file_share`** (`ss_open_file_share`): a share of a whole file on disk that is no torrent's, a download
  already complete, for the app's TV (app WORK-37, DEC-627): the same server and protections as `open_share`, with
  no torrent and no peer, so it also works while the session cannot reach the network (the app's DoH mode). Only a
  regular file inside the save path, once every link and `..` is resolved; the file is opened by its resolved path
  without following a last link, and held open (`FileSource`, `pread`), so what is read is the file that was
  checked. Closed with `close_share`.
- **`StreamSource`**: what a stream server reads a file through, now an interface: `ByteSource` (a torrent's file)
  and `FileSource` (a whole file) implement it.
- Tests: `FileSourceTest.*` (5), `EngineFileShareTest.*` (3), `CApiTest.FileShareNullArgs`,
  `.AFileShareOpensAFileOfTheSavePathOnly`. Written with the code, not seen failing first: two deliberate faults
  (no save path check; a cancel that does nothing) were each caught, then removed.

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
