#!/bin/bash
#
# Launch LLDB debugging for QuickJS shape corruption
# This is the main script to run for debugging
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
NC='\033[0m'

echo -e "${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     QuickJS Shape Corruption LLDB Debugging Session          ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"

# Check emulator
if ! adb devices | grep -q "emulator"; then
    echo -e "${RED}Error: No emulator found${NC}"
    exit 1
fi

# Kill existing
echo -e "${BLUE}[+] Stopping existing app...${NC}"
adb shell "am force-stop $APP_PACKAGE" 2>/dev/null || true
sleep 1

# Clear logs
echo -e "${BLUE}[+] Clearing logcat...${NC}"
adb logcat -c

# Start app
echo -e "${BLUE}[+] Starting app...${NC}"
adb shell "am start -n $APP_PACKAGE/$ACTIVITY" &
sleep 2

# Get PID
PID=$(adb shell "pidof $APP_PACKAGE" 2>/dev/null | tr -d '\r')
if [ -z "$PID" ]; then
    echo -e "${RED}Error: Could not get PID${NC}"
    exit 1
fi

echo -e "${GREEN}[+] App PID: $PID${NC}"

# Create LLDB init script
cat > /tmp/lldb_init.txt << 'LLDBEOF'
# QuickJS Shape Debugging Session

# Load master debug script
command script import /Users/qingpinghe/Documents/bgmdwnldr/lldb_master_debug.py

# Alternative: use advanced shape debugger instead
# command script import /Users/qingpinghe/Documents/bgmdwnldr/lldb_advanced_shape.py

# Start debugging
qjs-debug-start

# Continue execution - will stop automatically when corruption is detected
process continue

LLDBEOF

echo ""
echo -e "${YELLOW}=== LLDB Debugging Commands ===${NC}"
echo ""
echo "Once LLDB starts, you can use these commands:"
echo ""
echo "  qjs-debug-start      - Start comprehensive debugging"
echo "  qjs-check-obj <addr> - Inspect a JSObject at address"
echo "  qjs-where-shape      - Track shape origins"
echo "  qjs-analyze          - Full state analysis"
echo ""
echo "  scan-objects         - Scan heap for objects"
echo "  find-shape-refs      - Find references to a shape"
echo "  analyze-crash        - Analyze crash state"
echo ""
echo "  frame variable       - Show local variables"
echo "  register read        - Show registers"
echo "  memory read <addr>   - Read memory"
echo ""
echo -e "${YELLOW}The debugger will automatically stop when shape corruption is detected.${NC}"
echo ""

# Attach LLDB
echo -e "${GREEN}[+] Attaching LLDB to PID $PID...${NC}"
echo ""

lldb -p "$PID" -s /tmp/lldb_init.txt

echo ""
echo -e "${GREEN}=== Debugging session ended ===${NC}"
