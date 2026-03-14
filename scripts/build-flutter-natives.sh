#!/usr/bin/env bash
# Build native libraries for the Flutter plugin (iOS + Android)
# and copy them to the correct plugin directories.
#
# Usage: ./scripts/build-flutter-natives.sh [ios|android|all]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PLUGIN_DIR="$PROJECT_DIR/flutter_seekserve"

TARGET="${1:-all}"

# ---- iOS ------------------------------------------------------------------
build_ios() {
    echo ""
    echo "========================================"
    echo "  Building iOS XCFramework"
    echo "========================================"
    bash "$SCRIPT_DIR/build-ios.sh"

    # Copy XCFramework to plugin
    local XCFW="$PROJECT_DIR/build/ios-xcframework/seekserve.xcframework"
    local DEST="$PLUGIN_DIR/ios/Frameworks"
    rm -rf "$DEST/seekserve.xcframework"
    mkdir -p "$DEST"
    cp -R "$XCFW" "$DEST/"
    echo "Copied XCFramework to $DEST/seekserve.xcframework"
}

# ---- Android --------------------------------------------------------------
build_android() {
    echo ""
    echo "========================================"
    echo "  Building Android .so"
    echo "========================================"
    bash "$SCRIPT_DIR/build-android.sh"

    # Find NDK strip tool (cross-platform: Linux CI and macOS)
    local NDK_DIR
    if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME" ]; then
        NDK_DIR="$ANDROID_NDK_HOME"
    elif [ -n "${ANDROID_HOME:-}" ]; then
        NDK_DIR="$(ls -d "$ANDROID_HOME/ndk/"* 2>/dev/null | sort -V | tail -1)"
    else
        NDK_DIR="$(ls -d "$HOME/Library/Android/sdk/ndk/"* 2>/dev/null | sort -V | tail -1)"
    fi
    local PREBUILT_DIR=""
    if [ -d "${NDK_DIR:-}/toolchains/llvm/prebuilt/linux-x86_64" ]; then
        PREBUILT_DIR="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64"
    elif [ -d "${NDK_DIR:-}/toolchains/llvm/prebuilt/darwin-x86_64" ]; then
        PREBUILT_DIR="$NDK_DIR/toolchains/llvm/prebuilt/darwin-x86_64"
    fi
    local STRIP_TOOL="${PREBUILT_DIR:+$PREBUILT_DIR/bin/llvm-strip}"

    # Copy .so files to plugin jniLibs
    local -a ABIS
    IFS=' ' read -r -a ABIS <<< "${SEEKSERVE_ANDROID_ABIS:-arm64-v8a armeabi-v7a}"
    for ABI in "${ABIS[@]}"; do
        local SRC="$PROJECT_DIR/build/android-$ABI/seekserve-capi/libseekserve.so"
        local DEST="$PLUGIN_DIR/android/src/main/jniLibs/$ABI"
        mkdir -p "$DEST"
        cp "$SRC" "$DEST/libseekserve.so"
        # Strip debug symbols for smaller APK
        if [ -x "$STRIP_TOOL" ]; then
            "$STRIP_TOOL" "$DEST/libseekserve.so"
        fi
        echo "Copied $ABI: $(du -h "$DEST/libseekserve.so" | cut -f1) (stripped)"
    done
}

# ---- Execute ---------------------------------------------------------------
case "$TARGET" in
    ios)     build_ios ;;
    android) build_android ;;
    all)     build_ios; build_android ;;
    *)
        echo "Usage: $0 [ios|android|all]"
        exit 1
        ;;
esac

echo ""
echo "========================================"
echo "  Summary"
echo "========================================"

if [ "$TARGET" = "ios" ] || [ "$TARGET" = "all" ]; then
    echo ""
    echo "iOS XCFramework:"
    du -sh "$PLUGIN_DIR/ios/Frameworks/seekserve.xcframework" 2>/dev/null || echo "  (not built)"
fi

if [ "$TARGET" = "android" ] || [ "$TARGET" = "all" ]; then
    echo ""
    echo "Android jniLibs:"
    IFS=' ' read -r -a SUMMARY_ABIS <<< "${SEEKSERVE_ANDROID_ABIS:-arm64-v8a armeabi-v7a}"
    for ABI in "${SUMMARY_ABIS[@]}"; do
        SO="$PLUGIN_DIR/android/src/main/jniLibs/$ABI/libseekserve.so"
        if [ -f "$SO" ]; then
            echo "  $ABI: $(du -h "$SO" | cut -f1)"
        else
            echo "  $ABI: (not built)"
        fi
    done
fi

echo ""
echo "Done."
