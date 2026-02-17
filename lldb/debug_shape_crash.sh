#!/bin/bash
#
# Comprehensive LLDB debugging script for QuickJS shape corruption
# This script automates the entire debugging workflow
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_PACKAGE="com.bgmdwldr.vulkan"
ACTIVITY=".MainActivity"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     QuickJS Shape Corruption - Automated LLDB Debugger              ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════════╝${NC}"

# Cleanup function
cleanup() {
    echo -e "\n${BLUE}[Cleanup] Stopping debug server...${NC}"
    adb shell "pkill -f lldb-server" 2>/dev/null || true
    pkill -f "lldb" 2>/dev/null || true
}
trap cleanup EXIT

# Check emulator
echo -e "${BLUE}[1/6] Checking device connection...${NC}"
if ! adb devices | grep -q "device$"; then
    echo -e "${RED}Error: No device found${NC}"
    exit 1
fi

# Kill existing
echo -e "${BLUE}[2/6] Stopping existing app...${NC}"
adb shell "am force-stop $APP_PACKAGE" 2>/dev/null || true
adb shell "pkill -f lldb-server" 2>/dev/null || true
pkill -f "lldb" 2>/dev/null || true
sleep 1

# Clear logs
echo -e "${BLUE}[3/6] Clearing logcat...${NC}"
adb logcat -c

# Forward port
echo -e "${BLUE}[4/6] Setting up port forwarding...${NC}"
adb forward tcp:5039 tcp:5039

# Start lldb-server in background
echo -e "${BLUE}[5/6] Starting lldb-server...${NC}"
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &
LLDB_SERVER_PID=$!
sleep 3

# Create comprehensive LLDB script
cat > /tmp/lldb_auto_debug.txt << 'LLDBEOF'
# QuickJS Shape Corruption Auto-Debug Script

# Connect to device
platform select remote-android
platform connect connect://localhost:5039

# Wait for app to start
echo "Waiting for app to start..."
script import time
script import subprocess

# Start the app
script subprocess.run(["adb", "shell", "am", "start", "-n", "com.bgmdwldr.vulkan/.MainActivity"])
script time.sleep(2)

# Get PID and attach
script import lldb
echo "Getting app PID..."
script pid_str = subprocess.check_output(["adb", "shell", "pidof", "com.bgmdwldr.vulkan"]).decode().strip()
script print(f"App PID: {pid_str}")
script pid = int(pid_str)

# Attach to process
attach --pid $pid

# Load master debug script
echo "Loading master debug script..."
command script import /Users/qingpinghe/Documents/bgmdwnldr/lldb_master_debug.py

# Start comprehensive debugging
echo "Starting shape corruption detection..."
qjs-debug-start

# Continue and wait for crash
echo "Running app - will stop automatically when corruption is detected..."
process continue

# If we get here, corruption was detected or app exited
echo "Debugger stopped - analyzing state..."
qjs-analyze

# Keep LLDB session active for manual inspection
echo "\n========================================"
echo "LLDB session active. Commands available:"
echo "  qjs-check-obj <addr>  - Inspect object"
echo "  qjs-analyze           - Full analysis"
echo "  bt                    - Backtrace"
echo "  register read         - Show registers"
echo "  memory read <addr>    - Read memory"
echo "  continue              - Continue execution"
echo "  quit                  - Exit"
echo "========================================"
LLDBEOF

# Run LLDB with the script
echo -e "${BLUE}[6/6] Launching LLDB debugger...${NC}"
echo -e "${CYAN}The debugger will automatically detect the shape corruption${NC}"
echo ""

# Use system lldb with the script
lldb -s /tmp/lldb_auto_debug.txt

echo -e "\n${GREEN}=== Debugging session ended ===${NC}"
