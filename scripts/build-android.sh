#!/usr/bin/env bash
# Cross-compile SeekServe C API for Android (arm64-v8a)
# Requires: Android NDK + vcpkg
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
VCPKG_DIR="${VCPKG_ROOT:-$HOME/vcpkg}"
NDK_DIR="${ANDROID_NDK_HOME:-$HOME/Library/Android/sdk/ndk/26.1.10909125}"
BUILD_DIR="$PROJECT_DIR/build/android-arm64"

echo "=== SeekServe Android Build (arm64-v8a) ==="

if [ ! -d "$NDK_DIR" ]; then
    echo "ERROR: Android NDK not found at $NDK_DIR"
    echo "Set ANDROID_NDK_HOME or install NDK via Android Studio."
    exit 1
fi

rm -rf "$BUILD_DIR"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK_DIR/build/cmake/android.toolchain.cmake" \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$NDK_DIR/build/cmake/android.toolchain.cmake" \
    -DCMAKE_ANDROID_NDK="$NDK_DIR" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DCMAKE_BUILD_TYPE=Release \
    -DSEEKSERVE_ENABLE_WEBTORRENT=OFF \
    -DSEEKSERVE_BUILD_TESTS=OFF \
    -DSEEKSERVE_BUILD_DEMO=OFF \
    -DSEEKSERVE_BUILD_CAPI=ON

cmake --build "$BUILD_DIR" -- -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

echo ""
echo "=== Android Build Complete ==="
echo "Library: $BUILD_DIR/seekserve-capi/libseekserve.so"
