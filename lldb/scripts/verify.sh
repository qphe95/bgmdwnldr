#!/bin/bash
# Verify LLDB debug system setup

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/shell/common.sh"

qjs_log "QuickJS Debug System Verification"
echo ""

# Track errors
ERRORS=0

# Check 1: ADB available
qjs_info "Checking ADB..."
if command -v adb &> /dev/null; then
    ADB_VERSION=$(adb version | head -1)
    qjs_log "✓ ADB found: $ADB_VERSION"
else
    qjs_error "✗ ADB not found in PATH"
    ERRORS=$((ERRORS + 1))
fi
echo ""

# Check 2: LLDB available
qjs_info "Checking LLDB..."
if command -v lldb &> /dev/null; then
    LLDB_VERSION=$(lldb --version | head -1)
    qjs_log "✓ LLDB found: $LLDB_VERSION"
else
    qjs_error "✗ LLDB not found in PATH"
    ERRORS=$((ERRORS + 1))
fi
echo ""

# Check 3: Device connected
qjs_info "Checking device connection..."
if qjs_check_device; then
    DEVICE=$(adb devices | grep "device$" | head -1 | awk '{print $1}')
    qjs_log "✓ Device connected: $DEVICE"
    
    # Check if it's an emulator
    if adb shell getprop ro.kernel.qemu 2>/dev/null | grep -q "1"; then
        qjs_info "  Device is an emulator"
    fi
else
    qjs_error "✗ No device connected"
    ERRORS=$((ERRORS + 1))
fi
echo ""

# Check 4: App installed
qjs_info "Checking if app is installed..."
if adb shell "pm list packages" | grep -q "$QJS_APP_PACKAGE"; then
    qjs_log "✓ App $QJS_APP_PACKAGE is installed"
else
    qjs_warn "✗ App $QJS_APP_PACKAGE not found"
    qjs_info "  Install it first with: ./rebuild.sh"
fi
echo ""

# Check 5: lldb-server on device
qjs_info "Checking lldb-server on device..."
if qjs_check_lldb_binary; then
    qjs_log "✓ lldb-server binary found at $QJS_LLDB_SERVER"
else
    qjs_warn "✗ lldb-server not found at $QJS_LLDB_SERVER"
    qjs_info "  Push it with:"
    
    # Try to find NDK path
    NDK_PATH=""
    if [ -n "$ANDROID_HOME" ]; then
        NDK_PATH=$(find "$ANDROID_HOME/ndk" -name "lldb-server" -type f 2>/dev/null | grep aarch64 | head -1)
    elif [ -n "$ANDROID_SDK_ROOT" ]; then
        NDK_PATH=$(find "$ANDROID_SDK_ROOT/ndk" -name "lldb-server" -type f 2>/dev/null | grep aarch64 | head -1)
    fi
    
    if [ -n "$NDK_PATH" ]; then
        qjs_info "  adb push \"$NDK_PATH\" $QJS_LLDB_SERVER"
        qjs_info "  adb shell chmod +x $QJS_LLDB_SERVER"
    else
        qjs_info "  Find your NDK lldb-server and push it to $QJS_LLDB_SERVER"
    fi
    ERRORS=$((ERRORS + 1))
fi
echo ""

# Check 6: Port availability
qjs_info "Checking if port 5039 is available..."
if ! netstat -an 2>/dev/null | grep -q ":5039 "; then
    qjs_log "✓ Port 5039 is available"
else
    qjs_warn "⚠ Port 5039 is in use"
    qjs_info "  You may need to kill existing lldb-server: adb shell pkill -f lldb-server"
fi
echo ""

# Check 7: Python syntax
qjs_info "Checking Python module syntax..."
SYNTAX_OK=true

# Check main.py
if ! python3 -m py_compile "$SCRIPT_DIR/../main.py" 2>/dev/null; then
    qjs_error "✗ Syntax error in main.py"
    SYNTAX_OK=false
fi

# Check lib files using find
while IFS= read -r py_file; do
    if [ -f "$py_file" ]; then
        if ! python3 -m py_compile "$py_file" 2>/dev/null; then
            qjs_error "✗ Syntax error in $py_file"
            SYNTAX_OK=false
        fi
    fi
done < <(find "$SCRIPT_DIR/.." -name "*.py" -type f 2>/dev/null)

if $SYNTAX_OK; then
    qjs_log "✓ Python module syntax OK"
else
    qjs_error "✗ Some Python modules have syntax errors"
    ERRORS=$((ERRORS + 1))
fi
echo ""

# Check 8: LLDB Python availability (just check the scripts are importable in principle)
qjs_info "Checking LLDB script structure..."
if [ -f "$SCRIPT_DIR/../main.py" ] && [ -f "$SCRIPT_DIR/../lib/debug/session.py" ]; then
    qjs_log "✓ LLDB debug system files present"
else
    qjs_warn "✗ Some LLDB debug system files missing"
    ERRORS=$((ERRORS + 1))
fi
echo ""

# Summary
echo "=================================="
if [ $ERRORS -eq 0 ]; then
    qjs_log "All checks passed! You're ready to debug."
    echo ""
    qjs_info "Next steps:"
    qjs_info "  1. Start the app: adb shell am start -n $QJS_APP_PACKAGE/$QJS_ACTIVITY"
    qjs_info "  2. Run: ./lldb/quickstart.sh"
    exit 0
else
    qjs_error "$ERRORS check(s) failed. Please fix the issues above."
    exit 1
fi
