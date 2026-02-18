#!/bin/bash
# Attach-only variant - attach to running process

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/shell/common.sh"

PROFILE="${1:-minimal}"

qjs_log "QuickJS Debug - Attach Mode"

# Check device
qjs_check_device || exit 1

# Get PID of running app
PID=$(qjs_get_pid)
if [ -z "$PID" ]; then
    qjs_error "App not running. Start it first:"
    qjs_info "  adb shell am start -n $QJS_APP_PACKAGE/$QJS_ACTIVITY"
    exit 1
fi

qjs_log "Attaching to PID: $PID"

# Start LLDB server
qjs_start_lldb_server 5039

# Setup port forwarding
qjs_setup_port_forward 5039 5039

# Create init script for remote debugging
cat > /tmp/qjs_attach.txt << EOF
platform select remote-android
platform connect connect://localhost:5039
process attach -p $PID
command script import ${SCRIPT_DIR}/../main.py
qjs-debug $PROFILE
EOF

# Attach with LLDB
lldb -s /tmp/qjs_attach.txt
