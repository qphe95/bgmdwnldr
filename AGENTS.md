# Agent Instructions for bgmdwnldr

## Testing Procedures

### Prerequisites
- Android device connected via ADB (or emulator running)
- App installed (via `./rebuild.sh`)

## Emulator Management

### Starting the Emulator

Use the provided script to start the emulator with UI:

```bash
# Start the default bgmdwldr_avd emulator
./scripts/start-emulator.sh

# Or specify a different AVD
./scripts/start-emulator.sh Pixel_3a_API_34_extension_level_7_arm64-v8a
```

This script will:
1. Auto-detect Android SDK location
2. Kill any existing emulator processes (to prevent multiple emulators)
3. Start the emulator with UI
4. Wait for boot completion

### Stopping the Emulator

```bash
./scripts/stop-emulator.sh
```

This will gracefully shut down the emulator and clean up processes.

### Why Manage Emulators?

**IMPORTANT:** Running multiple emulators simultaneously causes:
- Port conflicts (ADB, console, etc.)
- High CPU/memory usage
- Confusion about which device to target

Always ensure only ONE emulator is running at a time.

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

## Debugging

### LLDB Setup

To use LLDB with the emulator, you need to:

1. **Run ADB as root** (required for attaching to processes):
```bash
adb root
```

2. **Push and start lldb-server**:
```bash
NDK=$ANDROID_SDK_ROOT/ndk/26.2.11394342
adb push "$NDK/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/17.0.2/lib/linux/aarch64/lldb-server" /data/local/tmp/
adb shell chmod +x /data/local/tmp/lldb-server
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &
adb forward tcp:5039 tcp:5039
```

### Quick Debug Session

Use the lldb quickstart script for interactive debugging:

```bash
./lldb/quickstart.sh comprehensive
```

### LLDB debug system

For full debugging instructions, see the debug system in the lldb/ folder

**`lldb/README.md`** - Quick start guide and usage instructions