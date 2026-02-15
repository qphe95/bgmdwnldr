#!/bin/bash
# Simplified LLDB batch mode debugging

APP_PACKAGE="com.bgmdwldr.vulkan"

echo "=== LLDB Batch Debugging ==="

# Kill and restart
adb shell am force-stop $APP_PACKAGE 2>/dev/null
sleep 0.5
adb logcat -c

# Start lldb-server
adb shell pkill -f lldb-server 2>/dev/null
sleep 0.5
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &
sleep 1
adb forward tcp:5039 tcp:5039

# Start app
adb shell am start -n "$APP_PACKAGE/.MainActivity"
sleep 2

PID=$(adb shell pidof $APP_PACKAGE 2>/dev/null)
echo "PID: $PID"

# Create batch commands
cat > /tmp/lldb_batch.txt << EOF
platform select remote-android
platform connect connect://localhost:5039
attach $PID

# Don't stop on SIGSEGV but print it
process handle SIGSEGV --stop true --print true

# Set breakpoints (pending)
breakpoint set -n init_browser_stubs -P true
breakpoint set -n JS_SetPropertyStr -P true

# Continue and log
continue

# If we stop, print info
bt 20
register read x0 x1 x2 x3 pc

# Continue until crash or exit
continue

# Final state
bt 20
register read
quit
EOF

echo "Running LLDB batch..."
/Library/Developer/CommandLineTools/usr/bin/lldb -b -s /tmp/lldb_batch.txt 2>&1 | tee /tmp/lldb_batch_output.log

echo "=== Done. Check /tmp/lldb_batch_output.log ==="
