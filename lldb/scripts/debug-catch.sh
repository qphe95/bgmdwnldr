#!/bin/bash
# Debug with automatic crash detection and analysis

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/shell/common.sh"

PROFILE="${1:-comprehensive}"
OUTPUT_DIR="${2:-/tmp/qjs_crash}"
mkdir -p "$OUTPUT_DIR"

qjs_log "QuickJS Crash Catcher"
qjs_log "Profile: $PROFILE"
qjs_log "Output: $OUTPUT_DIR"

# Pre-flight checks
qjs_check_device || exit 1

# Clear previous crash logs
adb logcat -c 2>/dev/null || true

# Stop any existing LLDB server
qjs_stop_lldb_server
sleep 1

# Start LLDB server
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

# Create LLDB script with crash-catching breakpoints
cat > /tmp/qjs_lldb_catch.txt << EOF
platform select remote-android
platform connect connect://localhost:5039
process attach -p $PID

# Configure crash signals
process handle SIGSEGV --stop=true --notify=true --pass=false
process handle SIGBUS --stop=true --notify=true --pass=false
process handle SIGILL --stop=true --notify=true --pass=false
process handle SIGABRT --stop=true --notify=true --pass=false

command script import ${SCRIPT_DIR}/../main.py
qjs-debug $PROFILE

# Add stop-hook for enhanced crash detection
target stop-hook add -o "script import sys; sys.path.insert(0, '${SCRIPT_DIR}/..'); from main import qjs_handle_stop_event; qjs_handle_stop_event(frame, None, {})"

# Set up crash detection breakpoints
breakpoint set -n abort -C "frame select 0; register read; bt all; qjs-dump-obj \$x0"
breakpoint set -n __stack_chk_fail -C "bt all"

# Log to file
log enable lldb events $OUTPUT_DIR/lldb-events.log

# Continue with timeout monitoring
script import subprocess, time, os
script print("[LLDB] Ready. Waiting for crash...")
script print("[LLDB] The debugger will automatically stop on SIGSEGV/SIGBUS/SIGILL/SIGABRT")
continue
EOF

qjs_info "Starting LLDB crash monitor..."
qjs_info "Trigger crash in another terminal with:"
qjs_info "  ./lldb/scripts/trigger-crash.sh"

# Run LLDB and capture output
qjs_timeout 300 lldb -s /tmp/qjs_lldb_catch.txt 2>&1 | tee "$OUTPUT_DIR/lldb-session.log" &
LLDB_PID=$!

# Monitor for crash in background
(
    while kill -0 $LLDB_PID 2>/dev/null; do
        if adb logcat -d 2>/dev/null | grep -q "FATAL.*signal"; then
            qjs_warn "CRASH DETECTED in logcat!"
            sleep 2
            
            # Capture tombstone
            TOMBSTONE=$(adb shell "ls -t /data/tombstones/tombstone_* 2>/dev/null | head -1" | tr -d '\r')
            if [ -n "$TOMBSTONE" ]; then
                qjs_info "Capturing tombstone: $TOMBSTONE"
                adb shell "cat $TOMBSTONE" > "$OUTPUT_DIR/tombstone.txt" 2>/dev/null
            fi
            
            # Capture logcat
            adb logcat -d > "$OUTPUT_DIR/logcat.txt" 2>/dev/null
            
            qjs_log "Crash data saved to $OUTPUT_DIR/"
            break
        fi
        sleep 1
    done
) &
MONITOR_PID=$!

# Wait for LLDB to finish
wait $LLDB_PID 2>/dev/null
kill $MONITOR_PID 2>/dev/null

# Final logcat capture
adb logcat -d > "$OUTPUT_DIR/logcat-final.txt" 2>/dev/null

qjs_log "Debugging session complete"
qjs_info "Results in: $OUTPUT_DIR/"
ls -la "$OUTPUT_DIR/"
