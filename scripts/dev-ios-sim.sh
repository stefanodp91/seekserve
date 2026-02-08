#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# dev-ios-sim.sh — One-command development loop for iOS Simulator
#
# Performs an INCREMENTAL build of the native C API, repackages the
# XCFramework, builds the Flutter example app, and installs + launches
# it on the booted iOS Simulator.
#
# Usage:
#   ./scripts/dev-ios-sim.sh              # incremental build + run
#   ./scripts/dev-ios-sim.sh --full       # clean rebuild of native libs
#   ./scripts/dev-ios-sim.sh --native     # rebuild native libs only (no Flutter)
#   ./scripts/dev-ios-sim.sh --flutter    # Flutter build + install only (skip native)
#   ./scripts/dev-ios-sim.sh --run        # install + launch only (skip build)
#
# Requirements: Xcode CLI tools, vcpkg, Flutter SDK, booted iOS Simulator
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
VCPKG_DIR="${VCPKG_ROOT:-$HOME/vcpkg}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
PLUGIN_DIR="$PROJECT_DIR/flutter_seekserve"
EXAMPLE_DIR="$PLUGIN_DIR/example"
HEADER="$PROJECT_DIR/seekserve-capi/include/seekserve_c.h"
BUNDLE_ID="com.example.flutterSeekserveExample"

# Build directories
SIM_BUILD="$PROJECT_DIR/build/ios-iphonesimulator"
DEV_BUILD="$PROJECT_DIR/build/ios-iphoneos"
XCF_OUTPUT="$PROJECT_DIR/build/ios-xcframework"

# Parse flags
FULL_REBUILD=false
NATIVE_ONLY=false
FLUTTER_ONLY=false
RUN_ONLY=false

for arg in "$@"; do
    case "$arg" in
        --full)     FULL_REBUILD=true ;;
        --native)   NATIVE_ONLY=true ;;
        --flutter)  FLUTTER_ONLY=true ;;
        --run)      RUN_ONLY=true ;;
        -h|--help)
            head -14 "$0" | tail -12
            exit 0
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Detect booted simulator
# ---------------------------------------------------------------------------
detect_simulator() {
    local SIM_ID
    SIM_ID=$(xcrun simctl list devices booted -j 2>/dev/null \
        | python3 -c "
import json, sys
data = json.load(sys.stdin)
for runtime, devices in data.get('devices', {}).items():
    for d in devices:
        if d.get('state') == 'Booted':
            print(d['udid'])
            sys.exit(0)
sys.exit(1)
" 2>/dev/null) || true

    if [ -z "$SIM_ID" ]; then
        echo "ERROR: No booted iOS Simulator found."
        echo "  Boot one with: xcrun simctl boot <DEVICE_ID>"
        echo "  Or open Simulator.app from Xcode."
        exit 1
    fi
    echo "$SIM_ID"
}

SIM_UDID="$(detect_simulator)"
SIM_NAME=$(xcrun simctl list devices booted | grep "$SIM_UDID" | sed 's/ (Booted)//;s/^[[:space:]]*//' | head -1)
echo "=== SeekServe iOS Simulator Dev Build ==="
echo "Simulator: $SIM_NAME ($SIM_UDID)"
echo ""

# ---------------------------------------------------------------------------
# Step 1: Build native C API for simulator (incremental)
# ---------------------------------------------------------------------------
build_native() {
    echo "--- [1/4] Building native library (simulator arm64) ---"

    if $FULL_REBUILD || [ ! -f "$SIM_BUILD/build.ninja" ]; then
        echo "  Configuring CMake (full)..."
        rm -rf "$SIM_BUILD"

        cmake -S "$PROJECT_DIR" -B "$SIM_BUILD" \
            -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
            -DVCPKG_TARGET_TRIPLET=arm64-ios-simulator \
            -DCMAKE_SYSTEM_NAME=iOS \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
            -DCMAKE_OSX_SYSROOT=iphonesimulator \
            -DCMAKE_BUILD_TYPE=Debug \
            -DSEEKSERVE_ENABLE_WEBTORRENT=ON \
            -DSEEKSERVE_BUILD_TESTS=OFF \
            -DSEEKSERVE_BUILD_DEMO=OFF \
            -DSEEKSERVE_BUILD_CAPI=ON \
            -DSEEKSERVE_CAPI_STATIC=ON
    else
        echo "  Using existing CMake config (incremental)..."
    fi

    cmake --build "$SIM_BUILD" -- -j "$JOBS"

    # Combine all static libraries into a single archive
    local COMBINED="$SIM_BUILD/libseekserve_combined.a"
    local LIBS=()
    while IFS= read -r -d '' lib; do
        LIBS+=("$lib")
    done < <(find "$SIM_BUILD" -name '*.a' \
        ! -path '*/CMakeFiles/*' \
        ! -name 'libgtest*' \
        -print0)

    echo "  Combining ${#LIBS[@]} static libraries..."
    libtool -static -o "$COMBINED" "${LIBS[@]}" 2>/dev/null

    # Wrap in .framework bundle
    local FW_DIR="$SIM_BUILD/seekserve.framework"
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

    echo "  Simulator framework: $(du -h "$COMBINED" | cut -f1)"
}

# ---------------------------------------------------------------------------
# Step 2: Create XCFramework and copy to Flutter plugin
# ---------------------------------------------------------------------------
package_xcframework() {
    echo ""
    echo "--- [2/4] Packaging XCFramework ---"

    local DEVICE_FW="$DEV_BUILD/seekserve.framework"
    local SIM_FW="$SIM_BUILD/seekserve.framework"

    # For dev iteration, if the device build doesn't exist yet,
    # create a dummy framework so xcodebuild -create-xcframework works.
    # The app only runs on simulator during development.
    if [ ! -d "$DEVICE_FW" ]; then
        echo "  Device framework not found — building device slice first..."
        echo "  (This is a one-time cost. Use --full to rebuild from scratch.)"

        rm -rf "$DEV_BUILD"
        cmake -S "$PROJECT_DIR" -B "$DEV_BUILD" \
            -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
            -DVCPKG_TARGET_TRIPLET=arm64-ios \
            -DCMAKE_SYSTEM_NAME=iOS \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
            -DCMAKE_OSX_SYSROOT=iphoneos \
            -DCMAKE_BUILD_TYPE=Release \
            -DSEEKSERVE_ENABLE_WEBTORRENT=ON \
            -DSEEKSERVE_BUILD_TESTS=OFF \
            -DSEEKSERVE_BUILD_DEMO=OFF \
            -DSEEKSERVE_BUILD_CAPI=ON \
            -DSEEKSERVE_CAPI_STATIC=ON

        cmake --build "$DEV_BUILD" -- -j "$JOBS"

        local DEV_COMBINED="$DEV_BUILD/libseekserve_combined.a"
        local DEV_LIBS=()
        while IFS= read -r -d '' lib; do
            DEV_LIBS+=("$lib")
        done < <(find "$DEV_BUILD" -name '*.a' \
            ! -path '*/CMakeFiles/*' \
            ! -name 'libgtest*' \
            -print0)

        libtool -static -o "$DEV_COMBINED" "${DEV_LIBS[@]}" 2>/dev/null

        mkdir -p "$DEVICE_FW/Headers" "$DEVICE_FW/Modules"
        cp "$DEV_COMBINED" "$DEVICE_FW/seekserve"
        cp "$HEADER" "$DEVICE_FW/Headers/"
        cat > "$DEVICE_FW/Modules/module.modulemap" <<'MMAP'
framework module seekserve {
    header "seekserve_c.h"
    export *
}
MMAP
        cat > "$DEVICE_FW/Info.plist" <<PLIST
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
    fi

    rm -rf "$XCF_OUTPUT"
    mkdir -p "$XCF_OUTPUT"
    xcodebuild -create-xcframework \
        -framework "$DEVICE_FW" \
        -framework "$SIM_FW" \
        -output "$XCF_OUTPUT/seekserve.xcframework" 2>&1 | grep -v "^$"

    # Copy to Flutter plugin
    rm -rf "$PLUGIN_DIR/ios/Frameworks/seekserve.xcframework"
    mkdir -p "$PLUGIN_DIR/ios/Frameworks"
    cp -R "$XCF_OUTPUT/seekserve.xcframework" "$PLUGIN_DIR/ios/Frameworks/"
    echo "  XCFramework installed to plugin"
}

# ---------------------------------------------------------------------------
# Step 3: Build Flutter app
# ---------------------------------------------------------------------------
build_flutter() {
    echo ""
    echo "--- [3/4] Building Flutter app ---"
    cd "$EXAMPLE_DIR"
    flutter clean > /dev/null 2>&1
    flutter pub get > /dev/null 2>&1
    flutter build ios --simulator --debug 2>&1 | grep -E "(Running|Xcode|Built|Error|error:)" || true
    cd "$PROJECT_DIR"
}

# ---------------------------------------------------------------------------
# Step 4: Install and launch on simulator
# ---------------------------------------------------------------------------
install_and_run() {
    echo ""
    echo "--- [4/4] Installing and launching on simulator ---"

    local APP_PATH="$EXAMPLE_DIR/build/ios/iphonesimulator/Runner.app"
    if [ ! -d "$APP_PATH" ]; then
        echo "ERROR: Runner.app not found at $APP_PATH"
        echo "  Run the full build first: $0"
        exit 1
    fi

    # Kill existing instance if running
    xcrun simctl terminate "$SIM_UDID" "$BUNDLE_ID" 2>/dev/null || true

    xcrun simctl install "$SIM_UDID" "$APP_PATH"
    xcrun simctl launch "$SIM_UDID" "$BUNDLE_ID"
    echo ""
    echo "=== App launched on simulator ==="
    echo "  To take screenshot:  xcrun simctl io $SIM_UDID screenshot /tmp/ss.png"
    echo "  To view logs:        xcrun simctl spawn $SIM_UDID log stream --predicate 'processImagePath ENDSWITH \"Runner\"' --level debug"
}

# ---------------------------------------------------------------------------
# Execute requested phases
# ---------------------------------------------------------------------------
if $RUN_ONLY; then
    install_and_run
elif $NATIVE_ONLY; then
    build_native
    package_xcframework
elif $FLUTTER_ONLY; then
    build_flutter
    install_and_run
else
    build_native
    package_xcframework
    build_flutter
    install_and_run
fi

echo ""
echo "Done."
