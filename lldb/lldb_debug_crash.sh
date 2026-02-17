#!/bin/bash
# Debug script for bgmdwldr crash

# Kill any existing instances
adb shell am force-stop com.bgmdwldr.vulkan 2>/dev/null
sleep 1

# Clear logs
adb logcat -c

# Start the app and get PID
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity
sleep 2

APP_PID=$(adb shell pidof com.bgmdwldr.vulkan)
if [ -z "$APP_PID" ]; then
    echo "App not running, checking for crash..."
    adb logcat -d | grep -E "(F DEBUG|signal|crash)" | tail -10
    exit 1
fi

echo "App PID: $APP_PID"
echo "Attaching LLDB..."

# Create LLDB commands
cat > /tmp/lldb_commands.txt << 'EOF'
# Set breakpoint at crash location
breakpoint set -n JS_SetPropertyStr
breakpoint set -n init_browser_stubs

# Continue
continue

# When we hit init_browser_stubs, log it
script print("=== Hit init_browser_stubs ===")

# Continue to crash
continue

# Show backtrace when crashed
bt all
register read
EOF

# Run LLDB
lldb -b \
    -o "platform select remote-android" \
    -o "platform connect connect://localhost:5039" \
    -o "attach $APP_PID" \
    -S /tmp/lldb_commands.txt \
    app/build/intermediates/cxx/Debug/5696k3i1/obj/local/arm64-v8a/libminimalvulkan.so
