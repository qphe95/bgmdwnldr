#!/bin/bash
# LLDB debugging script for QuickJS shape corruption

set -e

echo "=== LLDB Debug Script for QuickJS ==="

# Kill any existing app
adb shell am force-stop com.bgmdwldr.vulkan 2>/dev/null || true
sleep 1

# Start the app in debug mode (wait for debugger)
echo "[1/6] Starting app in debug mode..."
adb shell am start -D -n com.bgmdwldr.vulkan/.MainActivity

# Wait for app to start
sleep 3

# Get PID
PID=$(adb shell pidof com.bgmdwldr.vulkan)
echo "[2/6] App PID: $PID"

if [ -z "$PID" ]; then
    echo "ERROR: Failed to get PID"
    exit 1
fi

# Forward JDWP port for Java debugging (not needed for native, but good practice)
adb forward tcp:8700 jdwp:$PID 2>/dev/null || true

# Forward LLDB port
adb forward tcp:5039 tcp:5039

# Start lldb-server
echo "[3/6] Starting lldb-server..."
adb shell /data/local/tmp/lldb-server platform --listen "*:5039" --server &
LLDB_PID=$!
sleep 2

# Create LLDB command script
cat > /tmp/quickjs_lldb_commands.txt << 'EOF'
# Connect to remote
platform select remote-android
platform connect connect://localhost:5039

# Wait a moment for connection
script import time; time.sleep(1)

# Attach to process
attach -p <PID>

# Set async mode for better control
settings set target.async false

# Load symbols if needed
# target symbols add /path/to/symbols

# Set initial breakpoint at global object creation
breakpoint set -n JS_AddIntrinsicBasicObjects

# Run until we hit the breakpoint
continue

# At this point we should be at JS_AddIntrinsicBasicObjects
# Let it run once to get past global object creation
continue

# Now set breakpoint at init_browser_stubs
breakpoint set -n init_browser_stubs
continue

# Now we're in init_browser_stubs - let's examine the global object
echo "=== Examining global object ==="

# Get ctx->global_obj value
# This requires knowing the offset of global_obj in JSContext struct
# We'll use debug log address from previous run as reference

# For now, set watchpoint on the known shape address from previous run
# In real run, we'll get this dynamically

# Continue and see what happens
continue

# If we get here, app didn't crash yet - investigate further
echo "=== App still running - investigating ==="
bt all

quit
EOF

# Replace PID in script
sed -i.bak "s/<PID>/$PID/g" /tmp/quickjs_lldb_commands.txt

echo "[4/6] LLDB commands prepared"
echo "[5/6] Starting LLDB..."

# Start LLDB with the commands
/Users/qingpinghe/Android/sdk/ndk/26.2.11394342/toolchains/llvm/prebuilt/darwin-x86_64/bin/lldb -s /tmp/quickjs_lldb_commands.txt

echo "[6/6] LLDB session ended"
echo ""
echo "=== Manual LLDB Commands for Interactive Debugging ==="
echo "1. Run: ./lldb_debug.sh"
echo "2. When breakpoint hits, use:"
echo "   - expression ctx->global_obj"
echo "   - expression JSObject *p = JS_VALUE_GET_OBJ(ctx->global_obj); p"
echo "   - expression p->shape"
echo "   - memory read &p->shape -s 8 -c 1"
echo "3. Set watchpoint: watchpoint set expression -w write -- &((JSObject*)0xADDR)->shape"
echo "4. Continue: continue"
echo "5. When watchpoint triggers: bt, register read, frame info"

# Cleanup
kill $LLDB_PID 2>/dev/null || true
