#!/bin/bash
#
# Advanced LLDB debugging script for shape corruption issue
# Uses Python scripting for conditional breakpoints and memory inspection
#

set -e

APP_PACKAGE="com.bgmdwldr.vulkan"
ACTIVITY=".MainActivity"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== QuickJS Shape Corruption Debugger ===${NC}"

# Check if emulator is running
if ! adb devices | grep -q "emulator"; then
    echo -e "${RED}Error: No emulator detected${NC}"
    exit 1
fi

# Kill any running instance
echo "[+] Stopping any running instances..."
adb shell "am force-stop $APP_PACKAGE" 2>/dev/null || true
adb shell "am force-stop com.oneplus.backuprestore" 2>/dev/null || true

# Clear logs
echo "[+] Clearing logcat..."
adb logcat -c

# Find the app_process path (Android 10+)
APP_PROCESS="/system/bin/app_process"
if adb shell "test -f /apex/com.android.art/bin/app_process64" 2>/dev/null; then
    APP_PROCESS="/apex/com.android.art/bin/app_process64"
fi

echo "[+] App process: $APP_PROCESS"

# Start the app with debug flags
echo "[+] Starting app with debugger wait..."
adb shell "am start -D -n $APP_PACKAGE/$ACTIVITY" &

# Wait for app to start
echo "[+] Waiting for app to initialize..."
sleep 2

# Get PID
PID=$(adb shell "pidof $APP_PACKAGE" | tr -d '\r')
if [ -z "$PID" ]; then
    echo -e "${RED}Error: Could not get PID${NC}"
    exit 1
fi
echo "[+] App PID: $PID"

# Forward JDWP port for Java debugging (optional)
adb forward tcp:8700 jdwp:$PID 2>/dev/null || true

# Create LLDB command file
cat > /tmp/lldb_session.txt << 'LLDBEOF'
# LLDB session for shape debugging

# Settings
settings set target.inline-breakpoint-strategy always
settings set target.process.stop-on-sharedlibrary-load false

# Load Python script
command script import /Users/qingpinghe/Documents/bgmdwnldr/lldb_advanced_shape.py

# Target is already attached, start debugging
start-shape-debug

# Continue execution
process continue

LLDBEOF

# Launch LLDB with our debugging scripts
echo -e "${GREEN}[+] Launching LLDB with advanced debugging...${NC}"
echo "    Commands available:"
echo "      start-shape-debug  - Start comprehensive debugging"
echo "      check-obj <addr>   - Inspect a JSObject"
echo "      watch-shape <addr> - Watch shape field for changes"
echo ""
echo "    Press Enter to continue..."
read

# Attach to the running process
lldb -p $PID -s /tmp/lldb_session.txt

echo ""
echo -e "${GREEN}=== Debugging session ended ===${NC}"
