#!/bin/bash
# Comprehensive LLDB debugging session

set -e

echo "=== QuickJS LLDB Debug Session ==="

# Clean up
pkill -f lldb-server 2>/dev/null || true
adb shell am force-stop com.bgmdwldr.vulkan 2>/dev/null || true
adb logcat -c
sleep 1

# Start lldb-server first
echo "[1/6] Starting lldb-server..."
adb shell /data/local/tmp/lldb-server platform --listen "*:5039" --server &
LLDB_SERVER_PID=$!
sleep 2

# Forward port
adb forward tcp:5039 tcp:5039

# Start app in debug mode (will wait for debugger)
echo "[2/6] Starting app with debugger wait..."
adb shell am start -D -n com.bgmdwldr.vulkan/.MainActivity

# Wait for app to initialize
sleep 3

# Get PID
PID=$(adb shell pidof com.bgmdwldr.vulkan)
echo "[3/6] App PID: $PID"

if [ -z "$PID" ]; then
    echo "ERROR: App didn't start"
    kill $LLDB_SERVER_PID 2>/dev/null || true
    exit 1
fi

# Create LLDB script for this session
cat > /tmp/lldb_session_$PID.txt << EOF
# QuickJS Debug Session for PID $PID
platform select remote-android
platform connect connect://localhost:5039
attach -p $PID

# We should be stopped at debugger entry
# Wait for shape address to appear in logs
script import time
script print("Waiting for app to initialize...")
script time.sleep(3)

# Now set breakpoint at init_browser_stubs
breakpoint set -n init_browser_stubs
continue

# When we hit init_browser_stubs, the shape should be valid
script print("=== At init_browser_stubs ===")
script print("Check logcat for shape address, then manually set watchpoint")

# Show current frame
frame info
register read

# Interactive mode
script print("LLDB ready for interactive commands")
script print("Use: watchpoint set expression -w write -- <shape_addr_from_logcat>")
script print("Then: continue")
EOF

echo "[4/6] LLDB script created: /tmp/lldb_session_$PID.txt"
echo ""
echo "=== Starting LLDB ==="
echo ""

# Run LLDB with the script
lldb -s /tmp/lldb_session_$PID.txt

# Cleanup
echo ""
echo "[6/6] Cleaning up..."
kill $LLDB_SERVER_PID 2>/dev/null || true
adb shell am force-stop com.bgmdwldr.vulkan 2>/dev/null || true

echo "Debug session ended"
