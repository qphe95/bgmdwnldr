#!/bin/bash
# Quick-start debugging for bgmdwnldr
# Usage: ./lldb/quickstart.sh [profile]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROFILE="${1:-comprehensive}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log() { echo -e "${GREEN}[QJS]${NC} $1"; }
info() { echo -e "${BLUE}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

log "QuickJS Debugger Quick Start"
log "Profile: $PROFILE"

# Check prerequisites
if ! command -v adb &> /dev/null; then
    echo "adb not found. Please install Android SDK."
    exit 1
fi

if ! command -v lldb &> /dev/null; then
    echo "lldb not found. Please install Xcode or Android NDK."
    exit 1
fi

# Check device
if ! adb devices | grep -q "device$"; then
    warn "No Android device detected!"
    adb devices
    exit 1
fi

# Kill existing processes
info "Cleaning up..."
adb shell "pkill -9 -f lldb-server" 2>/dev/null || true
pkill -9 -f "lldb.*platform" 2>/dev/null || true
sleep 1

# Ensure lldb-server is on device
if ! adb shell "test -f /data/local/tmp/lldb-server" 2>/dev/null; then
    warn "lldb-server not found on device, trying to push..."
    NDK=${ANDROID_HOME:-$ANDROID_SDK_ROOT}/ndk/26.2.11394342
    LLDB_SERVER="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/17.0.2/lib/linux/aarch64/lldb-server"
    if [ -f "$LLDB_SERVER" ]; then
        adb push "$LLDB_SERVER" /data/local/tmp/
        adb shell chmod +x /data/local/tmp/lldb-server
    else
        warn "Could not find lldb-server in NDK"
    fi
fi

# Start lldb-server
info "Starting lldb-server..."
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &
LLDB_SERVER_PID=$!
sleep 2

# Port forward
adb forward tcp:5039 tcp:5039

# Get or start app
APP_PID=$(adb shell "pidof com.bgmdwldr.vulkan" 2>/dev/null | tr -d '\r')
if [ -z "$APP_PID" ]; then
    info "Starting app..."
    adb shell "am start -n com.bgmdwldr.vulkan/.MainActivity"
    sleep 2
    APP_PID=$(adb shell "pidof com.bgmdwldr.vulkan" 2>/dev/null | tr -d '\r')
fi

if [ -z "$APP_PID" ]; then
    echo "Failed to get app PID"
    kill $LLDB_SERVER_PID 2>/dev/null || true
    exit 1
fi

log "App PID: $APP_PID"

# Create init script
cat > /tmp/qjs_quickstart.txt << EOF
platform select remote-android
platform connect connect://localhost:5039
process attach -p $APP_PID
command script import $SCRIPT_DIR/main.py
qjs-debug $PROFILE
script print("\\n" + "="*60)
script print("QUICKJS DEBUGGER READY")
script print("="*60)
script print("To trigger crash, run in another terminal:")
script print("  ./lldb/scripts/trigger-crash.sh")
script print("="*60 + "\\n")
EOF

log "Starting LLDB..."
info "Press Ctrl+C to exit"
echo ""

# Cleanup on exit
cleanup() {
    info "Cleaning up..."
    kill $LLDB_SERVER_PID 2>/dev/null || true
    adb forward --remove tcp:5039 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Run LLDB
lldb -s /tmp/qjs_quickstart.txt
