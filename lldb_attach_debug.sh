#!/bin/bash
# Attach LLDB to running app and debug the crash

echo "=== Attaching LLDB to running app ==="

APP_PID=$(adb shell pidof com.bgmdwldr.vulkan 2>/dev/null)
if [ -z "$APP_PID" ]; then
    echo "ERROR: App not running. Start it first with:"
    echo "  adb shell am start -n com.bgmdwldr.vulkan/.MainActivity"
    exit 1
fi

echo "App PID: $APP_PID"

# Forward LLDB port
adb forward tcp:5039 tcp:5039 2>/dev/null || true

# Create LLDB script
cat > /tmp/lldb_attach.txt << LLDB_EOF
platform select remote-android
platform connect connect://localhost:5039
process attach -p $APP_PID

# Load debug script
command script import lldb_crash_debug.py

# Set breakpoints at key locations
breakpoint set -n JS_DefineProperty
breakpoint set -n JS_NewContextRaw

# Set a breakpoint with condition on x28
breakpoint set -n JS_DefineProperty -C '((unsigned long)$x28 == 0xc0000000) || ((unsigned long)$x28 == 0xc0000008)'

continue
LLDB_EOF

echo "Starting LLDB..."
lldb -s /tmp/lldb_attach.txt
