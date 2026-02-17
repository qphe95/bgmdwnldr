#!/bin/bash
# LLDB debugging for stripped binaries using address-based breakpoints

APP_PACKAGE="com.bgmdwldr.vulkan"
LIB_NAME="libminimalvulkan.so"

# Kill any existing app
adb shell am force-stop $APP_PACKAGE 2>/dev/null
sleep 0.5

# Clear logs
adb logcat -c

# Get library base address (need to read /proc/pid/maps)
echo "[DEBUG] Starting app in debug mode..."

# Start app with debugger wait
adb shell am start -D -n "$APP_PACKAGE/.MainActivity"
sleep 2

# Get PID
PID=$(adb shell pidof $APP_PACKAGE)
echo "[DEBUG] App PID: $PID"

# Forward JDWP port for Java debugging (optional)
# adb forward tcp:8700 jdwp:$PID

# Start lldb-server if not running
adb shell "pkill -f lldb-server" 2>/dev/null
sleep 0.5
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &
sleep 1
adb forward tcp:5039 tcp:5039

# Find library base address after load
get_lib_base() {
    local pid=$1
    local lib=$2
    adb shell "cat /proc/$pid/maps | grep $lib | head -1 | cut -d'-' -f1"
}

# Wait for library to load
echo "[DEBUG] Waiting for library to load..."
for i in {1..30}; do
    BASE=$(get_lib_base $PID $LIB_NAME)
    if [ -n "$BASE" ]; then
        echo "[DEBUG] Library loaded at base: 0x$BASE"
        break
    fi
    sleep 0.5
done

if [ -z "$BASE" ]; then
    echo "[ERROR] Library not loaded!"
    exit 1
fi

# Calculate addresses based on tombstone offsets
# From tombstone:
# init_browser_stubs+1848 = 0xa1f30
# JS_SetPropertyStr+116 = 0xb775c
# JS_SetPropertyInternal+1164 = 0xb51d4

# Since we have the base address, calculate absolute addresses
BASE_DEC=$(printf "%d" "0x$BASE")
INIT_BROWSER_STUBS=$((BASE_DEC + 0xa1f30 - 0xa16b8))  # Adjust for actual function entry
JS_SET_PROP_STR=$((BASE_DEC + 0xb775c - 0xb76f0))
JS_SET_PROP_INT=$((BASE_DEC + 0xb51d4 - 0xb4d20))

echo "[DEBUG] Breakpoint addresses:"
echo "  init_browser_stubs: 0x$(printf '%x' $INIT_BROWSER_STUBS)"
echo "  JS_SetPropertyStr: 0x$(printf '%x' $JS_SET_PROP_STR)"
echo "  JS_SetPropertyInternal: 0x$(printf '%x' $JS_SET_PROP_INT)"

# Create LLDB commands file
cat > /tmp/lldb_commands.txt << EOF
platform select remote-android
platform connect connect://localhost:5039
attach $PID

# Wait for library to be properly mapped
image list

# Set breakpoints by address (after library is loaded)
# These are relative to the image base
breakpoint set -a 0x$(printf '%x' $INIT_BROWSER_STUBS) -n init_browser_stubs
breakpoint set -a 0x$(printf '%x' $JS_SET_PROP_STR) -n JS_SetPropertyStr
breakpoint set -a 0x$(printf '%x' $JS_SET_PROP_INT) -n JS_SetPropertyInternal

# Also catch SIGSEGV
process handle SIGSEGV --stop true

# Continue execution
continue

# When stopped, show info
bt
register read x0 x1 x2
thread list
EOF

# Run LLDB
echo "[DEBUG] Starting LLDB..."
/Library/Developer/CommandLineTools/usr/bin/lldb -s /tmp/lldb_commands.txt 2>&1 | tee /tmp/lldb_output.log

echo "[DEBUG] LLDB session ended"
echo "[DEBUG] Check /tmp/lldb_output.log for details"
