#!/usr/bin/env python3
"""
LLDB Driver for Comprehensive QuickJS Shape Corruption Debugging
This script automates the entire LLDB session using the Python API.
"""

import sys
# Add LLDB Python paths
sys.path.insert(0, '/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Resources/Python')
sys.path.insert(0, '/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/Python')

import lldb
import sys
import time
import subprocess
import os
import struct
from collections import defaultdict

APP_PACKAGE = "com.bgmdwldr.vulkan"
SCRIPT_DIR = "/Users/qingpinghe/Documents/bgmdwnldr"

# Debug state
_debug_state = {
    'start_time': time.time(),
    'breakpoints_hit': defaultdict(int),
    'objects_created': [],
    'objects_freed': [],
    'shapes_freed': [],
    'gc_runs': [],
    'corruption_detected': False,
    'object_history': {},
    'global_object': None,
    'init_browser_stubs_active': False,
}

class JSObjectTracker:
    """Tracks JSObject structure from memory"""
    
    @staticmethod
    def read_object(process, addr):
        error = lldb.SBError()
        data = process.ReadMemory(addr, 64, error)
        if not error.Success() or len(data) < 24:
            return None
        return {
            'addr': addr,
            'class_id': struct.unpack_from('<H', data, 0)[0],
            'flags': data[2] if len(data) > 2 else 0,
            'weakref_count': struct.unpack_from('<I', data, 4)[0],
            'shape': struct.unpack_from('<Q', data, 8)[0],
            'prop': struct.unpack_from('<Q', data, 16)[0],
        }
    
    @staticmethod
    def is_valid_object(obj_data):
        if not obj_data:
            return False
        return 1 <= obj_data['class_id'] <= 255
    
    @staticmethod
    def check_corruption(obj_data, context=""):
        if not obj_data:
            return True
        shape = obj_data['shape']
        if shape < 0x1000 or shape > 0x7f0000000000:
            print(f"\n{'!'*80}")
            print(f"!!! SHAPE CORRUPTION DETECTED {context} !!!")
            print(f"!!! Object at 0x{obj_data['addr']:x}")
            print(f"!!! class_id: {obj_data['class_id']}")
            print(f"!!! shape: 0x{shape:x} (INVALID)")
            print(f"{'!'*80}")
            return True
        return False


def log(msg):
    print(f"[LLDB-Driver] {msg}")
    sys.stdout.flush()


def setup_breakpoints(target):
    """Set up all debugging breakpoints"""
    log("Setting up comprehensive breakpoint network...")
    
    breakpoints = []
    
    # Layer 1: Object lifecycle
    bp = target.BreakpointCreateByName("js_new_object")
    bp.SetScriptCallbackFunction("lldb_driver.on_js_new_object")
    log(f"  [BP] js_new_object (ID: {bp.GetID()})")
    breakpoints.append(bp)
    
    bp = target.BreakpointCreateByName("js_free_object")
    bp.SetScriptCallbackFunction("lldb_driver.on_js_free_object")
    log(f"  [BP] js_free_object (ID: {bp.GetID()})")
    breakpoints.append(bp)
    
    # Layer 2: Shape tracking
    bp = target.BreakpointCreateByName("js_new_shape_nohash")
    bp.SetScriptCallbackFunction("lldb_driver.on_js_new_shape")
    log(f"  [BP] js_new_shape_nohash (ID: {bp.GetID()})")
    breakpoints.append(bp)
    
    bp = target.BreakpointCreateByName("js_free_shape0")
    bp.SetScriptCallbackFunction("lldb_driver.on_js_free_shape")
    log(f"  [BP] js_free_shape0 (ID: {bp.GetID()})")
    breakpoints.append(bp)
    
    # Layer 3: Critical functions
    bp = target.BreakpointCreateByName("init_browser_stubs")
    bp.SetScriptCallbackFunction("lldb_driver.on_init_browser_stubs")
    log(f"  [BP] init_browser_stubs (ID: {bp.GetID()})")
    breakpoints.append(bp)
    
    bp = target.BreakpointCreateByName("JS_GetPropertyStr")
    bp.SetScriptCallbackFunction("lldb_driver.on_js_get_property")
    log(f"  [BP] JS_GetPropertyStr (ID: {bp.GetID()})")
    breakpoints.append(bp)
    
    bp = target.BreakpointCreateByName("JS_SetPropertyStr")
    bp.SetScriptCallbackFunction("lldb_driver.on_js_set_property")
    log(f"  [BP] JS_SetPropertyStr (ID: {bp.GetID()}) - CRASH WATCH")
    breakpoints.append(bp)
    
    bp = target.BreakpointCreateByName("JS_SetPropertyInternal")
    bp.SetScriptCallbackFunction("lldb_driver.on_js_set_property_internal")
    log(f"  [BP] JS_SetPropertyInternal (ID: {bp.GetID()})")
    breakpoints.append(bp)
    
    bp = target.BreakpointCreateByName("find_own_property")
    bp.SetScriptCallbackFunction("lldb_driver.on_find_own_property")
    log(f"  [BP] find_own_property (ID: {bp.GetID()}) - CRASH POINT")
    breakpoints.append(bp)
    
    bp = target.BreakpointCreateByName("add_property")
    bp.SetScriptCallbackFunction("lldb_driver.on_add_property")
    log(f"  [BP] add_property (ID: {bp.GetID()})")
    breakpoints.append(bp)
    
    log(f"All {len(breakpoints)} breakpoints set up")
    return breakpoints


# ===== BREAKPOINT CALLBACKS =====

def on_js_new_object(frame, bp_loc, dict):
    _debug_state['objects_created'].append({'time': time.time()})
    return False

def on_js_free_object(frame, bp_loc, dict):
    obj_addr = int(frame.FindRegister("x1").GetValue(), 16)
    _debug_state['objects_freed'].append({'time': time.time(), 'addr': obj_addr})
    if obj_addr in _debug_state['object_history']:
        _debug_state['object_history'][obj_addr]['freed_at'] = time.time()
    return False

def on_js_new_shape(frame, bp_loc, dict):
    _debug_state['shapes_freed'].append({'time': time.time()})  # Track allocations too
    return False

def on_js_free_shape(frame, bp_loc, dict):
    shape_addr = int(frame.FindRegister("x1").GetValue(), 16)
    _debug_state['shapes_freed'].append({'time': time.time(), 'addr': shape_addr})
    return False

def on_init_browser_stubs(frame, bp_loc, dict):
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    print(f"\n{'='*80}")
    print("[INIT_BROWSER_STUBS] ENTERING")
    print(f"{'='*80}")
    
    _debug_state['init_browser_stubs_active'] = True
    
    ctx = int(frame.FindRegister("x0").GetValue(), 16)
    global_obj = int(frame.FindRegister("x1").GetValue(), 16)
    
    print(f"  ctx = 0x{ctx:x}")
    print(f"  global = 0x{global_obj:x}")
    _debug_state['global_object'] = global_obj
    
    obj_data = JSObjectTracker.read_object(process, global_obj)
    if obj_data:
        print(f"  global->class_id = {obj_data['class_id']}")
        print(f"  global->shape = 0x{obj_data['shape']:x}")
        _debug_state['object_history'][global_obj] = {
            'created_at': time.time(),
            'current_shape': obj_data['shape'],
            'shape_history': [(time.time(), obj_data['shape'], 'init_browser_stubs_start')],
            'type': 'global_object',
        }
    
    return False

def on_js_get_property(frame, bp_loc, dict):
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
    
    if obj_val > 0x1000 and obj_val < 0x7f0000000000:
        obj_data = JSObjectTracker.read_object(process, obj_val)
        if obj_data and JSObjectTracker.is_valid_object(obj_data):
            if _debug_state['init_browser_stubs_active']:
                print(f"\n[GET_PROP] JS_GetPropertyStr('{prop_name}')")
                print(f"           obj=0x{obj_val:x}, shape=0x{obj_data['shape']:x}")
                
                if JSObjectTracker.check_corruption(obj_data, f"GET '{prop_name}'"):
                    _debug_state['corruption_detected'] = True
                    return analyze_and_stop(frame, obj_data)
    
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
    
    print(f"\n[SET_PROP] JS_SetPropertyStr('{prop_name}')")
    print(f"           obj=0x{obj_val:x}")
    
    if obj_val > 0x1000 and obj_val < 0x7f0000000000:
        obj_data = JSObjectTracker.read_object(process, obj_val)
        if obj_data:
            print(f"           class_id={obj_data['class_id']}, shape=0x{obj_data['shape']:x}")
            
            if JSObjectTracker.check_corruption(obj_data, f"SET '{prop_name}'"):
                _debug_state['corruption_detected'] = True
                return analyze_and_stop(frame, obj_data, prop_name)
            
            if obj_val in _debug_state['object_history']:
                _debug_state['object_history'][obj_val]['shape_history'].append(
                    (time.time(), obj_data['shape'], f'SET {prop_name}')
                )
                _debug_state['object_history'][obj_val]['current_shape'] = obj_data['shape']
    
    return False

def on_js_set_property_internal(frame, bp_loc, dict):
    thread = frame.GetThread()
    process = thread.GetProcess()
    obj_val = int(frame.FindRegister("x1").GetValue(), 16)
    
    if obj_val > 0x1000 and obj_val < 0x7f0000000000:
        obj_data = JSObjectTracker.read_object(process, obj_val)
        if obj_data and JSObjectTracker.check_corruption(obj_data, "SetPropertyInternal"):
            _debug_state['corruption_detected'] = True
            return analyze_and_stop(frame, obj_data)
    return False

def on_find_own_property(frame, bp_loc, dict):
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    psh = int(frame.FindRegister("x0").GetValue(), 16)
    print(f"\n[FIND_OWN_PROP] psh=0x{psh:x}")
    
    if psh < 0x1000:
        print("  ERROR: psh is NULL!")
        return True
    
    error = lldb.SBError()
    obj_ptr = process.ReadPointerFromMemory(psh, error)
    if not error.Success():
        print(f"  ERROR reading obj: {error.GetCString()}")
        return True
    
    print(f"  obj=0x{obj_ptr:x}")
    
    if obj_ptr < 0x1000 or obj_ptr > 0x7f0000000000:
        print("  ERROR: obj is invalid!")
        return True
    
    obj_data = JSObjectTracker.read_object(process, obj_ptr)
    if obj_data:
        print(f"  shape=0x{obj_data['shape']:x}")
        if obj_data['shape'] < 0x1000:
            print(f"\n{'!'*80}")
            print("!!! CRASH IMMINENT - INVALID SHAPE !!!")
            print(f"{'!'*80}")
            return analyze_and_stop(frame, obj_data)
    
    return False

def on_add_property(frame, bp_loc, dict):
    thread = frame.GetThread()
    process = thread.GetProcess()
    p = int(frame.FindRegister("x1").GetValue(), 16)
    
    if p > 0x1000:
        obj_data = JSObjectTracker.read_object(process, p)
        if obj_data:
            print(f"\n[ADD_PROP] obj=0x{p:x}, shape=0x{obj_data['shape']:x}")
            if JSObjectTracker.check_corruption(obj_data, "add_property"):
                return analyze_and_stop(frame, obj_data)
    return False

def analyze_and_stop(frame, obj_data, context=""):
    """Analyze corruption and stop execution"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    print(f"\n{'='*80}")
    print("CORRUPTION ANALYSIS")
    print(f"{'='*80}")
    
    print(f"\n--- Object Details ---")
    print(f"  Address: 0x{obj_data['addr']:x}")
    print(f"  Class ID: {obj_data['class_id']}")
    print(f"  Shape: 0x{obj_data['shape']:x} (INVALID)")
    print(f"  Prop: 0x{obj_data['prop']:x}")
    
    if obj_data['addr'] in _debug_state['object_history']:
        history = _debug_state['object_history'][obj_data['addr']]
        print(f"\n--- History ---")
        for ts, shape, event in history.get('shape_history', []):
            status = "OK" if shape > 0x1000 else "BAD"
            print(f"  [{ts:.3f}] {event}: 0x{shape:x} [{status}]")
    
    print(f"\n--- Memory Context ---")
    error = lldb.SBError()
    for offset in [-64, -32, -16, 0, 16, 32, 64]:
        addr = obj_data['addr'] + offset
        data = process.ReadMemory(addr, 16, error)
        if error.Success():
            hex_str = ' '.join(f'{b:02x}' for b in data)
            marker = " <-- JSObject" if offset == 0 else ""
            print(f"  0x{addr:x}: {hex_str}{marker}")
    
    print(f"\n--- Registers ---")
    for reg in ['x0', 'x1', 'x2', 'x3', 'x4', 'x19', 'x20', 'lr']:
        val = frame.FindRegister(reg)
        print(f"  {reg:3}: {val.GetValue()}")
    
    print(f"\n--- Backtrace ---")
    for i, f in enumerate(thread.frames[:12]):
        func = f.GetFunctionName() or "???"
        pc = f.GetPCAddress().GetLoadAddress(thread.GetProcess().GetTarget())
        print(f"  #{i}: 0x{pc:x} {func}")
    
    print(f"\n{'='*80}")
    print("STOPPING FOR MANUAL INSPECTION")
    print(f"{'='*80}")
    
    return True


def main():
    log("="*80)
    log("COMPREHENSIVE QUICKJS SHAPE CORRUPTION DEBUGGER")
    log("="*80)
    
    # Pre-flight checks
    log("Pre-flight checks...")
    result = subprocess.run(["adb", "devices"], capture_output=True, text=True)
    if "device" not in result.stdout:
        log("ERROR: No device connected")
        return 1
    log("  ✓ Device connected")
    
    result = subprocess.run(["adb", "shell", "pm", "list", "packages"], capture_output=True, text=True)
    if APP_PACKAGE not in result.stdout:
        log(f"ERROR: App {APP_PACKAGE} not installed")
        return 1
    log("  ✓ App installed")
    
    # Setup environment
    log("Setting up debug environment...")
    subprocess.run(["adb", "shell", "am", "force-stop", APP_PACKAGE], capture_output=True)
    subprocess.run(["adb", "shell", "pkill", "-9", "-f", "lldb-server"], capture_output=True)
    time.sleep(1)
    
    subprocess.run(["adb", "logcat", "-c"], capture_output=True)
    subprocess.run(["adb", "forward", "tcp:5039", "tcp:5039"], capture_output=True)
    log("  ✓ Environment ready")
    
    # Start lldb-server
    log("Starting lldb-server...")
    subprocess.Popen(["adb", "shell", "/data/local/tmp/lldb-server", 
                      "platform", "--listen", "'*:5039'", "--server"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    
    result = subprocess.run(["adb", "shell", "ps", "-A"], capture_output=True, text=True)
    if "lldb-server" not in result.stdout:
        log("ERROR: lldb-server failed to start")
        return 1
    log("  ✓ lldb-server running")
    
    # Start app normally (not in debug mode)
    log("Starting app...")
    subprocess.run(["adb", "shell", "am", "start", "-n", f"{APP_PACKAGE}/.MainActivity"],
                   capture_output=True)
    time.sleep(3)
    
    # Get PID
    result = subprocess.run(["adb", "shell", "pidof", APP_PACKAGE], capture_output=True, text=True)
    if result.returncode != 0 or not result.stdout.strip():
        log("ERROR: Could not get app PID")
        return 1
    pid = int(result.stdout.strip())
    log(f"  ✓ App PID: {pid}")
    
    # Create debugger
    log("Creating LLDB debugger...")
    debugger = lldb.SBDebugger.Create()
    debugger.SetAsync(False)
    
    # Connect to platform
    error = lldb.SBError()
    debugger.HandleCommand("platform select remote-android")
    debugger.HandleCommand("platform connect connect://localhost:5039")
    log("  ✓ Connected to remote platform")
    
    # Attach to process using platform
    log(f"Attaching to PID {pid}...")
    
    # Create empty target first
    target = debugger.CreateTarget("")
    if not target:
        log("ERROR: Failed to create target")
        return 1
    
    # Get the connected platform and attach
    platform = debugger.GetSelectedPlatform()
    
    # Use platform attach
    attach_info = lldb.SBAttachInfo(pid)
    error = lldb.SBError()
    process = target.Attach(attach_info, error)
    
    if error.Fail() or not process:
        log(f"ERROR: Failed to attach: {error.GetCString()}")
        return 1
    
    log(f"  ✓ Attached successfully! (PID: {process.GetProcessID()})")
    
    # Set up breakpoints
    setup_breakpoints(target)
    
    # Check if breakpoints have locations (are resolved)
    log("Checking breakpoint resolution...")
    for i in range(1, 11):  # Breakpoints 1-10
        bp = target.FindBreakpointByID(i)
        if bp.IsValid():
            num_locs = bp.GetNumLocations()
            log(f"  BP {i}: {num_locs} location(s)")
    
    # Trigger the crash by entering YouTube URL (process is already running)
    log("Triggering crash - entering YouTube URL...")
    subprocess.run(["adb", "shell", "input", "tap", "540", "600"])
    time.sleep(0.5)
    subprocess.run(["adb", "shell", "input", "keyevent", "--longpress", "67", "67", "67", "67", "67"])
    time.sleep(0.5)
    subprocess.run(["adb", "shell", "input", "text", "https://www.youtube.com/watch?v=dQw4w9WgXcQ"])
    time.sleep(0.5)
    subprocess.run(["adb", "shell", "input", "keyevent", "66"])
    log("URL submitted - waiting for crash...")
    
    # Main loop - wait for breakpoint hit or crash
    log("Running - will stop when corruption detected...")
    print(f"\n{'='*80}")
    
    while True:
        state = process.GetState()
        
        if state == lldb.eStateStopped:
            thread = process.GetSelectedThread()
            stop_reason = thread.GetStopReason()
            
            if stop_reason == lldb.eStopReasonBreakpoint:
                # Breakpoint hit - callback already ran
                continue
            elif stop_reason == lldb.eStopReasonSignal:
                # Get signal info
                signo = thread.GetStopReasonDataAtIndex(0)
                print(f"\n{'!'*80}")
                print(f"!!! PROCESS STOPPED WITH SIGNAL {signo} !!!")
                print(f"{'!'*80}")
                # Print backtrace for all threads
                for t in process.threads:
                    print(f"\nThread {t.GetIndexID()}: {t.GetName()}")
                    for i, f in enumerate(t.frames[:5]):
                        func = f.GetFunctionName() or "???"
                        pc = f.GetPCAddress().GetLoadAddress(target)
                        print(f"  #{i}: 0x{pc:x} {func}")
                break
            else:
                print(f"\nProcess stopped (reason: {stop_reason})")
                break
        elif state == lldb.eStateExited:
            print(f"\nProcess exited")
            break
        elif state == lldb.eStateCrashed:
            print(f"\n{'!'*80}")
            print("!!! PROCESS CRASHED !!!")
            print(f"{'!'*80}")
            break
    
    # Final analysis
    print(f"\n{'='*80}")
    print("SESSION SUMMARY")
    print(f"{'='*80}")
    print(f"Corruption detected: {_debug_state['corruption_detected']}")
    print(f"Objects created: {len(_debug_state['objects_created'])}")
    print(f"Objects freed: {len(_debug_state['objects_freed'])}")
    print(f"Shapes freed: {len(_debug_state['shapes_freed'])}")
    
    # Interactive mode
    print(f"\n{'='*80}")
    print("INTERACTIVE MODE - Type LLDB commands (quit to exit)")
    print(f"{'='*80}")
    debugger.RunCommandInterpreter(True, False, None, False, False)
    
    # Cleanup
    lldb.SBDebugger.Destroy(debugger)
    return 0

if __name__ == "__main__":
    sys.exit(main())
