#!/bin/bash
# LLDB attach and catch crash

APP_PACKAGE="com.bgmdwldr.vulkan"

echo "[DEBUG] Killing existing app..."
adb shell am force-stop $APP_PACKAGE 2>/dev/null
sleep 0.5

echo "[DEBUG] Clearing logs..."
adb logcat -c

echo "[DEBUG] Starting app in debug-wait mode..."
adb shell am start -D -n "$APP_PACKAGE/.MainActivity"
sleep 2

PID=$(adb shell pidof $APP_PACKAGE)
echo "[DEBUG] App PID: $PID (waiting for debugger)"

# Start lldb-server
echo "[DEBUG] Setting up lldb-server..."
adb shell "pkill -f lldb-server" 2>/dev/null
sleep 0.5
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &
sleep 1
adb forward tcp:5039 tcp:5039

# Create expect script
cat > /tmp/lldb_expect.exp << 'EOF'
#!/usr/bin/expect -f
set PID [lindex $argv 0]
set timeout 60

spawn /Library/Developer/CommandLineTools/usr/bin/lldb

expect "(lldb) "
send "platform select remote-android\r"

expect "(lldb) "
send "platform connect connect://localhost:5039\r"

expect "(lldb) "
send "attach $PID\r"

expect "(lldb) "
send "process handle SIGSEGV --stop true --print true\r"

expect "(lldb) "
send "breakpoint set -n init_browser_stubs\r"

expect "(lldb) "
send "breakpoint set -n JS_SetPropertyStr\r"

expect "(lldb) "
send "breakpoint list\r"

expect "(lldb) "
send "process continue\r"

# Wait for stop
expect {
    "stop reason" {
        send "bt\r"
        expect "(lldb) "
        send "register read\r"
        expect "(lldb) "
        send "thread list\r"
        expect "(lldb) "
        send "process continue\r"
    }
    "Process" {
        puts "\nProcess exited\n"
    }
    timeout {
        puts "\nTimeout - no crash or breakpoint\n"
    }
}

expect timeout { exit 0 }
EOF

chmod +x /tmp/lldb_expect.exp

echo "[DEBUG] Starting LLDB..."
/tmp/lldb_expect.exp $PID 2>&1 | tee /tmp/lldb_attach.log

echo ""
echo "[DEBUG] LLDB session ended"
echo "[DEBUG] Check /tmp/lldb_attach.log for details"
