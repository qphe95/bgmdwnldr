#!/bin/bash
# Root cause analysis using LLDB watchpoints

echo "=== QuickJS Shape Corruption - Root Cause Analysis ==="
echo ""

# Kill any existing app
adb shell am force-stop com.bgmdwldr.vulkan
sleep 1

# Clear logs
adb logcat -c

# Start app in background
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity &
sleep 2

# Get PID
APP_PID=$(adb shell pidof com.bgmdwldr.vulkan)
if [ -z "$APP_PID" ]; then
    echo "Failed to get app PID"
    exit 1
fi

echo "App PID: $APP_PID"

# Forward LLDB port
adb forward tcp:5039 tcp:5039 2>/dev/null || true

# Start lldb-server
adb shell /data/local/tmp/lldb-server platform --server --listen "*:5039" &
sleep 2

# Create LLDB command script
cat > /tmp/lldb_rc_commands.txt << 'EOF'
platform select remote-android
platform connect connect://localhost:5039

# Wait a moment for connection
script import time
time.sleep(0.5)

# Attach to process
attach -p APP_PID

# Load our root cause analyzer
command script import lldb_root_cause.py

# Start tracking
rc-start

echo "Ready! The debugger will now track objects and catch corruption."
echo "Trigger the crash by entering a URL in the app."
echo ""

# Continue execution
c
EOF

# Replace PID
sed -i.bak "s/APP_PID/$APP_PID/g" /tmp/lldb_rc_commands.txt

echo ""
echo "Starting LLDB with root cause analysis..."
echo ""

lldb -s /tmp/lldb_rc_commands.txt
