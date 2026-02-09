# Agent Instructions for bgmdwnldr

## Testing Procedures

### Prerequisites
- Android device connected via ADB
- App installed (via `./rebuild.sh`)

### Proper App Launch Sequence

**IMPORTANT:** Always verify the correct app is running before testing:

```bash
# 1. Kill any running instances
adb shell am force-stop com.bgmdwldr.vulkan
adb shell am force-stop com.oneplus.backuprestore  # Clone Phone often interferes

# 2. Clear logs
adb logcat -c

# 3. Start the app explicitly
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# 4. Verify it's running
adb shell pidof com.bgmdwldr.vulkan

# 5. Check focused activity (should show bgmdwldr.vulkan)
adb shell dumpsys activity activities | grep mFocusedApp
```

### Entering Test URL

```bash
# Tap input field (center of text box)
adb shell input tap 540 600

# Clear existing text
adb shell input keyevent --longpress 67 67 67 67 67 67 67 67

# Enter URL
adb shell input text "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

# Submit
adb shell input keyevent 66

# Wait for processing (10-15 seconds)
sleep 12
```

### Viewing Results

```bash
# Get app PID
APP_PID=$(adb shell pidof com.bgmdwldr.vulkan)

# View js_quickjs logs
adb logcat -d --pid=$APP_PID | grep -E "js_quickjs:|HtmlExtract:|Executed|Captured"
```

## Using AddressSanitizer (ASAN)

ASAN is useful for detecting memory errors like use-after-free, buffer overflows, and memory leaks.

### Setup ASAN

```bash
# Enable ASAN in the build
./setup_asan.sh

# Rebuild the app with ASAN enabled
./rebuild.sh
```

### Run with ASAN

```bash
# Wrap the app with ASAN libraries on device
./wrap_with_asan.sh
```

### Alternative: Manual ASAN Setup

If the wrap script doesn't work, you can manually set up ASAN:

```bash
# Find your ASAN library
NDK_ROOT=/Users/qingpinghe/Android/sdk/ndk/26.2.11394342
ASAN_LIB="$NDK_ROOT/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/17.0.2/lib/linux/libclang_rt.asan-aarch64-android.so"

# If not found, search for it
find $NDK_ROOT -name "libclang_rt.asan-aarch64-android.so" 2>/dev/null

# Push ASAN library to device
adb push "$ASAN_LIB" /data/local/tmp/libasan.so

# Set environment variable and run app
adb shell am force-stop com.bgmdwldr.vulkan
adb shell "ASAN_OPTIONS=detect_leaks=0" am start -n com.bgmdwldr.vulkan/.MainActivity
```

### Viewing ASAN Output

```bash
# View ASAN errors in logcat
adb logcat -d | grep -i asan

# Or monitor in real-time
adb logcat | grep -i asan
```

### Disabling ASAN

```bash
# Revert to normal build (edit Android.mk manually or restore from git)
git checkout app/src/main/cpp/Android.mk

# Rebuild without ASAN
./rebuild.sh
```
