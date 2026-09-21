# Resuming work on `chore/resume-build-remove-auth`

Handoff notes for this branch. The full project context (decisions, security
register, roadmap) lives in the app repository, `obsidian-eclipse`, in
`docs/wiki/analyses/resume-guide.md`.

## Where things are

| What | Where |
|---|---|
| This repo | `/Users/stefano/Workspace/seekserve`, remote `https://github.com/stefanodp91/seekserve` |
| Branch | `chore/resume-build-remove-auth`, created from `feature/native-jackett-engine` at `403bef8` and published. `feature/native-jackett-engine` and `main` are untouched |
| App | `obsidian-eclipse` pins `38cf24a` for `flutter_seekserve` and `flutter_seekserve_ui`, and ships an Android `libseekserve.so` built from it (committed in `android/app/src/main/jniLibs/<abi>/`, sentinel `.seekserve_build_commit`) |

Rule from the project owner (DEC-12 in the app wiki): every repository the
work touches uses a branch named like the app's work branch; nothing is
merged into `main` unless asked.

## What the branch adds

| Commit | Change |
|---|---|
| `88fd8d6` | `pause_torrent` unsets `auto_managed` before `pause()`, `resume_torrent` sets it back: libtorrent's queue can no longer resume a paused torrent (app BUG-22) |
| `d8776ee` | WebTorrent off by default in `build-android.sh`, `build-ios.sh`, `dev-ios-sim.sh` (`SEEKSERVE_ENABLE_WEBTORRENT`, default `OFF`); `#if TORRENT_USE_RTC` in `session_manager.cpp`, because libtorrent defines the macro as 0 when WebTorrent is off (app SEC-18) |
| `38cf24a` | uTP off while the SOCKS5 proxy is on (Tor cannot carry UDP) |

## Building the Android library

```bash
ANDROID_NDK_HOME=~/Library/Android/sdk/ndk/28.2.13676358 VCPKG_ROOT=~/vcpkg ./scripts/build-android.sh
```

- vcpkg is a full clone in `~/vcpkg` (the `vcpkg.json` baseline needs its
  history). The first build compiles the vcpkg dependencies for both ABIs
  (tens of minutes); later builds take about a minute.
- Pass the NDK explicitly: the script otherwise picks the highest installed
  version, which may be a beta.
- For the app: strip with the NDK's `llvm-strip --strip-unneeded`, copy to
  `android/app/src/main/jniLibs/<abi>/libseekserve.so`, write this repo's
  commit into `.seekserve_build_commit`, and move the `flutter_seekserve` refs
  in the app's `pubspec.yaml` and `plugins/torrent_streaming/pubspec.yaml`.
- Checks used on 2026-09-22: 17 `ss_*` exports (`llvm-nm -D`), 16 KB `LOAD`
  alignment on arm64 (`llvm-readelf -l`), no WebRTC strings.

## Open items

- The iOS framework in `flutter_seekserve` was not rebuilt: it still has
  WebTorrent.
- libtorrent keeps a UDP socket on `0.0.0.0:6881` even with the proxy; it sends
  nothing directly (`udp_socket.cpp`), but it stays open.
- Torrent states in the status JSON are libtorrent's raw `state_t` values
  (1-based); the app's Android side read them 0-based until app commit
  `c2f7e952` (BUG-42).
- `build-android.sh` still picks the NDK implicitly and `update_seekserve.dart`
  in the app does not fail hard (app WORK-09, step C).
