#!/usr/bin/env bash
# Cross-compile SeekServe C API for Android.
# Produces shared libraries for each ABI.
# Requires: Android NDK + vcpkg
#
# Configure target ABIs via SEEKSERVE_ANDROID_ABIS (space-separated).
# Default: arm64-v8a armeabi-v7a
# Example: SEEKSERVE_ANDROID_ABIS="arm64-v8a armeabi-v7a x86_64" ./scripts/build-android.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
VCPKG_DIR="${VCPKG_ROOT:-$HOME/vcpkg}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

# Find NDK
if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME" ]; then
    NDK_DIR="$ANDROID_NDK_HOME"
elif [ -n "${ANDROID_HOME:-}" ]; then
    # Pick the latest NDK version installed
    NDK_DIR="$(ls -d "$ANDROID_HOME/ndk/"* 2>/dev/null | sort -V | tail -1)"
else
    NDK_DIR="$HOME/Library/Android/sdk/ndk"
    NDK_DIR="$(ls -d "$NDK_DIR/"* 2>/dev/null | sort -V | tail -1)"
fi

if [ ! -d "$NDK_DIR" ]; then
    echo "ERROR: Android NDK not found."
    echo "Set ANDROID_NDK_HOME or install NDK via Android Studio."
    exit 1
fi

# Export for vcpkg's android toolchain detection
export ANDROID_NDK_HOME="$NDK_DIR"

echo "=== SeekServe Android Build ==="
echo "NDK:   $NDK_DIR"
echo "vcpkg: $VCPKG_DIR"
echo "jobs:  $JOBS"

IFS=' ' read -r -a ABIS <<< "${SEEKSERVE_ANDROID_ABIS:-arm64-v8a armeabi-v7a}"

abi_to_triplet() {
    case "$1" in
        arm64-v8a)   echo "arm64-android" ;;
        armeabi-v7a) echo "arm-neon-android" ;;
        x86_64)      echo "x64-android" ;;
        *) echo "ERROR: unknown ABI $1" >&2; exit 1 ;;
    esac
}

VCPKG_TRIPLETS=()
for ABI in "${ABIS[@]}"; do
    VCPKG_TRIPLETS+=("$(abi_to_triplet "$ABI")")
done

OVERLAY_TRIPLETS="$PROJECT_DIR/triplets"

build_abi() {
    local ABI=$1
    local TRIPLET=$2
    local BUILD_DIR="$PROJECT_DIR/build/android-$ABI"

    echo ""
    echo "--- Building for $ABI ($TRIPLET) ---"

    rm -rf "$BUILD_DIR"

    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
        -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$NDK_DIR/build/cmake/android.toolchain.cmake" \
        -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
        -DVCPKG_OVERLAY_TRIPLETS="$OVERLAY_TRIPLETS" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM=android-28 \
        -DCMAKE_BUILD_TYPE=Release \
        -DSEEKSERVE_ENABLE_WEBTORRENT=ON \
        -DSEEKSERVE_BUILD_TESTS=OFF \
        -DSEEKSERVE_BUILD_DEMO=OFF \
        -DSEEKSERVE_BUILD_CAPI=ON \
        -DSEEKSERVE_CAPI_STATIC=OFF

    cmake --build "$BUILD_DIR" -- -j "$JOBS"

    local LIB="$BUILD_DIR/seekserve-capi/libseekserve.so"
    if [ -f "$LIB" ]; then
        echo "$ABI: $(du -h "$LIB" | cut -f1)"
    else
        echo "ERROR: $LIB not found"
        exit 1
    fi
}

for i in "${!ABIS[@]}"; do
    build_abi "${ABIS[$i]}" "${VCPKG_TRIPLETS[$i]}"
done

echo ""
echo "=== Android Build Complete ==="
for ABI in "${ABIS[@]}"; do
    echo "  $ABI: $PROJECT_DIR/build/android-$ABI/seekserve-capi/libseekserve.so"
done
