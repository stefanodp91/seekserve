# Resuming work on `dev`

Handoff notes for the work branch, `dev` (until 2026-09-25 `chore/resume-build-remove-auth`). The full project context (decisions, security
register, roadmap) lives in the app repository, `obsidian-eclipse`, in
`docs/wiki/analyses/resume-guide.md`.

## Where things are

| What | Where |
|---|---|
| This repo | `/Users/stefano/Workspace/seekserve`, remote `https://github.com/stefanodp91/seekserve` |
| Branch | Since 2026-09-26 the work branch is `dev` (app DEC-27: the owner asked to bring everything to `dev`, in the app and in the related repositories, to restore the app's CI and Firebase App Distribution). `dev` is fast-forwarded to the tip of `chore/resume-build-remove-auth`, so every commit and every SHA the app pins stays the same; `chore/resume-build-remove-auth` stays on GitHub untouched, and so does `main`. Before: `chore/resume-build-remove-auth`, created from `feature/native-jackett-engine` at `403bef8` and published (pushes on 2026-09-22: `dba03c0`, `3bc9aa0` and these notes, last updated for app BUG-19 and BUG-55). The notes of 2026-09-23 (app BUG-43, the app's reconciliation) were published that day with the owner's approval, together with the app's WORK-07, and so was `f49afe4` with the app's WORK-15. Then `847c1be` (`ca_cert_file`, app BUG-61) and the notes up to this one were published on 2026-09-25 with the owner's approval ("pubblica tutto"), before the app's WORK-21 and WORK-22. Also on 2026-09-25, each time with the owner's approval and before the app: `2f8509c` (`ss_start_torrent`, app BUG-63) with its notes, then `4782c68` (a torrent removed and added again gets its metadata, app BUG-68) with the notes up to `b767dcb`. On 2026-09-25 evening `6c2decd` (a hybrid torrent keeps its add id when its metadata arrives, app BUG-71), then, after an independent review, `d5923ea` (the add id travels in the torrent's userdata), with these notes: published on 2026-09-25 night with the owner's approval ("sì, prova sull'A25 e poi pubblica tutto"), before the app, which pins `d5923ea`. `feature/native-jackett-engine` and `main` are untouched |
| App | `obsidian-eclipse` pins `bb1b283` for `flutter_seekserve` and `flutter_seekserve_ui` since app commit `43b4ef63` (2026-10-11, app WORK-29 wave P2b), and its arm64-v8a `libseekserve.so` is built from `bb1b283` in this clone since app commit `6cfcd09f` (tested on the owner's phone that day, app DEC-179: after a full stop of the app the player opens in about a second, 6 to 9 before). The app's service always sets `resume_data` and takes `resume_save_interval` from the app's configuration (30 s). Both app commits were published on 2026-10-11 with the owner's approval (app DEC-180), right after this repository. The same day the resume data was stress-tested on that phone (app DEC-182, step P2b-09): the app killed twice (`am force-stop`) while a season pack of 2804 pieces was downloading, in the middle of a file and across two files; each time the download went on 7 to 10 seconds after the app was opened again, from where it was, a file finished before the kill stayed finished, and the two files, copied off the phone, matched the torrent's SHA-1 hashes piece by piece (415 of 415, 497 of 497), the pieces half done at the kills included. Note for whoever reads a resume blob by hand: libtorrent 2.1 writes `pieces` as a bitfield, not as one byte per piece. Before: `082a17b` since app commit `b96bbde1` (2026-10-10, app WORK-29 wave P2; `c93d7b3` from `1817390e`, `2b8af2c` from `ca77b204`), where only `flutter_seekserve_ui` had changed since `387c213`, no native code, and the `libseekserve.so` in the app was still the one built from `387c213`. Before: `387c213` for `flutter_seekserve` and `flutter_seekserve_ui` since app commit `55a8fab3` (2026-09-26 night, app BUG-83), with the arm64-v8a `libseekserve.so` built from it in this clone; `387c213` and its notes (`cc84397`, this update) were published on 2026-09-27 with the owner's approval ("Pubblica ora"), before the app. Before: `d5923ea` since app commit `d5c3378f` (2026-09-25, published that night after this repository), `6c2decd` since `4239968b` (2026-09-25), `4782c68` since `b0e095eb` (2026-09-25), `2f8509c` since `ed706005` (2026-09-25, published first, then the app), `847c1be` since `ad596493` (2026-09-23; before that `3bc9aa0`, `782f2ee` and `38cf24a`), and ships an Android `libseekserve.so` built from it (arm64-v8a only) (committed in `android/app/src/main/jniLibs/arm64-v8a/`, sentinel `.seekserve_build_commit`). Since app commit `9cd94f4d` the APK holds arm64-v8a only, the only ABI with Tor (app BUG-19); the unused armeabi-v7a library left the app repository in app commit `380d934b` (2026-09-25, app WORK-10). Since app commit `9fa9ffc7` (2026-09-25) the app builds with AGP 9.0.1, Gradle 9.1.0 and Kotlin 2.3.20: nothing changes for this repository, whose Android library the app ships prebuilt |

Rule from the project owner (DEC-12 and DEC-27 in the app wiki): every repository the
work touches uses the same work branch as the app, today `dev`, and changes are pushed
there only when the owner asks; `main` is not touched and nothing is merged into it
unless asked. Never squash or rebase `dev`: the app pins commits by SHA.

## What the branch adds

| Commit | Change |
|---|---|
| `88fd8d6` | `pause_torrent` unsets `auto_managed` before `pause()`, `resume_torrent` sets it back: libtorrent's queue can no longer resume a paused torrent (app BUG-22) |
| `d8776ee` | WebTorrent off by default in `build-android.sh`, `build-ios.sh`, `dev-ios-sim.sh` (`SEEKSERVE_ENABLE_WEBTORRENT`, default `OFF`); `#if TORRENT_USE_RTC` in `session_manager.cpp`, because libtorrent defines the macro as 0 when WebTorrent is off (app SEC-18) |
| `38cf24a` | uTP off while the SOCKS5 proxy is on (Tor cannot carry UDP) |
| `782f2ee` | One limit on queued downloads: the C API reads `max_concurrent_torrents` into `SessionConfig::max_active_downloads`, set as libtorrent's `active_downloads` when > 0; `add_torrent` clears `auto_managed` and `paused`, so metadata and streaming start at once outside the queue, and `resume_torrent` (which sets `auto_managed`) is how a download joins the queue (app BUG-18, DEC-15) |
| `dba03c0` | `file_progress` added to the alert mask: `file_completed_alert` belongs to that category, so the `file_completed` event never fired and the offline cache never marked a file ready (app BUG-20) |
| `3bc9aa0` | `select_file` keeps subtitle files (`.srt`, `.vtt`, `.ass`, `.ssa`) at default priority, so they download with the selected video and the player reads them from disk; selecting a subtitle stopped the video (app BUG-24) |
| `847c1be` | New config key `ca_cert_file` (`SessionConfig::ca_cert_file`): a PEM file whose CAs are loaded, right after the session is created, into the SSL context libtorrent uses for HTTPS trackers and web seeds, on top of OpenSSL's defaults. On Android every HTTPS tracker failed with "certificate verify failed": OpenSSL (3.6.1 from vcpkg, `OPENSSLDIR` `/etc/ssl`) finds no CAs there, ignores `SSL_CERT_FILE` in app processes (`AT_SECURE` is 1), and cannot use Android's CA directory, whose file names are the old subject hash. libtorrent has no public hook: the context is reached through `session_interface::ssl_ctx()`. Three tests in `tests/integration/test_tracker_cas.cpp` with `fixtures/test_tracker_ca.pem`, the session on a proxy at port 0, so no network (app BUG-61, WORK-22) |
| `2f8509c` | New `ss_start_torrent` (`SeekServeEngine::start_torrent`): resumes a torrent without `auto_managed`, outside the download queue of `max_concurrent_torrents`, as `ss_add_torrent` adds it; `ss_resume_torrent` still puts it into the queue. Streaming from the app's search used the queue: it took the slot of a running Library download or waited for it (app BUG-63, WORK-23). Three tests in `tests/unit/test_capi.cpp`, the one that adds a torrent on a proxy at port 0. Published on 2026-09-25 with the owner's approval, before the app |
| `6c2decd` | A hybrid torrent (v1 and v2 hashes) added from a magnet with only its v1 hash (`btih`) keeps its add id when the metadata arrives. libtorrent then adds the v2 hash to the torrent (`extern/libtorrent/src/torrent.cpp:7882-7957`), and the alert handlers derived the id from the new hashes: catalog, `metadata_received` and `file_completed` events and streaming state went under the v2 id, while the app and every call use the v1 id `add_torrent` returned, so `has_metadata` stayed false and `list_files` failed (app BUG-71). First fix: the handlers mapped the hashes back to the add id; replaced by `d5923ea`. Tests in `tests/integration/test_hybrid_torrent.cpp` (`HybridTorrent.*`): a second libtorrent session seeds a hybrid torrent on 127.0.0.1 and the engine fetches it from a `btih` magnet with `x.pe`, through a SOCKS5 proxy in the test that connects only to 127.0.0.1 |
| `d5923ea` | After an independent review of `6c2decd`: `TorrentSessionManager::add_torrent` fixes the id from what the add knows (v2 hash if any, else v1) and stores a pointer to it in the torrent's `userdata` (`ids_`, never erased, so the pointer stays valid for late alerts); the alert handlers read it with `id_of(handle)` instead of hashing. It closes what the review found in `6c2decd`: the same `btih` magnet added again after the metadata returned the v2 id (libtorrent returns the existing torrent, now with both hashes), so the torrent got two ids, two cache rows and its later events under v2 (a second tap in the app's search); an `add_torrent_alert` handled before `handles_` had the id could be taken for a late alert of a removed torrent; metadata arriving before that could still go under v2. Two new tests (`V1MagnetAddedTwiceKeepsItsId`, which fails on `6c2decd`, and `TorrentFileAfterV1MagnetRemovalGetsItsMetadata`), and the test proxy shuts down both legs of a finished connection (a half-open leg made the seeder refuse the engine's next connection as a duplicate peer: 3 failures in about 150 runs) |
| `387c213` | Streams no longer end as if the video were over after a seek beyond the downloaded part (app BUG-83). The server waits for missing pieces in 500 ms slices for as long as the client stays (a closed peer is detected without blocking, bytes sent after the request are dropped; optional `ServerConfig::stall_timeout`, 0 = no limit) instead of cutting the 206 body short after `read_timeout`; chunks stop at piece boundaries. The newest GET of a stream owns its position and reports it to the scheduler when it starts, at each piece it enters and every 2 s while it waits, claim and report under one lock; HEAD no longer moves the scheduler. The engine sets one range callback in `start_server` that finds the torrent's own state (before, each `select_file` replaced it with the last selected scheduler, later freed); `on_tick` reconciles availability from `status(query_pieces)`; `ByteSource` notifies under its lock (lost wakeup) and never touches the mapper after `cancel()`; the lookahead starts only when the hot window needs pieces or deadlines are pending (a first deadline cancels every peer's requests). `stop()` shuts down live connection sockets and `stop_server` waits for them before the io_context can go, resetting the tick timer after the join. `flutter_seekserve_ui`: mpv `network-timeout` 0 for local streams (media_kit sets 5 s, and FFmpeg's reconnects then ended in EOF), a seek while buffering reopens the stream at the target keeping the chosen tracks, one seek where a drag ends with the target shown. Tests: `HttpRangeServerWaitTest` (12), `EngineStreamingTest`, new `ByteSourceTest` and `StreamingSchedulerTest` cases; the `HttpRangeServerTest`/`StressTest` fixtures now own the server through `shared_ptr` (16 tests failed with `bad_weak_ptr` since `efa0b44`). After an independent review, whose confirmed findings are all in this commit. Known gap, app BUG-88: `read_timeout`/`connection_timeout` still have no effect on blocked reads and writes |
| `4782c68` | `add_torrent` takes the id out of `removed_ids_` when the same torrent is added again, and registers the metadata the handle already has (in case the alert thread skipped its alert before the erase). Before, a torrent removed and added again in the same session never got its files in the catalog: `has_metadata` stayed false and `list_files` failed until the engine restarted; in the app a film removed from the Library and opened again waited 60 s and ended with "no peers found" (app BUG-68). The metadata and add_torrent alert handlers and the catch-up share `register_metadata`. New test `CApiTest.AddedAgainAfterRemovalGetsItsMetadata` (fails without the fix; proxy on port 0, no network). Published on 2026-09-25 with the owner's approval, before the app |
| `c93d7b3` | `flutter_seekserve_ui` only, no native code (app WORK-29, wave P2, step P2-01; app DEC-29): `SsVideoPlayer` gets `initialPosition` (applied at every open of the stream, also when a format is tried again with a new `Player`), `onPosition(position, duration)` at most once per `positionInterval` (5 s by default), once when the app goes to the background and once when the player is closed, and `onCompleted`. All optional: without them the player behaves as before. `SsPositionReporter` (`lib/src/player/ss_position_reporter.dart`, 7 tests in `test/position_reporter_test.dart`) decides when to speak: nothing before the length is known, and with a starting point, or a jump that opens the stream again, never the "0:00" a player says before it gets there. Pinned by the app since its commit `1817390e`, and published on 2026-10-10 with the owner's approval. The wiring inside the widget has no test here (it needs a real player): seen on the owner's Samsung A25 from the app on 2026-10-10, playing a downloaded file and a stream through Tor (the point said while playing, at the close and in the background; the start from a saved point) |
| `9978599` | `flutter_seekserve_ui` only, no native code (app WORK-29, wave P2, steps P2-02 and P2-03; app DEC-29, DEC-40): `SsVideoPlayer` gets `onBufferingChanged`, `onError` (not for the format errors it retries by itself, `ssIsFormatNotReady`), `pauseInBackground` (pauses when the app goes to the background, after the point is told), `strings` (`SsStrings`, every word the player shows, in English by default; the errors with a detail are builders, so an app can leave the stream's address out), `showStreamUrl` (the address in the error view, on by default) and `topBarActions` (the app's buttons in the top bar). All optional: without them the player behaves as before. With `2b8af2c` the player's own error (mpv's text, which can name the stream's address) goes through `SsStrings.playerError` too, and with `082a17b` the entry of the audio selector that turns the audio off is `SsStrings.audioOff` ("Off"), no longer "Track no", mpv's inner id (seen on the owner's phone as "Traccia no"). Tests: `test/player_strings_test.dart` (5; 44 in the package). Pinned by the app at `082a17b` since its commit `b96bbde1`, and published on 2026-10-10 with the owner's approval. The wiring inside the widget has no test here: the words, the audio selector and the pause in the background were seen on the owner's Samsung A25 from the app on 2026-10-10 |
| `d9b8226` | Native code, published on 2026-10-11 with the owner's approval (app DEC-178), pinned by the app at `bb1b283` since its commit `43b4ef63` (app WORK-29, wave P2b, step P2b-01; app DEC-41, DEC-176): the engine keeps each torrent's resume data. Off unless `SessionConfig::resume_data` is set (the C API key came with `ecdd17a`), so the app's library behaves as before. With it, the engine asks libtorrent for the resume data (`save_info_dict | only_if_modified`) when the metadata is known, when a file completes, on pause, every `resume_save_interval` (the timer runs with the servers) and at shutdown, where it waits up to `resume_flush_timeout`; `encode_resume_data` (`seekserve-core/src/resume_data.cpp`) takes the addresses of other peers out (`peers`, `banned_peers`) and the bytes go into a new `resume_data` BLOB column of the `torrents` table of `seekserve_cache.db`, added on open to a database written before. `save_torrent_uri` is an upsert now: `INSERT OR REPLACE` would have dropped the column every time a torrent the engine has is added again. A save that arrives after its torrent was removed writes nothing. At this commit nothing reads the data yet (that is `9c39444`). Tests: `OfflineCacheTest.ResumeData*` (7), `ResumeDataTest.*` (2), `EngineResumeDataTest.*` (6, offline like `EngineStreaming`), seen failing against empty implementations first |
| `9c39444` | Native code, published on 2026-10-11 (app DEC-178), pinned by the app at `bb1b283` since its commit `43b4ef63` (app WORK-29, wave P2b, step P2b-02): with `resume_data` set, the engine's constructor adds each saved torrent again from its resume data (`decode_resume_data`, `TorrentSessionManager::restore_torrent`) instead of from its URI: the files are known at once, without peers, and the pieces it has without a check of the disk. The torrent keeps the id of its row (not derived from the hashes again: app BUG-71), takes the engine's `save_path`, and comes back **paused and outside the queue** whatever it was doing. A torrent with no resume data yet, or with bytes that are not resume data, is restored from its URI as before, paused too (`AddTorrentParams::paused`). libtorrent itself re-checks a torrent whose resume data names a file that is gone. Without the setting: the URI only, started at once, as before. Tests: `ResumeDataTest.ReadsBackWhatItWrote`, `.UnreadableBytesAreNothing`, and 9 more `EngineResumeDataTest.*` (no metadata and no check after a restart, with the `.torrent` file removed and the data file overwritten; paused; streams what it has; a deleted file is not believed; damaged data falls back to the URI; three restarts in a row; the id kept), 7 seen failing against empty implementations first |
| `ecdd17a` | Published on 2026-10-11 (app DEC-178), pinned by the app at `bb1b283` since its commit `43b4ef63` (app WORK-29, wave P2b, step P2b-03): `ss_engine_create` reads `"resume_data"` (bool) and `"resume_save_interval"` (seconds), after the proxy keys so that a value of the wrong type cannot cost the proxy settings; documented in `seekserve_c.h`. The C API itself is unchanged (18 functions). **The C API tests are offline now**: before, every engine they created had no proxy and the cache database of the working directory, so it restored and started the fixture torrent (`Sintel_archive.torrent`, with trackers and web seeds) on the open network; `make_config` now sets a proxy on a closed port and a database inside the test's folder. `CreateWithNullConfig`, `CreateWithEmptyConfig` and `CreateWithInvalidJson` cannot take a config and still use the defaults: leave them out (see Tests). Tests: 4 new `CApiTest.*` through the C API (the torrent comes back without its `.torrent` file; nothing kept without the key; the timer saves a changed torrent again; without an interval it waits for an event), 3 seen failing before the keys were read |
| `f2fa8c5` | A test only, no change to the library (app WORK-29, wave P2b, step P2b-07): `EngineResumeDataTest.ATorrentStillWithoutMetadataComesBackTheSame`. On the owner's phone every torrent had resume data after the first run, also the ones added from a magnet whose metadata never arrived: their resume data carries no info dict, and the test fixes that such a torrent comes back with the same id, paused and still without metadata, instead of being dropped or taken for damaged data. It passed at once: `9c39444` already behaved so |
| `abae22e` | Tests only, no change to the library (app WORK-29, phase P4, step P4-01; app DEC-42, DEC-247): what selecting a file of a pack does today, before the engine learns to want two files of a torrent, the one a player reads and the one that downloads behind it. `tests/integration/test_multi_file_torrent.cpp` (`MultiFileTorrent.*`, 6 tests): a v1 torrent with three videos and a subtitle file, none a multiple of the piece size, so each file shares its last piece with the next; a second libtorrent session seeds it on 127.0.0.1 and the engine fetches it from a magnet through `LoopbackSocks5`, as `HybridTorrent` does. They fix: the selected file downloads alone, with the subtitles; a player gets its bytes from the stream server while it comes, the shared last piece too; the resume data keeps libtorrent's file priorities, while the engine's own selection is gone after a restart (`selected_file` is null). Two describe behaviour P4 has to deal with, and say so in a comment: `SelectingAnotherFileStopsTheFirst` (one selection per torrent: the first file is taken out, and the torrent reads as finished without it) and `WithEveryWantedPieceThereTheSeederIsLetGo` (see Open items). `LoopbackSocks5` moved from `test_hybrid_torrent.cpp` to `tests/integration/loopback_socks5.hpp`, with `limit_download(bytes_per_second)`: a test slows what the seeder sends to act while a download is under way, and keeps it slow until it selects a file, because until then every file of a new torrent is wanted and a small one would be complete first. Three deliberate faults in the engine (every file left wanted, subtitles left out, the reader mapped to another file) were each caught by these tests, then removed. Published on 2026-10-12 with the owner's approval (app DEC-248), before the app's wiki: tests only, so nothing waits for the phone (app DEC-246) |
| `fdc2087` | Native code, **not published**: by the owner's choice the commits that change the engine stay on this Mac until the app's phase P4 is tested on the phone (app DEC-246), and the app still pins `bb1b283` (app WORK-29, phase P4, step P4-02; app DEC-42, DEC-176, DEC-254). The status of a torrent (`get_status_json`, `ss_get_status`) gets `files`, once the metadata is known: one entry for each file libtorrent downloads (every file until one is selected, then the selected file and the subtitles), with `index`, `size`, `downloaded` (the bytes of that file that are there, the blocks of unfinished pieces included: `file_progress()`) and `complete` (every piece the file has a byte in is checked: `file_progress(piece_granularity)` against the size, which is what `file_completed` goes by). Additions only: every key that was there keeps its meaning, `ss_select_file` and the C API are unchanged (18 functions), and a client that does not read `files` sees nothing new. Why: `progress` counts the wanted pieces of the whole torrent, which is one file's number only while one file is wanted, and P4 will want two. `complete` does not come from the cache: `offline_ready` is its note that the selected file completed once, nothing takes it back, and it still says ready for a file deleted behind the engine. The new part catches libtorrent's exception for a torrent that went in between and leaves the key out (see Open items for the older call). Tests: `MultiFileTorrent.TheStatusSaysWhatEachWantedFileHas`, `.AFileThatWentIsNotWholeAnyMore` and more checks in `.ResumeDataKeepsTheFilePrioritiesNotTheSelection` (the list goes by libtorrent's priorities, so a restored torrent, paused and with no selection, still says what it has), seen failing first; two deliberate faults in the engine (every file listed; `complete` taken from the cache) were each caught, then removed. Not covered by a test: the blocks of an unfinished piece in `downloaded` (the fixture's pieces are one block long). `docs/C_API.md` describes `files`. The Dart client (`TorrentStatus` in `flutter_seekserve`) does not read it yet: that is the app's step P4-04 |

## Building the Android library

For the app, use its script: it builds from the pub-cache copy of the pinned
commit, strips, copies into `android/app/src/main/jniLibs/<abi>/` and writes
the sentinel.

```bash
dart scripts/build_utilities/commands/update_seekserve.dart --platform=android
```

(from the `obsidian-eclipse` checkout). Since app commit `9de05d68` it:
- uses NDK 28.2.13676358 only (`ANDROID_NDK_HOME` is honoured only if it points
  to that version) and vcpkg in `VCPKG_ROOT` or `~/vcpkg`;
- passes `SEEKSERVE_ENABLE_WEBTORRENT=OFF` and always strips with
  `--strip-unneeded`;
- writes a sentinel with this repo's commit, NDK, vcpkg commit, WebTorrent and
  strip settings;
- stops the app configuration if the build fails.

Since app commit `cfa11ed8` (app BUG-55) the committed library counts as
current when the sentinel records the pinned commit of this repo with the same
NDK, WebTorrent and strip settings and the `.so` exists for every requested
ABI. The script checks this first and looks for NDK and vcpkg only when it has
to rebuild, so CI and machines without vcpkg use the committed file; before,
a missing vcpkg stopped the app configuration, CI included. The vcpkg commit in
the sentinel is only a note: dependency versions come from the `vcpkg.json`
baseline. Since app commit `9cd94f4d` the script builds arm64-v8a only unless
`--abis=` says otherwise.

To publish a seekserve change to the app: commit and push here, move the
`flutter_seekserve` refs in the app's `pubspec.yaml` and
`plugins/torrent_streaming/pubspec.yaml`, `flutter pub get`, run the script,
commit the `jniLibs` changes.

To build by hand in this checkout instead:

```bash
ANDROID_NDK_HOME=~/Library/Android/sdk/ndk/28.2.13676358 VCPKG_ROOT=~/vcpkg ./scripts/build-android.sh
```

- vcpkg is a full clone in `~/vcpkg` (the `vcpkg.json` baseline needs its
  history). The first build compiles the vcpkg dependencies for every ABI in
  `SEEKSERVE_ANDROID_ABIS` (tens of minutes); later builds take about a minute.
  The app needs arm64-v8a only.
- Always pass `ANDROID_NDK_HOME`: the script alone picks the highest installed
  NDK, which may be a beta.
- Checks used on 2026-09-22: 17 `ss_*` exports (18 from `2f8509c`) (`llvm-nm -D`), 16 KB `LOAD`
  alignment on arm64 (`llvm-readelf -l`), no WebRTC strings.
- The libraries of `847c1be` in the app (app `ad596493`, 2026-09-23) were built
  this way, one ABI at a time (`SEEKSERVE_ANDROID_ABIS=arm64-v8a`, then
  `armeabi-v7a`) with `SEEKSERVE_ENABLE_WEBTORRENT=OFF`, stripped with the
  NDK's `llvm-strip --strip-unneeded` into the app's
  `android/app/src/main/jniLibs/<abi>/`, with the full commit as the first line
  of the app's sentinel: the app's script then reports them up to date. Same
  checks as above.
- For a quick rebuild after a local change (a temporary probe, for example),
  `cmake --build build/android-arm64-v8a` takes seconds: the script itself
  deletes the build folder first. A probe must never be committed.

## Tests on the owner's Mac

`flutter_seekserve_ui`: `flutter test` from its folder (39 passed at `c93d7b3`, 44 at `082a17b`). `flutter analyze` there reports one warning that is not about the code (`pubspec.yaml`: a publishable package with a `path` dependency), and both tools may rewrite `analysis_options.yaml` (they add `analyzer: exclude: build/**`): put it back with `git checkout --` before committing.

`build/debug` (triplet `arm64-osx`): `cmake --build build/debug`, then `build/debug/tests/seekserve-unit-tests` (138 passed on 2026-09-25, 147 since `387c213`, 156 since `d9b8226`, 158 since `9c39444`), `build/debug/tests/seekserve-capi-tests --gtest_filter='-CApiTest.CreateWithNullConfig:CApiTest.CreateWithEmptyConfig:CApiTest.CreateWithInvalidJson'` (30 since `ecdd17a`: **only with this filter the C API tests stay off the network**, and before `ecdd17a` none of them did; the three left out create an engine with the default config, which has no proxy and restores the torrents of `./seekserve_cache.db`, so run them from an empty folder if at all) and `build/debug/tests/seekserve-integration-tests --gtest_filter='HttpRangeServer*:StressTest.*:TrackerCas.*:HybridTorrent.*:EngineStreaming*:EngineResumeData*:MultiFileTorrent.*'` (61 since `fdc2087`, 59 since `abae22e`, with `MultiFileTorrent.*`, which stays on loopback as `HybridTorrent` does and takes about 30 seconds, most of them in a test that slows its download on purpose; 53 since `f2fa8c5`, 52 since `9c39444`, 43 at `d9b8226`, with `EngineResumeData*`; 37 since `387c213` without it: the server tests stay on loopback, `EngineStreaming` uses torrents without trackers behind a proxy on a closed port; before, `TrackerCas.*:HybridTorrent.*`, 3 + 5; `HybridTorrent` stays on loopback: its engine has DHT off and reaches the seeder only through the test's SOCKS5 proxy, which connects only to 127.0.0.1, and the seeder has DHT, LSD, UPnP and NAT-PMP off). Of the C API tests run only the four that stay off the network (proxy on port 0 or no torrent): `build/debug/tests/seekserve-capi-tests --gtest_filter='CApiTest.StartTorrent*:CApiTest.PauseAndStartTorrentOutsideQueue:CApiTest.AddedAgainAfterRemovalGetsItsMetadata'`; the others add the Sintel fixture without a proxy. The other integration tests create libtorrent sessions without a proxy, with the DHT on and announces to the trackers of the Sintel fixture: the app project does not allow BitTorrent traffic from the Mac, so do not run them there.

## Publishing order

The app pins this repository by commit and `flutter pub get` fetches it from GitHub: publish this branch **before** the app's. Until a pinned commit is published, the app resolves it only on the machine whose pub cache fetched it from this clone (app wiki, `toolchain-and-local-build`), as happened with `847c1be` until its publication on 2026-09-25.

**Not published (2026-10-13):** `fdc2087` and the notes above it, the first engine change of the app's phase P4. They stay on this Mac until the phase is tested on the owner's phone (app DEC-246); `origin/dev` is at `743fa9a` and the app pins `bb1b283`. Publishing needs the owner's word, this repository first.

## Open items

- Seen on 2026-10-13 while reading for the app's step P4-02 (app BUG-199), from the code, not reproduced:
  `get_status_json` checks `handle.is_valid()` and then asks libtorrent (`handle.status()`); if another
  thread removes the torrent in between and libtorrent has let it go, the call throws
  `invalid_torrent_handle`. Neither `ss_get_status` nor the app's JNI catches it, so it would end the
  process. The window is very short, and the app's service asks for the status and removes torrents
  on different threads. The `files` part added in `fdc2087` catches it and leaves the key out; the
  older call is as it was, waiting for the owner's decision.
- Also seen there: `offline_ready` in the status is the cache's flag (`cache_entries.offline_ready`),
  set by `file_completed` and never taken back, so it still says ready for a file deleted behind the
  engine and found missing (`MultiFileTorrent.AFileThatWentIsNotWholeAnyMore` shows it next to
  `complete`, which says no). The app does not read it (app at `d6e0e8fa`).
- Seen on 2026-10-12 while writing `MultiFileTorrent.*` (app step P4-01, app BUG-198), by a test with one
  seeder; never measured on a phone. When a torrent has every piece it wants, libtorrent closes its
  connections to the seeds (`close_redundant_connections`, on by default; `torrent::finished` in
  `extern/libtorrent/src/torrent.cpp`) and takes a peer back only `min_reconnect_time` (60 s) after it
  let it go (`peer_list.cpp`). The app's queue downloads the files of a pack one after the other, each
  the only wanted file of the torrent: when one completes the torrent is finished for a moment, and
  the next file may have nobody to ask until peers come back. In a swarm the peers that are not seeds
  stay, and the trackers may give others. The engine sets neither value (`session_manager.cpp`).
  To be decided in the app's phase P4, where the next file of a pack may start while a player holds
  the torrent.
- Also seen there: between the metadata and the first `select_file` every file of a new torrent is
  wanted and downloads (the tests keep their proxy slow until they select). The app selects on
  `metadata_received`; what arrives in between stays on disk as pieces of files nobody asked for.

- The download limit is not strict by design: with libtorrent's default
  `dont_count_slow_torrents`, a download below 2 KB/s for 60 s leaves its slot,
  so a dead torrent does not block the queue.

- The iOS framework in `flutter_seekserve` was not rebuilt: it still has
  WebTorrent. The app is Android only since 2026-09-25 (app DEC-25, DEC-26):
  it no longer calls the engine through FFI and uses `flutter_seekserve` only
  for its types, so nothing in the app depends on iOS here any more.
- libtorrent keeps a UDP socket on `0.0.0.0:6881` even with the proxy; it sends
  nothing directly (`udp_socket.cpp`), but it stays open.
- Torrent states in the status JSON are libtorrent's raw `state_t` values
  (1-based); the app's Android side read them 0-based until app commit
  `c2f7e952` (BUG-42).
- `build-android.sh` used alone still picks the NDK implicitly; the app's
  `update_seekserve.dart` pins it.
- The app's Android service used to be bind-only and died with its last
  client, so downloads stopped when the app was closed (app BUG-43). Since app
  `4e0c86e7` (the owner's DEC-18) it starts itself and outlives the app: a
  download goes on with the app closed, with its notification. If the app's
  main process dies, Tor dies with it, and the service pauses every torrent
  and stops, because torrents run only through Tor (app DEC-03). Since app
  `df2d52d6` it also keeps that process awake while torrents are active (app
  BUG-58) and handles Android 15's time limit for data-sync services (app
  BUG-09). Nothing changed here for any of it.
- At startup this engine restores every torrent in `seekserve_cache.db` as
  running. The app then reads that table (`torrent_id`, `uri`), matches each
  row to its Library by every hash either side knows, and brings each torrent
  back to the state the user left: completed ones leave the engine (no
  seeding), paused ones are paused again, downloading ones resume only with
  Tor ready, unknown ones are removed (files deleted only when certainly
  nobody's). Since app `bea21c7b` the decision is a tested function,
  `reconcileAction`. The engine still starts those torrents before the app has
  reconciled them, without connections until Tor listens (app SEC-19); a way
  to restore torrents paused would remove that window.
- Since app `e615ad1a` (app SEC-14, 2026-09-23) Tor picks its SOCKS port at
  runtime instead of the fixed 9050, which another app could take first. The
  app's Android service creates this engine with `proxy_enabled=true` and
  `proxy_port=0` (nothing can listen there, so every connection fails), then
  calls `ss_set_proxy` whenever Tor is ready, with Tor's current port, and
  with 0 while Tor is paused. The app now depends on two things here:
  - `ss_set_proxy` applying `proxy_type`, host and port at runtime through
    `set_proxy` → `apply_settings` (verified on the emulator: the engine
    connects to the new port, also after it changes);
  - libtorrent failing closed with proxy port 0: no special case for port 0,
    TCP to `127.0.0.1:0` is refused, UDP without a SOCKS5 association is
    dropped (`udp_socket.cpp`).
  `ss_set_proxy` treats a JSON without `"enabled"` as a disabled proxy
  (`ProxyConfig.enabled` defaults to `false`); since app `93e6a73b` the
  service always sends a JSON it builds itself, with `"enabled": true`. Keep
  these semantics if the C API changes. Nothing changed in this repository.
- HTTPS trackers and web seeds on Android need `ca_cert_file` (since `847c1be`); the app's service exports the system CAs (`AndroidCAStore`, `system:` aliases) to a PEM file and passes it. The iOS framework was not rebuilt and passes nothing: libtorrent looks for `/etc/ssl/cert.pem` there (the `__APPLE__` branch of `session_impl::start_session`), not checked on a device.
- `parse_config` in `seekserve-capi/src/seekserve_c.cpp` reads every key inside one
  `try`: a key of the wrong type (for example `ca_cert_file` or
  `max_concurrent_torrents` not being a string or a number) throws before the
  `proxy_*` keys are read, and the engine starts with the proxy **off**
  (`ProxyConfig.enabled` defaults to `false`). The app's service always sends
  the right types, so this cannot happen today; reading the proxy keys first,
  or each key on its own, would keep the engine fail-closed (found by an
  independent review of `847c1be` on 2026-09-23).
- Hybrid torrents added from a magnet with only `btih` (app BUG-71): fixed
  in `6c2decd` and `d5923ea`, reproduced first by `HybridTorrent.*` without
  leaving the Mac (no free hybrid torrent reachable through Tor was needed).
  Offline-cache rows that a hybrid got under its v2 id before the fix stay
  orphaned (the torrent is restored under v1); the app never saw those
  files as completed anyway, since their events carried the v2 id. Two things
  seen while writing those tests: with a SOCKS5 proxy for peer connections
  libtorrent 2.1 opens no TCP listen socket (`extern/libtorrent/src/session_impl.cpp:2037-2051`),
  so the engine never accepts incoming peers; and a seeder in `seed_mode`
  does not serve the v2 hashes a hybrid download asks for, so the test's
  seeder checks its file instead.
- Found by the review of `6c2decd`, older than it: `DELETE
  /api/torrents/{id}` in `control_api_server.cpp` removes the torrent
  without adding its id to `removed_ids_` (the app uses the C API, which
  goes through the engine); and a torrent removed and added again under the
  same id takes the late alerts of the removed torrent object as its own
  (`removed_ids_` loses the id at the new add, `4782c68`).
- The catch-up in `add_torrent` (`4782c68`) checks `has_metadata` and then
  registers without holding a lock against the alert thread, so a torrent
  added again could get two `metadata_received` events. The catalog
  (`on_metadata_received` ignores a known id) and the cache (`INSERT OR
  IGNORE`) take it; the app would select the file twice. It needs a
  `.torrent` with its metadata at add time: the app adds magnets only, so it
  cannot happen today.
