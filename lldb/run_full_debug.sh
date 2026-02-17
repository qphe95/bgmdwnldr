#!/bin/bash
# Full debugging session for the 0xc0000008 crash

set -e

echo "========================================"
echo "  QuickJS 0xc0000008 Crash Debugger"
echo "========================================"
echo ""

# Check prerequisites
if ! command -v lldb &> /dev/null; then
    echo "ERROR: LLDB not found in PATH"
    exit 1
fi

# Kill existing app
echo "[1/7] Cleaning up..."
adb shell am force-stop com.bgmdwldr.vulkan 2>/dev/null || true
sleep 1

# Start app in debugger-wait mode
echo "[2/7] Starting app with debugger-wait (-D)..."
adb shell am start -D -n com.bgmdwldr.vulkan/.MainActivity &
sleep 2

# Get PID
APP_PID=$(adb shell pidof com.bgmdwldr.vulkan 2>/dev/null)
if [ -z "$APP_PID" ]; then
    echo "ERROR: App failed to start"
    exit 1
fi
echo "    PID: $APP_PID"

# Setup port forwarding
echo "[3/7] Setting up port forwarding..."
adb forward tcp:5039 tcp:5039

# Check for lldb-server
echo "[4/7] Checking for lldb-server on device..."
if ! adb shell test -f /data/local/tmp/lldb-server; then
    echo "    WARNING: lldb-server not found at /data/local/tmp/lldb-server"
    echo "    Attempting to find and push it..."
    
    # Try to find lldb-server in NDK
    NDK_LLDB=$(find ~/Library/Android/sdk/ndk -name "lldb-server" -path "*/arm64/*" 2>/dev/null | head -1)
    if [ -z "$NDK_LLDB" ]; then
        NDK_LLDB=$(find /usr/local/lib/android/sdk/ndk -name "lldb-server" -path "*/arm64/*" 2>/dev/null | head -1)
    fi
    
    if [ -n "$NDK_LLDB" ]; then
        echo "    Found: $NDK_LLDB"
        adb push "$NDK_LLDB" /data/local/tmp/
        adb shell chmod +x /data/local/tmp/lldb-server
    else
        echo "    ERROR: Could not find lldb-server in NDK"
        exit 1
    fi
fi

# Start lldb-server
echo "[5/7] Starting lldb-server..."
adb shell /data/local/tmp/lldb-server platform --server --listen "*:5039" &
LLDB_SERVER_PID=$!
sleep 2

# Create comprehensive LLDB script
echo "[6/7] Creating LLDB script..."
cat > /tmp/full_debug.lldb << 'LLDB_SCRIPT'
# Connect to device
platform select remote-android
platform connect connect://localhost:5039

# Attach to process
process attach -p $APP_PID

# Import our debugging scripts
command script import lldb_crash_debug.py
command script import lldb_trace_x28.py

echo "========================================"
echo "  Debugging commands available:"
echo "========================================"
echo "  trace_x28_origin  - Full trace to find bad x28 origin"
echo "  step_until_bad_x28 [n] - Step n instructions watching x28"
echo "  check_registers   - Check all registers for suspicious values"
echo "  trace_jsvalue <addr> - Inspect JSValue at address"
echo ""
echo "  Manual commands:"
echo "    bt                - Show backtrace"
echo "    register read x28 - Read x28 register"
echo "    frame variable    - Show local variables"
echo "    continue          - Continue execution"
echo "    stepi             - Single step"
echo "========================================"
echo ""

# Set up basic breakpoints
breakpoint set -n JS_DefineProperty
breakpoint set -n JS_NewContextRaw
breakpoint set -n JS_NewContext

# Set a breakpoint that stops when x28 looks suspicious
breakpoint set -n JS_DefineProperty -C '(unsigned long)$x28 == 0xc0000000 || (unsigned long)$x28 == 0xc0000008'

echo "Breakpoints set. Ready to debug."
echo ""
echo "The app is waiting for input."
echo "After continuing, enter a URL in the app to trigger the crash."
echo ""

continue
LLDB_SCRIPT

# Replace $APP_PID with actual value
sed -i.bak "s/\$APP_PID/$APP_PID/g" /tmp/full_debug.lldb

# Instructions
echo "[7/7] Ready to debug!"
echo ""
echo "========================================"
echo "  IMPORTANT INSTRUCTIONS:"
echo "========================================"
echo ""
echo "1. LLDB will start and attach to the app"
echo "2. The app is currently paused waiting for debugger"
echo "3. In LLDB, type 'continue' to let the app run"
echo "4. In ANOTHER terminal, trigger the crash:"
echo ""
echo "   adb shell input tap 540 1126"
echo "   adb shell input text 'https://example.com'"
echo "   adb shell input keyevent 66"
echo ""
echo "5. When the crash breakpoint hits, use:"
echo "   - 'bt' for backtrace"
echo "   - 'register read' to see all registers"
echo "   - 'check_registers' to find suspicious values"
echo ""
echo "========================================"
echo ""

# Run LLDB
lldb -s /tmp/full_debug.lldb

# Cleanup
echo ""
echo "Cleaning up..."
kill $LLDB_SERVER_PID 2>/dev/null || true
