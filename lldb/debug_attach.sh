#!/bin/bash
#
# Simple attach-and-debug script for QuickJS shape corruption
# Attaches to running app and sets up shape corruption detection
#

APP_PACKAGE="com.bgmdwldr.vulkan"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}QuickJS Shape Corruption Debugger${NC}"
echo "=================================="

# Get PID
PID=$(adb shell "pidof $APP_PACKAGE" 2>/dev/null | tr -d '\r')
if [ -z "$PID" ]; then
    echo "App not running. Starting it..."
    adb shell "am start -n $APP_PACKAGE/.MainActivity"
    sleep 2
    PID=$(adb shell "pidof $APP_PACKAGE" 2>/dev/null | tr -d '\r')
fi

if [ -z "$PID" ]; then
    echo "Error: Could not get app PID"
    exit 1
fi

echo -e "${BLUE}Attaching to PID $PID...${NC}"

# Create LLDB commands
cat > /tmp/lldb_attach.txt << EOF
# Attach to process
process attach --pid $PID

# Load master debug script  
command script import /Users/qingpinghe/Documents/bgmdwnldr/lldb_master_debug.py

# Start debugging
qjs-debug-start

# Continue
continue
EOF

# Run LLDB
echo -e "${YELLOW}Starting LLDB (will stop automatically on corruption)...${NC}"
lldb -s /tmp/lldb_attach.txt
