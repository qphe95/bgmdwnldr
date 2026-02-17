#!/bin/bash
# Main debug script - thin wrapper around Python system

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/shell/common.sh"

PROFILE="${1:-comprehensive}"

qjs_log "QuickJS Shape Corruption Debugger"
qjs_log "Profile: $PROFILE"

# Pre-flight checks
qjs_check_device || exit 1

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

# Setup LLDB
qjs_setup_port_forward

# Run Python-based debugger
lldb -p "$PID" \
    -o "command script import ${SCRIPT_DIR}/../main.py" \
    -o "qjs-debug $PROFILE" \
    -o "continue"
