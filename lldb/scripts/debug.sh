#!/bin/bash
# Main debug script - interactive LLDB debugging with crash catching

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/shell/common.sh"

PROFILE="${1:-comprehensive}"

qjs_log "QuickJS Shape Corruption Debugger"
qjs_log "Profile: $PROFILE"

# Pre-flight checks
qjs_check_device || exit 1

# Stop any existing LLDB server
qjs_stop_lldb_server
sleep 1

# Start LLDB server on device
qjs_info "Starting LLDB server..."
qjs_start_lldb_server 5039

# Setup port forwarding
qjs_setup_port_forward 5039 5039

# Get or start app
PID=$(qjs_get_pid)
if [ -z "$PID" ]; then
    qjs_info "Starting app..."
    qjs_start_app
    sleep 2
    PID=$(qjs_get_pid)
fi

if [ -z "$PID" ]; then
    qjs_error "Could not get app PID"
    exit 1
fi

qjs_log "App PID: $PID"

# Run adb root for better debugging access
adb root 2>/dev/null || true
sleep 1

# Create LLDB init script with proper remote debugging
cat > /tmp/qjs_lldb_init.txt << EOF
# Connect to remote Android device
platform select remote-android
platform connect connect://localhost:5039

# Attach to process
process attach -p $PID

# Configure signal handling for crash detection
process handle SIGSEGV --stop=true --notify=true --pass=false
process handle SIGBUS --stop=true --notify=true --pass=false
process handle SIGILL --stop=true --notify=true --pass=false
process handle SIGABRT --stop=true --notify=true --pass=false

# Load QuickJS debugging module
command script import ${SCRIPT_DIR}/../main.py

# Set up debugging profile
qjs-debug $PROFILE

# Add stop-hook for enhanced crash detection
target stop-hook add -o "script import sys; sys.path.insert(0, '${SCRIPT_DIR}/..'); from main import qjs_handle_stop_event; qjs_handle_stop_event(frame, None, {})"

# Show status
qjs-status
EOF

qjs_log "Starting LLDB interactive session..."
qjs_info "Commands to trigger crash:"
qjs_info "  adb shell input tap 540 1126"
qjs_info "  adb shell input text 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'"
qjs_info "  adb shell input keyevent 66"
echo ""

# Start LLDB interactive session
lldb -s /tmp/qjs_lldb_init.txt

qjs_log "LLDB session ended"
