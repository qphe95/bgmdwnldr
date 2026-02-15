#!/bin/bash
# Automated LLDB debugging script for QuickJS shape corruption

APP_PACKAGE="com.bgmdwldr.vulkan"
LIB_NAME="libminimalvulkan.so"

echo "=========================================="
echo "QuickJS Shape Corruption LLDB Debugger"
echo "=========================================="

# Kill any existing app
echo "[1/6] Killing existing app..."
adb shell am force-stop $APP_PACKAGE 2>/dev/null
sleep 0.5

# Clear logs
echo "[2/6] Clearing logs..."
adb logcat -c

# Start lldb-server
echo "[3/6] Starting lldb-server..."
adb shell "pkill -f lldb-server" 2>/dev/null
sleep 0.5
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &
LLDB_SERVER_PID=$!
sleep 1
adb forward tcp:5039 tcp:5039

# Start app
echo "[4/6] Starting app..."
adb shell am start -n "$APP_PACKAGE/.MainActivity"
sleep 2

# Get PID
PID=$(adb shell pidof $APP_PACKAGE 2>/dev/null)
if [ -z "$PID" ]; then
    echo "ERROR: App not running!"
    kill $LLDB_SERVER_PID 2>/dev/null
    exit 1
fi
echo "App PID: $PID"

# Wait for library to load
echo "[5/6] Waiting for library to load..."
for i in {1..20}; do
    BASE=$(adb shell "cat /proc/$PID/maps | grep $LIB_NAME | head -1 | cut -d'-' -f1" 2>/dev/null)
    if [ -n "$BASE" ]; then
        echo "Library loaded at base: 0x$BASE"
        break
    fi
    sleep 0.5
done

if [ -z "$BASE" ]; then
    echo "WARNING: Could not find library base address"
fi

# Create LLDB script
echo "[6/6] Starting LLDB..."
cat > /tmp/lldb_session.txt << 'EOF'
platform select remote-android
platform connect connect://localhost:5039
EOF

echo "attach $PID" >> /tmp/lldb_session.txt

cat >> /tmp/lldb_session.txt << 'EOF'
# Handle SIGSEGV
process handle SIGSEGV --stop true --print true

# Set pending breakpoints
breakpoint set -n init_browser_stubs -P true
breakpoint set -n JS_SetPropertyStr -P true
breakpoint set -n JS_SetPropertyInternal -P true

# Show breakpoints
breakpoint list

echo "Ready! Continue to start execution..."
continue

# If we hit a breakpoint, show info
echo "STOPPED - examining state..."
bt
register read x0 x1 x2 x3

# Check if x0 looks like a valid JSObject
script << PYTHON
import lldb
target = lldb.debugger.GetSelectedTarget()
frame = target.GetProcess().GetSelectedThread().GetSelectedFrame()
x0 = frame.FindRegister('x0')
print(f"x0 = {x0.GetValue()}")
obj_ptr = int(x0.GetValue(), 0)
error = lldb.SBError()
shape_data = target.GetProcess().ReadMemory(obj_ptr + 8, 8, error)
if error.Success():
    import struct
    shape_addr = struct.unpack('<Q', shape_data)[0]
    print(f"Shape pointer: 0x{shape_addr:x}")
    if shape_addr < 0x1000:
        print("WARNING: Shape pointer looks invalid!")
PYTHON

echo "Press continue to continue execution, or quit to exit..."
EOF

# Run LLDB
/Library/Developer/CommandLineTools/usr/bin/lldb -s /tmp/lldb_session.txt

# Cleanup
echo "Cleaning up..."
kill $LLDB_SERVER_PID 2>/dev/null

echo "Done!"
