#!/bin/bash
# Trigger crash by entering YouTube URL in app

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/shell/common.sh"

URL="${1:-https://www.youtube.com/watch?v=dQw4w9WgXcQ}"

qjs_log "Triggering crash with URL: $URL"

# Ensure app is running
PID=$(qjs_get_pid)
if [ -z "$PID" ]; then
    qjs_warn "App not running, starting..."
    qjs_start_app
    sleep 3
fi

# Tap input field (correct coordinates based on debug output)
qjs_info "Tapping URL input field..."
adb shell input tap 540 1126
sleep 0.5

# Clear existing text
qjs_info "Clearing input..."
adb shell input keyevent --longpress 67 67 67 67 67 67 67 67
sleep 0.3

# Type URL
qjs_info "Entering URL..."
# Escape special characters for adb
adb shell "input text '$URL'"
sleep 0.5

# Submit
qjs_info "Submitting..."
adb shell input keyevent 66

qjs_log "Crash trigger sequence complete"
qjs_info "Check LLDB or logcat for crash details"
