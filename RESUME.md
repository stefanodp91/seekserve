# Resuming work on `chore/resume-build-remove-auth`

Handoff notes for this branch. The full project context (decisions, security
register, roadmap) lives in the app repository, `obsidian-eclipse`, in
`docs/wiki/analyses/resume-guide.md`.

## Where things are

| What | Where |
|---|---|
| This repo | `/Users/stefano/Workspace/seekserve`, remote `https://github.com/stefanodp91/seekserve` |
| Branch | `chore/resume-build-remove-auth`, created from `feature/native-jackett-engine` at `403bef8` and published (last push 2026-09-22, with `782f2ee` and this note). `feature/native-jackett-engine` and `main` are untouched |
| App | `obsidian-eclipse` pins `782f2ee` for `flutter_seekserve` and `flutter_seekserve_ui` (since 2026-09-22, for its BUG-18 download queue; before that `38cf24a`), and ships an Android `libseekserve.so` built from it (committed in `android/app/src/main/jniLibs/<abi>/`, sentinel `.seekserve_build_commit`) |

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

## Building the Android library

For the app, use its script: it builds from the pub-cache copy of the pinned
commit, strips, copies into `android/app/src/main/jniLibs/<abi>/` and writes
the sentinel.

```bash
dart scripts/build_utilities/commands/update_seekserve.dart --platform=android
```

(from the `obsidian-eclipse` checkout). Since app commit `9de05d68` it:
- uses NDK 28.2.13676358 only (`ANDROID_NDK_HOME` is honoured only if it points
  to that version) and requires vcpkg in `VCPKG_ROOT` or `~/vcpkg`;
- passes `SEEKSERVE_ENABLE_WEBTORRENT=OFF` and always strips with
  `--strip-unneeded`;
- writes a sentinel with this repo's commit, NDK, vcpkg commit, WebTorrent and
  strip settings, and rebuilds when any of them changes;
- stops the app configuration if the build fails.

To publish a seekserve change to the app: commit and push here, move the
`flutter_seekserve` refs in the app's `pubspec.yaml` and
`plugins/torrent_streaming/pubspec.yaml`, `flutter pub get`, run the script,
commit the `jniLibs` changes.

To build by hand in this checkout instead:

```bash
ANDROID_NDK_HOME=~/Library/Android/sdk/ndk/28.2.13676358 VCPKG_ROOT=~/vcpkg ./scripts/build-android.sh
```

- vcpkg is a full clone in `~/vcpkg` (the `vcpkg.json` baseline needs its
  history). The first build compiles the vcpkg dependencies for both ABIs
  (tens of minutes); later builds take about a minute.
- Always pass `ANDROID_NDK_HOME`: the script alone picks the highest installed
  NDK, which may be a beta.
- Checks used on 2026-09-22: 17 `ss_*` exports (`llvm-nm -D`), 16 KB `LOAD`
  alignment on arm64 (`llvm-readelf -l`), no WebRTC strings.

## Open items

- The download limit is not strict by design: with libtorrent's default
  `dont_count_slow_torrents`, a download below 2 KB/s for 60 s leaves its slot,
  so a dead torrent does not block the queue.

- The iOS framework in `flutter_seekserve` was not rebuilt: it still has
  WebTorrent.
- libtorrent keeps a UDP socket on `0.0.0.0:6881` even with the proxy; it sends
  nothing directly (`udp_socket.cpp`), but it stays open.
- Torrent states in the status JSON are libtorrent's raw `state_t` values
  (1-based); the app's Android side read them 0-based until app commit
  `c2f7e952` (BUG-42).
- `build-android.sh` used alone still picks the NDK implicitly; the app's
  `update_seekserve.dart` pins it.
- The app's Android service is bind-only and dies with its last client, so
  downloads stop when the app's Flutter engine detaches (app BUG-43, WORK-18);
  nothing to change here for that.
