#!/bin/bash
#
# Comprehensive LLDB Debugging Orchestration Script
# This is the MASTER script - run this to debug the shape corruption
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_PACKAGE="com.bgmdwldr.vulkan"
ACTIVITY=".MainActivity"
LLDB_SERVER="/data/local/tmp/lldb-server"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

# Logging
LOG_FILE="/tmp/qjs_debug_$(date +%Y%m%d_%H%M%S).log"

log() {
    echo -e "$1" | tee -a "$LOG_FILE"
}

# Cleanup function
cleanup() {
    log "\n${BLUE}[Cleanup] Shutting down debug infrastructure...${NC}"
    adb shell "pkill -9 -f lldb-server" 2>/dev/null || true
    pkill -9 -f "lldb.*comprehensive" 2>/dev/null || true
    adb forward --remove tcp:5039 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Header
log "${GREEN}╔══════════════════════════════════════════════════════════════════════════════╗${NC}"
log "${GREEN}║          COMPREHENSIVE QUICKJS SHAPE CORRUPTION DEBUG SESSION              ║${NC}"
log "${GREEN}╚══════════════════════════════════════════════════════════════════════════════╝${NC}"
log ""

# Phase 1: Pre-flight checks
log "${MAGENTA}[Phase 1/5] Pre-flight checks...${NC}"

if ! adb devices | grep -q "device$"; then
    log "${RED}ERROR: No Android device connected${NC}"
    exit 1
fi
log "  ✓ Device connected"

if ! adb shell "pm list packages" | grep -q "$APP_PACKAGE"; then
    log "${RED}ERROR: App $APP_PACKAGE not installed${NC}"
    exit 1
fi
log "  ✓ App installed"

if ! adb shell "test -f $LLDB_SERVER && echo yes" | grep -q "yes"; then
    log "${YELLOW}WARNING: lldb-server not found at $LLDB_SERVER${NC}"
    NDK_PATH="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-$HOME/Android/sdk/ndk/26.2.11394342}}"
    LLDB_SRC="$NDK_PATH/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/17/lib/linux/arm64/lldb-server"
    if [ -f "$LLDB_SRC" ]; then
        adb push "$LLDB_SRC" "$LLDB_SERVER"
        adb shell "chmod +x $LLDB_SERVER"
        log "  ✓ lldb-server pushed"
    else
        log "${RED}ERROR: Cannot find lldb-server in NDK${NC}"
        exit 1
    fi
fi
log "  ✓ lldb-server available"

# Phase 2: Environment setup
log "${MAGENTA}[Phase 2/5] Setting up debug environment...${NC}"

adb shell "am force-stop $APP_PACKAGE" 2>/dev/null || true
adb shell "pkill -9 -f lldb-server" 2>/dev/null || true
pkill -9 -f "lldb.*comprehensive" 2>/dev/null || true
sleep 2

adb logcat -c 2>/dev/null || true
log "  ✓ Logcat cleared"

adb forward tcp:5039 tcp:5039
log "  ✓ Port forwarded (5039)"

# Phase 3: Start lldb-server
log "${MAGENTA}[Phase 3/5] Starting lldb-server...${NC}"

adb shell "$LLDB_SERVER platform --listen '*:5039' --server" &
sleep 3

if ! adb shell "ps -A | grep lldb-server" | grep -q "lldb-server"; then
    log "${RED}ERROR: lldb-server failed to start${NC}"
    exit 1
fi
log "  ✓ lldb-server running"

# Phase 4: Prepare LLDB script
log "${MAGENTA}[Phase 4/5] Preparing LLDB automation script...${NC}"

cp "$SCRIPT_DIR/lldb_comprehensive_cmds.txt" /tmp/lldb_comprehensive_cmds.txt
log "  ✓ LLDB script ready"

# Phase 5: Launch debugger
log "${MAGENTA}[Phase 5/5] Launching comprehensive debugger...${NC}"
log ""
log "${YELLOW}═══════════════════════════════════════════════════════════════════${NC}"
log "${YELLOW}  DEBUGGER IS NOW RUNNING${NC}"
log "${YELLOW}  The app will execute until shape corruption is detected${NC}"
log "${YELLOW}═══════════════════════════════════════════════════════════════════${NC}"
log ""

lldb -s /tmp/lldb_comprehensive_cmds.txt 2>&1 | tee -a "$LOG_FILE"

log ""
log "${GREEN}═══════════════════════════════════════════════════════════════════${NC}"
log "${GREEN}  DEBUG SESSION COMPLETE${NC}"
log "${GREEN}  Full log saved to: $LOG_FILE${NC}"
log "${GREEN}═══════════════════════════════════════════════════════════════════${NC}"
