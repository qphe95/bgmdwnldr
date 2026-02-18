#!/bin/bash
# Common shell functions - source this file

# ============================================================================
# Colors
# ============================================================================
QJS_RED='\033[0;31m'
QJS_GREEN='\033[0;32m'
QJS_YELLOW='\033[1;33m'
QJS_BLUE='\033[0;34m'
QJS_CYAN='\033[0;36m'
QJS_MAGENTA='\033[0;35m'
QJS_NC='\033[0m'

# ============================================================================
# App Configuration
# ============================================================================
QJS_APP_PACKAGE="${QJS_APP_PACKAGE:-com.bgmdwldr.vulkan}"
QJS_ACTIVITY="${QJS_ACTIVITY:-.MainActivity}"
QJS_LLDB_SERVER="${QJS_LLDB_SERVER:-/data/local/tmp/lldb-server}"
QJS_LIB_NAME="${QJS_LIB_NAME:-libminimalvulkan.so}"

# ============================================================================
# Logging
# ============================================================================
qjs_log() { echo -e "${QJS_GREEN}[QJS]${QJS_NC} $1"; }
qjs_warn() { echo -e "${QJS_YELLOW}[QJS WARN]${QJS_NC} $1"; }
qjs_error() { echo -e "${QJS_RED}[QJS ERROR]${QJS_NC} $1"; }
qjs_info() { echo -e "${QJS_BLUE}[QJS INFO]${QJS_NC} $1"; }
qjs_debug() { echo -e "${QJS_CYAN}[QJS DEBUG]${QJS_NC} $1"; }

# ============================================================================
# Device Operations
# ============================================================================
qjs_check_device() {
    if ! adb devices | grep -q "device$"; then
        qjs_error "No Android device connected"
        return 1
    fi
    return 0
}

qjs_get_pid() {
    adb shell "pidof $QJS_APP_PACKAGE" 2>/dev/null | tr -d '\r'
}

qjs_start_app() {
    adb shell "am start -n $QJS_APP_PACKAGE/$QJS_ACTIVITY" 2>/dev/null
}

qjs_stop_app() {
    adb shell "am force-stop $QJS_APP_PACKAGE" 2>/dev/null
}

qjs_clear_logcat() {
    adb logcat -c 2>/dev/null
}

# ============================================================================
# LLDB Server Operations
# ============================================================================
qjs_start_lldb_server() {
    local port="${1:-5039}"
    adb shell "pkill -f lldb-server" 2>/dev/null
    sleep 0.5
    adb shell "$QJS_LLDB_SERVER platform --listen '*:$port' --server" &
    sleep 2
}

qjs_check_lldb_server() {
    adb shell "ps -A | grep lldb-server" | grep -q "lldb-server"
}

qjs_stop_lldb_server() {
    adb shell "pkill -9 -f lldb-server" 2>/dev/null
}

# ============================================================================
# Port Forwarding
# ============================================================================
qjs_setup_port_forward() {
    local local_port="${1:-5039}"
    local remote_port="${2:-5039}"
    adb forward tcp:$local_port tcp:$remote_port 2>/dev/null
}

qjs_setup_jdwp_forward() {
    local pid="$1"
    if [ -n "$pid" ]; then
        adb forward tcp:8700 jdwp:$pid 2>/dev/null || true
    fi
}

# ============================================================================
# Library Address Resolution (for stripped binaries)
# ============================================================================
qjs_get_lib_base() {
    local pid="$1"
    local lib="${2:-$QJS_LIB_NAME}"
    adb shell "cat /proc/$pid/maps | grep $lib | head -1 | cut -d'-' -f1" 2>/dev/null
}

qjs_wait_for_lib() {
    local pid="$1"
    local lib="${2:-$QJS_LIB_NAME}"
    local timeout="${3:-30}"
    
    for i in $(seq 1 $timeout); do
        local base=$(qjs_get_lib_base "$pid" "$lib")
        if [ -n "$base" ]; then
            echo "$base"
            return 0
        fi
        sleep 0.5
    done
    return 1
}

# ============================================================================
# ADB Automation (UI interaction)
# ============================================================================
qjs_tap() {
    local x="$1"
    local y="$2"
    adb shell "input tap $x $y"
}

qjs_input_text() {
    local text="$1"
    # Escape spaces for adb
    local escaped="${text// /%s}"
    adb shell "input text '$escaped'"
}

qjs_keyevent() {
    local keycode="$1"
    adb shell "input keyevent $keycode"
}

qjs_trigger_url_entry() {
    local url="${1:-https://www.youtube.com/watch?v=dQw4w9WgXcQ}"
    
    # Tap input field
    qjs_tap 540 600
    sleep 0.5
    
    # Clear existing (multiple delete)
    qjs_keyevent 67  # KEYCODE_DEL
    sleep 0.3
    
    # Enter URL
    qjs_input_text "$url"
    sleep 0.5
    
    # Submit
    qjs_keyevent 66  # KEYCODE_ENTER
}

# ============================================================================
# Cross-platform Timeout
# ============================================================================

# Cross-platform timeout function
# Usage: qjs_timeout <seconds> <command> [args...]
qjs_timeout() {
    local duration=$1
    shift
    if command -v timeout &> /dev/null; then
        timeout "$duration" "$@"
    elif command -v gtimeout &> /dev/null; then
        gtimeout "$duration" "$@"
    else
        # macOS fallback using Perl
        perl -e 'alarm shift; exec @ARGV' "$duration" "$@"
    fi
}

# ============================================================================
# Utility Functions
# ============================================================================
qjs_get_script_dir() {
    cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
}

qjs_cleanup() {
    qjs_info "Cleaning up..."
    qjs_stop_lldb_server
    qjs_stop_app
    adb forward --remove-all 2>/dev/null || true
}

# Set up trap for cleanup on exit
trap qjs_cleanup EXIT INT TERM
