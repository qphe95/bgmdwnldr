#!/bin/bash
# Full orchestration with remote-android platform

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/shell/common.sh"

PROFILE="${1:-comprehensive}"
PORT="${2:-5039}"

qjs_log "QuickJS Debug - Full Orchestration"
qjs_log "Profile: $PROFILE, Port: $PORT"

# Pre-flight
qjs_check_device || exit 1

# Kill existing
qjs_stop_app
qjs_stop_lldb_server
sleep 1

# Start fresh
qjs_clear_logcat
qjs_start_lldb_server "$PORT"
qjs_setup_port_forward "$PORT" "$PORT"

# Start app
qjs_start_app
sleep 2

# Get PID
PID=$(qjs_get_pid)
if [ -z "$PID" ]; then
    qjs_error "Failed to get app PID"
    exit 1
fi

qjs_log "App PID: $PID"

# Create LLDB script
cat > /tmp/qjs_lldb_$$.txt << EOF
platform select remote-android
platform connect connect://localhost:$PORT
attach $PID
command script import ${SCRIPT_DIR}/../main.py
qjs-debug $PROFILE
continue
EOF

# Run LLDB
lldb -s /tmp/qjs_lldb_$$.txt

# Cleanup
rm -f /tmp/qjs_lldb_$$.txt
qjs_cleanup
