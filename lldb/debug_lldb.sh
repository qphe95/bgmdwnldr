#!/bin/bash

# Start the app
adb shell am force-stop com.bgmdwldr.vulkan
adb logcat -c
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# Wait for app to start and get PID
sleep 2
PID=$(adb shell pidof com.bgmdwldr.vulkan)
echo "App PID: $PID"

if [ -z "$PID" ]; then
    echo "Failed to get PID, app may not have started"
    exit 1
fi

# Forward port for LLDB
adb forward tcp:5039 tcp:5039

# Start lldb-server attached to the process
adb shell /data/local/tmp/lldb-server platform --listen "*:5039" --server &
sleep 2

# Create LLDB script
cat > /tmp/lldb_commands.txt << 'EOF'
# Connect to the device
platform select remote-android
platform connect connect://localhost:5039

# Attach to the process
attach -p <PID>

# Set breakpoint on init_browser_stubs
breakpoint set -n init_browser_stubs

# Continue execution
continue
EOF

# Replace PID in script
sed -i '' "s/<PID>/$PID/g" /tmp/lldb_commands.txt

echo "LLDB commands prepared in /tmp/lldb_commands.txt"
echo "Run: /Users/qingpinghe/Android/sdk/ndk/26.2.11394342/toolchains/llvm/prebuilt/darwin-x86_64/bin/lldb -s /tmp/lldb_commands.txt"
