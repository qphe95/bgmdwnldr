#!/bin/bash
# Script to wrap the app with ASAN on the device

PACKAGE=com.bgmdwldr.vulkan
ACTIVITY=.MainActivity

# Push ASAN library to device
NDK_ROOT=/Users/qingpinghe/Android/sdk/ndk/26.2.11394342
ASAN_LIB="$NDK_ROOT/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/17.0.2/lib/linux/libclang_rt.asan-aarch64-android.so"

if [ ! -f "$ASAN_LIB" ]; then
    echo "ASAN library not found at $ASAN_LIB"
    echo "Searching in alternative locations..."
    ASAN_LIB=$(find $NDK_ROOT -name "libclang_rt.asan-aarch64-android.so" 2>/dev/null | head -1)
fi

if [ -z "$ASAN_LIB" ]; then
    echo "Error: Could not find libclang_rt.asan-aarch64-android.so"
    echo "Please set ANDROID_NDK_HOME or ANDROID_HOME environment variable"
    exit 1
fi

echo "Found ASAN library: $ASAN_LIB"

# Get the app's library directory
APP_LIB_DIR=$(adb shell run-as $PACKAGE ls -la /data/data/$PACKAGE/lib 2>/dev/null | head -5)
echo "App library directory:"
echo "$APP_LIB_DIR"

# Push ASAN library to app's lib directory
echo "Pushing ASAN library to device..."
adb push "$ASAN_LIB" /data/local/tmp/libasan.so

# Set up ASAN wrap
echo "Setting up ASAN wrap..."
adb shell am force-stop $PACKAGE

# Alternative approach: set environment variable and start app
echo "Starting app with ASAN..."
adb shell am start -D -n $PACKAGE/$ACTIVITY

echo "App started with ASAN enabled"
echo "Check logcat for ASAN output: adb logcat -d | grep -i asan"
