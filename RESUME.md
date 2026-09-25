# Resuming work on `chore/resume-build-remove-auth`

Handoff notes for this branch. The full project context (decisions, security
register, roadmap) lives in the app repository, `obsidian-eclipse`, in
`docs/wiki/analyses/resume-guide.md`.

## Where things are

| What | Where |
|---|---|
| This repo | `/Users/stefano/Workspace/seekserve`, remote `https://github.com/stefanodp91/seekserve` |
| Branch | `chore/resume-build-remove-auth`, created from `feature/native-jackett-engine` at `403bef8` and published (pushes on 2026-09-22: `dba03c0`, `3bc9aa0` and these notes, last updated for app BUG-19 and BUG-55). The notes of 2026-09-23 (app BUG-43, the app's reconciliation) were published that day with the owner's approval, together with the app's WORK-07, and so was `f49afe4` with the app's WORK-15. Then `847c1be` (`ca_cert_file`, app BUG-61) and the notes up to this one were published on 2026-09-25 with the owner's approval ("pubblica tutto"), before the app's WORK-21 and WORK-22. Also on 2026-09-25, each time with the owner's approval and before the app: `2f8509c` (`ss_start_torrent`, app BUG-63) with its notes, then `4782c68` (a torrent removed and added again gets its metadata, app BUG-68) with the notes up to `b767dcb`. On 2026-09-25 evening `6c2decd` (a hybrid torrent keeps its add id when its metadata arrives, app BUG-71), then, after an independent review, `d5923ea` (the add id travels in the torrent's userdata), with these notes: **not published yet**; publish them before the app, which pins `d5923ea`. `feature/native-jackett-engine` and `main` are untouched |
| App | `obsidian-eclipse` pins `d5923ea` for `flutter_seekserve` and `flutter_seekserve_ui` since app commit `d5c3378f` (2026-09-25, not published yet: `flutter pub get` resolves it only on this Mac until this branch is pushed), `6c2decd` since `4239968b` (2026-09-25, not published), `4782c68` since `b0e095eb` (2026-09-25), `2f8509c` since `ed706005` (2026-09-25, published first, then the app), `847c1be` since `ad596493` (2026-09-23; before that `3bc9aa0`, `782f2ee` and `38cf24a`), and ships an Android `libseekserve.so` built from it (arm64-v8a only) (committed in `android/app/src/main/jniLibs/arm64-v8a/`, sentinel `.seekserve_build_commit`). Since app commit `9cd94f4d` the APK holds arm64-v8a only, the only ABI with Tor (app BUG-19); the unused armeabi-v7a library left the app repository in app commit `380d934b` (2026-09-25, app WORK-10). Since app commit `9fa9ffc7` (2026-09-25) the app builds with AGP 9.0.1, Gradle 9.1.0 and Kotlin 2.3.20: nothing changes for this repository, whose Android library the app ships prebuilt |

Rule from the project owner (DEC-12 in the app wiki): every repository the
work touches uses a branch named like the app's work branch; nothing is
merged into `main` unless asked.

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
| `4782c68` | `add_torrent` takes the id out of `removed_ids_` when the same torrent is added again, and registers the metadata the handle already has (in case the alert thread skipped its alert before the erase). Before, a torrent removed and added again in the same session never got its files in the catalog: `has_metadata` stayed false and `list_files` failed until the engine restarted; in the app a film removed from the Library and opened again waited 60 s and ended with "no peers found" (app BUG-68). The metadata and add_torrent alert handlers and the catch-up share `register_metadata`. New test `CApiTest.AddedAgainAfterRemovalGetsItsMetadata` (fails without the fix; proxy on port 0, no network). Published on 2026-09-25 with the owner's approval, before the app |

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

`build/debug` (triplet `arm64-osx`): `cmake --build build/debug`, then `build/debug/tests/seekserve-unit-tests` (138 passed on 2026-09-25) and `build/debug/tests/seekserve-integration-tests --gtest_filter='TrackerCas.*:HybridTorrent.*'` (3 + 5; `HybridTorrent` stays on loopback: its engine has DHT off and reaches the seeder only through the test's SOCKS5 proxy, which connects only to 127.0.0.1, and the seeder has DHT, LSD, UPnP and NAT-PMP off). Of the C API tests run only the four that stay off the network (proxy on port 0 or no torrent): `build/debug/tests/seekserve-capi-tests --gtest_filter='CApiTest.StartTorrent*:CApiTest.PauseAndStartTorrentOutsideQueue:CApiTest.AddedAgainAfterRemovalGetsItsMetadata'`; the others add the Sintel fixture without a proxy. The other integration tests create libtorrent sessions without a proxy, with the DHT on and announces to the trackers of the Sintel fixture: the app project does not allow BitTorrent traffic from the Mac, so do not run them there.

## Publishing order

The app pins this repository by commit and `flutter pub get` fetches it from GitHub: publish this branch **before** the app's. Until a pinned commit is published, the app resolves it only on the machine whose pub cache fetched it from this clone (app wiki, `toolchain-and-local-build`), as happened with `847c1be` until its publication on 2026-09-25.

## Open items

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
