#!/bin/bash
# Debug script with watchpoint-based shape corruption detection

echo "=== Shape Corruption Watchpoint Debugger ==="
echo ""

# Get app PID
echo "Waiting for app to start..."
adb shell "while true; do pid=$(pidof com.bgmdwldr.vulkan); if [ -n \"\$pid\" ]; then echo \$pid; break; fi; sleep 0.5; done" > /tmp/app_pid.txt &

# Start app
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# Wait for PID
sleep 2
APP_PID=$(cat /tmp/app_pid.txt 2>/dev/null)

if [ -z "$APP_PID" ]; then
    echo "Failed to get app PID"
    exit 1
fi

echo "App PID: $APP_PID"

# Forward debugging port
adb forward tcp:5039 tcp:5039

# Start lldb-server
adb shell /data/local/tmp/lldb-server platform --server --listen "*:5039" &
sleep 2

# Create LLDB commands
cat > /tmp/lldb_watch_commands.txt << 'EOF'
platform select remote-android
platform connect connect://localhost:5039
attach -p APP_PID

# Load our watchpoint debug script
command script import lldb_watchpoint_debug.py

# Set additional breakpoints for tracing
breakpoint set --name init_browser_stubs
breakpoint set --name JS_SetPropertyInternal
breakpoint set --name find_own_property

# Start watching objects
wp-start

# Continue execution
c
EOF

# Replace PID
sed -i.bak "s/APP_PID/$APP_PID/g" /tmp/lldb_watch_commands.txt

echo "Starting LLDB..."
lldb -s /tmp/lldb_watch_commands.txt
