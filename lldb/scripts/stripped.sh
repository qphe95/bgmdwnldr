#!/bin/bash
# Stripped binary debugging with address calculation

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/shell/common.sh"

PROFILE="${1:-stripped}"

qjs_log "QuickJS Debug - Stripped Binary Mode"

# Pre-flight
qjs_check_device || exit 1

# Kill existing
qjs_stop_app
qjs_stop_lldb_server
sleep 1

# Start fresh
qjs_clear_logcat
qjs_start_lldb_server
qjs_setup_port_forward

# Start app and wait for library load
qjs_start_app
sleep 2

# Get PID
PID=$(qjs_get_pid)
if [ -z "$PID" ]; then
    qjs_error "Failed to get app PID"
    exit 1
fi

qjs_log "App PID: $PID"

# Wait for library to load
qjs_info "Waiting for library to load..."
BASE=$(qjs_wait_for_lib "$PID" "$QJS_LIB_NAME" 30)

if [ -z "$BASE" ]; then
    qjs_error "Library did not load in time"
    exit 1
fi

qjs_log "Library base: 0x$BASE"

# Run with stripped binary profile
lldb \
    -o "platform select remote-android" \
    -o "platform connect connect://localhost:5039" \
    -o "attach $PID" \
    -o "command script import ${SCRIPT_DIR}/../main.py" \
    -o "qjs-debug $PROFILE"

qjs_cleanup
