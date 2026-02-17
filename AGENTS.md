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

## Debugging

### LLDB debug system

For LLDB debugging, use the debug system in the lldb/ folder

**`lldb/README.md`** - Quick start guide and usage instructions