#!/bin/bash
# Debug script to catch the 0xc0000008 crash with LLDB

set -e

echo "=== LLDB Crash Debug Script ==="
echo "Target: Catch 0xc0000008 crash in JS_DefineProperty"
echo ""

# Kill any running instances
echo "[1/6] Killing any running app instances..."
adb shell am force-stop com.bgmdwldr.vulkan 2>/dev/null || true
sleep 1

# Start app with debugger wait
echo "[2/6] Starting app with debugger wait (-D flag)..."
adb shell am start -D -n com.bgmdwldr.vulkan/.MainActivity &
sleep 2

# Get PID
APP_PID=$(adb shell pidof com.bgmdwldr.vulkan 2>/dev/null)
if [ -z "$APP_PID" ]; then
    echo "ERROR: App didn't start properly"
    exit 1
fi
echo "    App PID: $APP_PID"

# Forward JDWP port for Java debugger (optional, for reference)
echo "[3/6] Setting up port forwarding..."
adb forward tcp:8700 jdwp:$APP_PID 2>/dev/null || true

# Forward LLDB port
adb forward tcp:5039 tcp:5039

# Start lldb-server on device
echo "[4/6] Starting lldb-server on device..."
adb shell /data/local/tmp/lldb-server platform --server --listen "*:5039" &
sleep 2

# Create LLDB command script
echo "[5/6] Creating LLDB command script..."
cat > /tmp/lldb_crash_commands.txt << 'LLDB_EOF'
# Connect to device
platform select remote-android
platform connect connect://localhost:5039

# Set target architecture
settings set target.arm.arch arm64

# Attach to process
process attach -p $APP_PID

# Load our debug script
command script import lldb_crash_debug.py

# Set up crash detection
catch_crash

# Also set a breakpoint at the crash location
breakpoint set -n JS_DefineProperty

# Continue execution
continue

# When we hit a breakpoint, print context
script \
    frame = lldb.frame; \
    print(f"\n=== Stopped at {frame.GetFunctionName()} ==="); \
    for reg in ["x28", "x0", "x1", "x2", "x3", "lr", "pc"]: \
        val = frame.FindRegister(reg); \
        print(f"  {reg} = {val.GetValue()}")

# Continue to catch the crash
continue
LLDB_EOF

# Run LLDB
echo "[6/6] Launching LLDB..."
echo ""
echo "Instructions:"
echo "  - The app is running and waiting for input"
echo "  - LLDB will attach and break at JS_DefineProperty"
echo "  - Use 'continue' to run until crash"
echo "  - When crash happens, check registers with 'check_registers'"
echo "  - Use 'bt' for backtrace"
echo ""

lldb -s /tmp/lldb_crash_commands.txt
