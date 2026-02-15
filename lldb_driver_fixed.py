#!/usr/bin/env python3
"""
Fixed LLDB Driver for QuickJS Shape Corruption Debugging
Attaches without causing ANR by continuing immediately
"""

import sys
sys.path.insert(0, '/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/Python')

import lldb
import subprocess
import time
import struct
from collections import defaultdict

APP_PACKAGE = "com.bgmdwldr.vulkan"
_debug_state = {'corruption_detected': False}

def log(msg):
    print(f"[LLDB] {msg}")
    sys.stdout.flush()

def on_init_browser_stubs(frame, bp_loc, dict):
    print("\n" + "="*70)
    print("[BREAKPOINT] init_browser_stubs ENTERED")
    print("="*70)
    return False

def on_js_set_property(frame, bp_loc, dict):
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    obj_val = int(frame.FindRegister("x1").GetValue(), 16)
    prop_ptr = int(frame.FindRegister("x2").GetValue(), 16)
    
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
    
    if obj_val > 0x1000:
        error = lldb.SBError()
        obj_data = process.ReadMemory(obj_val, 24, error)
        if error.Success() and len(obj_data) == 24:
            class_id = struct.unpack_from('<H', obj_data, 0)[0]
            shape = struct.unpack_from('<Q', obj_data, 8)[0]
            print(f"  class_id = {class_id}")
            print(f"  shape = 0x{shape:x}")
            
            if shape < 0x1000:
                print("\n" + "!"*70)
                print(f"!!! CORRUPTION DETECTED - shape is 0x{shape:x} !!!")
                print("!"*70)
                _debug_state['corruption_detected'] = True
                
                # Print backtrace
                print("\nBacktrace:")
                for i, f in enumerate(thread.frames[:10]):
                    func = f.GetFunctionName() or "???"
                    pc = f.GetPCAddress().GetLoadAddress(thread.GetProcess().GetTarget())
                    print(f"  #{i}: 0x{pc:x} {func}")
                return True
    
    return False

def main():
    log("="*70)
    log("QUICKJS SHAPE CORRUPTION DEBUGGER (FIXED)")
    log("="*70)
    
    # Start the app normally first
    log("Starting app...")
    subprocess.run(["adb", "shell", "am", "start", "-n", f"{APP_PACKAGE}/.MainActivity"], 
                   capture_output=True)
    time.sleep(3)
    
    # Get PID
    result = subprocess.run(["adb", "shell", "pidof", APP_PACKAGE], 
                           capture_output=True, text=True)
    if result.returncode != 0:
        log("ERROR: Could not get PID")
        return 1
    pid = int(result.stdout.strip())
    log(f"App PID: {pid}")
    
    # Start lldb-server
    log("Starting lldb-server...")
    subprocess.Popen(["adb", "shell", "/data/local/tmp/lldb-server", 
                      "platform", "--listen", "'*:5039'", "--server"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    
    # Create debugger
    log("Creating debugger...")
    debugger = lldb.SBDebugger.Create()
    debugger.SetAsync(True)  # Important: async mode for non-blocking
    
    # Connect to platform
    debugger.HandleCommand("platform select remote-android")
    debugger.HandleCommand("platform connect connect://localhost:5039")
    log("Connected to platform")
    
    # Create target and attach
    target = debugger.CreateTarget("")
    log(f"Attaching to PID {pid}...")
    
    attach_info = lldb.SBAttachInfo(pid)
    error = lldb.SBError()
    process = target.Attach(attach_info, error)
    
    if error.Fail():
        log(f"ERROR: Failed to attach: {error.GetCString()}")
        return 1
    
    log("Attached!")
    
    # Set breakpoints
    bp_init = target.BreakpointCreateByName("init_browser_stubs")
    bp_init.SetScriptCallbackFunction("lldb_driver_fixed.on_init_browser_stubs")
    
    bp_set = target.BreakpointCreateByName("JS_SetPropertyStr")
    bp_set.SetScriptCallbackFunction("lldb_driver_fixed.on_js_set_property")
    
    log(f"Breakpoints: init_browser_stubs={bp_init.GetNumLocations()}, "
        f"JS_SetPropertyStr={bp_set.GetNumLocations()}")
    
    # IMPORTANT: Continue immediately to prevent ANR
    log("Continuing process immediately...")
    process.Continue()
    
    # Now trigger the crash
    log("Triggering crash in 2 seconds...")
    time.sleep(2)
    
    subprocess.run(["adb", "shell", "input", "tap", "540", "600"])
    time.sleep(0.5)
    subprocess.run(["adb", "shell", "input", "text", 
                   "https://www.youtube.com/watch?v=dQw4w9WgXcQ"])
    time.sleep(0.5)
    subprocess.run(["adb", "shell", "input", "keyevent", "66"])
    log("URL submitted")
    
    # Wait and poll for crash
    log("Waiting for crash (polling)...")
    for i in range(30):
        time.sleep(1)
        state = process.GetState()
        
        if state == lldb.eStateStopped:
            thread = process.GetSelectedThread()
            stop_reason = thread.GetStopReason()
            
            if stop_reason == lldb.eStopReasonBreakpoint:
                # Process hit a breakpoint - callback should have run
                # Continue to let it finish
                process.Continue()
                continue
            else:
                print(f"\n{'!'*70}")
                print(f"!!! PROCESS STOPPED (reason={stop_reason}) !!!")
                print(f"{'!'*70}")
                
                # Print backtrace
                print("\nBacktrace:")
                for i, f in enumerate(thread.frames[:10]):
                    func = f.GetFunctionName() or "???"
                    pc = f.GetPCAddress().GetLoadAddress(target)
                    print(f"  #{i}: 0x{pc:x} {func}")
                break
                
        elif state == lldb.eStateCrashed:
            print(f"\n{'!'*70}")
            print("!!! PROCESS CRASHED !!!")
            print(f"{'!'*70}")
            break
            
        elif state == lldb.eStateExited:
            log("Process exited")
            break
    
    # Summary
    print(f"\n{'='*70}")
    print("SESSION SUMMARY")
    print(f"{'='*70}")
    print(f"Corruption detected: {_debug_state['corruption_detected']}")
    
    lldb.SBDebugger.Destroy(debugger)
    return 0

if __name__ == "__main__":
    sys.exit(main())
