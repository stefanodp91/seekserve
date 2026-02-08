#!/usr/bin/env bash
# Cross-compile SeekServe C API for iOS (device arm64 + simulator arm64)
# Produces an XCFramework ready for the Flutter plugin.
# Requires: Xcode command-line tools + vcpkg
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
VCPKG_DIR="${VCPKG_ROOT:-$HOME/vcpkg}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
HEADER="$PROJECT_DIR/seekserve-capi/include/seekserve_c.h"
OUTPUT_DIR="$PROJECT_DIR/build/ios-xcframework"

echo "=== SeekServe iOS Build ==="
echo "vcpkg: $VCPKG_DIR"
echo "jobs:  $JOBS"

# ---- Helper: build one iOS slice -----------------------------------------
build_slice() {
    local PLATFORM=$1  # iphoneos | iphonesimulator
    local TRIPLET=$2   # arm64-ios | arm64-ios-simulator
    local BUILD_DIR="$PROJECT_DIR/build/ios-$PLATFORM"

    echo ""
    echo "--- Building for $PLATFORM ($TRIPLET) ---"

    rm -rf "$BUILD_DIR"

    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
        -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
        -DCMAKE_OSX_SYSROOT="$PLATFORM" \
        -DCMAKE_BUILD_TYPE=Release \
        -DSEEKSERVE_ENABLE_WEBTORRENT=ON \
        -DSEEKSERVE_BUILD_TESTS=OFF \
        -DSEEKSERVE_BUILD_DEMO=OFF \
        -DSEEKSERVE_BUILD_CAPI=ON \
        -DSEEKSERVE_CAPI_STATIC=ON

    cmake --build "$BUILD_DIR" -- -j "$JOBS"

    # Combine all static libraries into a single archive
    local COMBINED="$BUILD_DIR/libseekserve_combined.a"
    local LIBS=()

    # Collect all .a files from the build tree (excluding CMake internal stuff)
    while IFS= read -r -d '' lib; do
        LIBS+=("$lib")
    done < <(find "$BUILD_DIR" -name '*.a' \
        ! -path '*/CMakeFiles/*' \
        ! -name 'libgtest*' \
        -print0)

    echo "Combining ${#LIBS[@]} static libraries into $COMBINED"
    libtool -static -o "$COMBINED" "${LIBS[@]}"

    # Wrap in a .framework bundle (required by CocoaPods vendored_frameworks)
    local FW_DIR="$BUILD_DIR/seekserve.framework"
    rm -rf "$FW_DIR"
    mkdir -p "$FW_DIR/Headers" "$FW_DIR/Modules"
    cp "$COMBINED" "$FW_DIR/seekserve"
    cp "$HEADER" "$FW_DIR/Headers/"

    cat > "$FW_DIR/Modules/module.modulemap" <<'MMAP'
framework module seekserve {
    header "seekserve_c.h"
    export *
}
MMAP

    cat > "$FW_DIR/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>com.seekserve.sdk</string>
    <key>CFBundleName</key>
    <string>seekserve</string>
    <key>CFBundleVersion</key>
    <string>0.1.0</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
</dict>
</plist>
PLIST

    echo "Slice $PLATFORM: $(du -h "$COMBINED" | cut -f1)"
}

# ---- Build both slices ---------------------------------------------------
build_slice iphoneos arm64-ios
build_slice iphonesimulator arm64-ios-simulator

# ---- Create XCFramework --------------------------------------------------
DEVICE_FW="$PROJECT_DIR/build/ios-iphoneos/seekserve.framework"
SIM_FW="$PROJECT_DIR/build/ios-iphonesimulator/seekserve.framework"

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

echo ""
echo "--- Creating XCFramework ---"
xcodebuild -create-xcframework \
    -framework "$DEVICE_FW" \
    -framework "$SIM_FW" \
    -output "$OUTPUT_DIR/seekserve.xcframework"

echo ""
echo "=== iOS Build Complete ==="
echo "XCFramework: $OUTPUT_DIR/seekserve.xcframework"
du -sh "$OUTPUT_DIR/seekserve.xcframework"
