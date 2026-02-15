#!/bin/bash
#
# Batch-mode LLDB driver for QuickJS shape corruption debugging
# Uses LLDB's batch mode with Python scripting
#

APP_PACKAGE="com.bgmdwldr.vulkan"
SCRIPT_DIR="/Users/qingpinghe/Documents/bgmdwnldr"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}QuickJS Shape Corruption - Batch LLDB Debugger${NC}"
echo "================================================"

# Pre-flight
echo -e "${BLUE}Pre-flight checks...${NC}"
adb devices | grep -q "device$" || { echo -e "${RED}No device${NC}"; exit 1; }
echo "  ✓ Device connected"

# Setup
echo -e "${BLUE}Setting up environment...${NC}"
adb shell "am force-stop $APP_PACKAGE" 2>/dev/null
adb shell "pkill -9 -f lldb-server" 2>/dev/null
sleep 1
adb forward tcp:5039 tcp:5039

# Start lldb-server
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &
sleep 3
echo "  ✓ lldb-server started"

# Start app
adb shell "am start -n $APP_PACKAGE/.MainActivity"
sleep 2

# Get PID
PID=$(adb shell "pidof $APP_PACKAGE" | tr -d '\r')
echo "  ✓ App PID: $PID"

# Create the LLDB Python script that will be run
cat > /tmp/lldb_batch_script.py << 'PYTHON_SCRIPT'
import lldb
import struct
import sys

def log(msg):
    print(f"[LLDB-Py] {msg}")
    sys.stdout.flush()

# Get debugger
debugger = lldb.SBDebugger.GetDebugger()
target = debugger.GetSelectedTarget()
process = target.GetProcess()

log("Python script loaded into LLDB")
log(f"Target: {target.GetName()}")
log(f"Process: {process.GetProcessID()}")

# Define breakpoint callbacks
def on_init_browser_stubs(frame, bp_loc, dict):
    print("\n" + "="*70)
    print("[BREAKPOINT] init_browser_stubs ENTERED")
    print("="*70)
    return False

def on_set_property(frame, bp_loc, dict):
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    obj_val = int(frame.FindRegister("x1").GetValue(), 16)
    prop_ptr = int(frame.FindRegister("x2").GetValue(), 16)
    
    # Read property name
    error = lldb.SBError()
    name_bytes = process.ReadMemory(prop_ptr, 64, error)
    prop_name = "?"
    if error.Success() and name_bytes:
        try:
            prop_name = name_bytes.split(b'\x00')[0].decode('utf-8')
        except:
            pass
    
    print(f"\n[SET_PROPERTY] JS_SetPropertyStr('{prop_name}')")
    print(f"  obj = 0x{obj_val:x}")
    
    # Check if object looks valid
    if obj_val > 0x1000 and obj_val < 0x7f0000000000:
        error = lldb.SBError()
        obj_data = process.ReadMemory(obj_val, 24, error)
        if error.Success() and len(obj_data) == 24:
            class_id = struct.unpack_from('<H', obj_data, 0)[0]
            shape = struct.unpack_from('<Q', obj_data, 8)[0]
            print(f"  class_id = {class_id}")
            print(f"  shape = 0x{shape:x}")
            
            if shape < 0x1000:
                print("\n" + "!"*70)
                print(f"!!! CORRUPTION DETECTED !!!")
                print(f"!!! Shape is invalid: 0x{shape:x}")
                print("!"*70)
                
                # Print backtrace
                print("\nBacktrace:")
                for i, f in enumerate(thread.frames[:10]):
                    print(f"  #{i}: {f.GetFunctionName()}")
                
                return True  # Stop
    
    return False

def on_find_own_property(frame, bp_loc, dict):
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    psh = int(frame.FindRegister("x0").GetValue(), 16)
    print(f"\n[FIND_OWN_PROPERTY] psh = 0x{psh:x}")
    
    if psh < 0x1000:
        print("  ERROR: psh is NULL!")
        return True
    
    error = lldb.SBError()
    obj_ptr = process.ReadPointerFromMemory(psh, error)
    if not error.Success():
        print(f"  ERROR: {error.GetCString()}")
        return True
    
    print(f"  obj = 0x{obj_ptr:x}")
    
    # Read shape
    shape_ptr = process.ReadPointerFromMemory(obj_ptr + 8, error)
    if error.Success():
        print(f"  shape = 0x{shape_ptr:x}")
        if shape_ptr < 0x1000:
            print("\n" + "!"*70)
            print("!!! CRASH IMMINENT - INVALID SHAPE !!!")
            print("!"*70)
            return True
    
    return False

# Set up breakpoints
log("Setting up breakpoints...")

bp1 = target.BreakpointCreateByName("init_browser_stubs")
bp1.SetScriptCallbackFunction("lldb_batch_script.on_init_browser_stubs")
log(f"Breakpoint init_browser_stubs: ID={bp1.GetID()}")

bp2 = target.BreakpointCreateByName("JS_SetPropertyStr")
bp2.SetScriptCallbackFunction("lldb_batch_script.on_set_property")
log(f"Breakpoint JS_SetPropertyStr: ID={bp2.GetID()}")

bp3 = target.BreakpointCreateByName("find_own_property")
bp3.SetScriptCallbackFunction("lldb_batch_script.on_find_own_property")
log(f"Breakpoint find_own_property: ID={bp3.GetID()}")

log("All breakpoints set up")
log("Continuing execution...")

# Continue execution
process.Continue()

log("Script completed")
PYTHON_SCRIPT

# Create LLDB command file
cat > /tmp/lldb_batch_cmds.txt << LLDBCMDS
# Connect and attach
platform select remote-android
platform connect connect://localhost:5039
attach --pid $PID

# Load Python script
command script import /tmp/lldb_batch_script

# Continue - the Python script will handle breakpoints
process continue

# When stopped, show state
bt
register read

# Interactive mode
echo "Entering interactive mode - type 'quit' to exit"
LLDBCMDS

echo ""
echo -e "${YELLOW}Starting LLDB with Python scripting...${NC}"
echo ""

# Run LLDB
lldb -s /tmp/lldb_batch_cmds.txt

echo ""
echo -e "${GREEN}Debug session complete${NC}"
