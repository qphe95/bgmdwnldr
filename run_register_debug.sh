#!/bin/bash
# Debug script to catch the x28=0xc0000000 crash

echo "=== Killing any running app instances ==="
adb shell am force-stop com.bgmdwldr.vulkan 2>/dev/null
sleep 1

echo "=== Starting app with debugger wait ==="
adb shell am start -D -n com.bgmdwldr.vulkan/.MainActivity &
sleep 2

APP_PID=$(adb shell pidof com.bgmdwldr.vulkan 2>/dev/null)
if [ -z "$APP_PID" ]; then
    echo "ERROR: App didn't start"
    exit 1
fi

echo "App PID: $APP_PID"

# Forward JDWP port
adb forward tcp:8700 jdwp:$APP_PID

cat > /tmp/lldb_script.txt << 'LLDB_EOF'
platform select remote-android
platform connect connect://localhost:5039
platform settings -w /data/local/tmp

# Set target architecture
settings set target.arm.arch arm64

# Attach to process
process attach -p $APP_PID

# Load debug script
command script import lldb_register_debug.py

# Set breakpoint at JS_NewContext
breakpoint set -n JS_NewContext

# Continue execution
continue

# When we hit the breakpoint, step through and check x28
script \
    for i in range(100): \
        lldb.debugger.HandleCommand("register read x28") \
        lldb.debugger.HandleCommand("stepi") \
        if lldb.frame.FindRegister("x28").GetValue() == "0xc0000000": \
            print("FOUND x28 = 0xc0000000!") \
            lldb.debugger.HandleCommand("bt") \
            break
LLDB_EOF

echo "=== Launching LLDB ==="
lldb -s /tmp/lldb_script.txt

