#!/usr/bin/env bash
# Cross-compile SeekServe C API for iOS (arm64)
# Requires: Xcode + vcpkg with arm64-ios triplet
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
VCPKG_DIR="${VCPKG_ROOT:-$HOME/vcpkg}"
BUILD_DIR="$PROJECT_DIR/build/ios-arm64"

echo "=== SeekServe iOS Build (arm64) ==="

# Ensure vcpkg has the iOS triplet
if [ ! -d "$VCPKG_DIR/triplets/community" ] || ! ls "$VCPKG_DIR/triplets/community/arm64-ios.cmake" &>/dev/null; then
    echo "WARNING: vcpkg arm64-ios triplet may not be available."
    echo "You may need to create a custom triplet."
fi

rm -rf "$BUILD_DIR"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=arm64-ios \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_BUILD_TYPE=Release \
    -DSEEKSERVE_ENABLE_WEBTORRENT=OFF \
    -DSEEKSERVE_BUILD_TESTS=OFF \
    -DSEEKSERVE_BUILD_DEMO=OFF \
    -DSEEKSERVE_BUILD_CAPI=ON

cmake --build "$BUILD_DIR" -- -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

echo ""
echo "=== iOS Build Complete ==="
echo "Library: $BUILD_DIR/seekserve-capi/libseekserve.dylib"
