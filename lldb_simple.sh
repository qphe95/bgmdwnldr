#!/bin/bash
# Simple LLDB debugging with file-based breakpoints

APP_PACKAGE="com.bgmdwldr.vulkan"

echo "[DEBUG] Killing existing app..."
adb shell am force-stop $APP_PACKAGE 2>/dev/null
sleep 0.5

echo "[DEBUG] Clearing logs..."
adb logcat -c

echo "[DEBUG] Starting app..."
adb shell am start -n "$APP_PACKAGE/.MainActivity"
sleep 2

PID=$(adb shell pidof $APP_PACKAGE)
echo "[DEBUG] App PID: $PID"

# Kill existing lldb-server
echo "[DEBUG] Setting up lldb-server..."
adb shell "pkill -f lldb-server" 2>/dev/null
sleep 0.5

# Start lldb-server
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &
LLDB_PID=$!
sleep 1

# Forward port
adb forward tcp:5039 tcp:5039

# Create LLDB commands
cat > /tmp/lldb_simple.txt << 'EOF'
platform select remote-android
platform connect connect://localhost:5039

# Attach to process
attach 999999

# Wait for modules to load
image list

# Set breakpoints on function names (pending since library loads after attach)
breakpoint set -n init_browser_stubs
breakpoint set -n JS_SetPropertyStr
breakpoint set -n JS_SetPropertyInternal

# List breakpoints
breakpoint list

# Continue
continue

# When hit, show backtrace and registers
bt
register read x0 x1 x2 x3 x4
frame variable

# Continue and catch crash
continue

# After crash
bt all
register read
quit
EOF

# Replace PID in commands
sed -i '' "s/999999/$PID/g" /tmp/lldb_simple.txt

echo "[DEBUG] Starting LLDB (this will take a moment)..."

# Create expect script to automate
/usr/bin/expect << EXPECT_SCRIPT
spawn /Library/Developer/CommandLineTools/usr/bin/lldb
expect "(lldb)"
send "platform select remote-android\r"
expect "(lldb)"
send "platform connect connect://localhost:5039\r"
expect "(lldb)"
send "attach $PID\r"
expect "(lldb)"
send "breakpoint set -n init_browser_stubs\r"
expect "(lldb)"
send "breakpoint set -n JS_SetPropertyStr\r"
expect "(lldb)"
send "breakpoint set -n JS_SetPropertyInternal\r"
expect "(lldb)"
send "breakpoint list\r"
expect "(lldb)"
send "continue\r"
set timeout 30
expect {
    "Process" {
        send "bt\r"
        expect "(lldb)"
        send "register read x0\r"
        expect "(lldb)"
        send "continue\r"
    }
    timeout {
        puts "\n[TIMEOUT] No breakpoint hit within 30 seconds\n"
    }
}
expect eof
EXPECT_SCRIPT

echo ""
echo "[DEBUG] LLDB session ended"

# Clean up
kill $LLDB_PID 2>/dev/null
