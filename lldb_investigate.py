#!/usr/bin/env python3
"""
Advanced LLDB Investigation Script for bgmdwnldr crash

This script uses:
- Hardware watchpoints on shape pointers
- Memory inspection
- Automatic backtrace on crash
- Custom data structure inspection
"""

import lldb
import sys

# Global state
watchpoint_count = 0
shape_addresses = set()

def print_banner(msg):
    print(f"\n{'='*60}")
    print(f"  {msg}")
    print(f"{'='*60}\n")

def frame_summary(frame):
    """Get a summary of the current frame"""
    name = frame.GetFunctionName()
    file = frame.GetLineEntry().GetFileSpec().GetFilename()
    line = frame.GetLineEntry().GetLine()
    return f"{name} at {file}:{line}"

def inspect_object_shape(frame, obj_addr):
    """Inspect the shape of a JSObject"""
    target = frame.GetThread().GetProcess().GetTarget()
    error = lldb.SBError()
    
    # JSObject shape field offset (typically at offset 0 or 8 depending on layout)
    # We need to find the shape pointer
    process = frame.GetThread().GetProcess()
    
    # Read first few fields of JSObject
    data = process.ReadMemory(obj_addr, 64, error)
    if not error.Success():
        print(f"  ERROR: Cannot read object at {obj_addr:#x}")
        return None
    
    # Interpret as raw pointers
    import struct
    ptrs = struct.unpack('<' + 'Q'*8, data)
    
    print(f"  JSObject at {obj_addr:#x}:")
    for i, p in enumerate(ptrs):
        print(f"    [{i*8:3d}]: {p:#018x}")
    
    return ptrs

# ============================================================================
# Breakpoint Callbacks
# ============================================================================

def on_find_own_property(frame, bp_loc, internal_dict):
    """Called when find_own_property is hit"""
    print_banner("BREAKPOINT: find_own_property")
    
    thread = frame.GetThread()
    process = thread.GetProcess()
    target = process.GetTarget()
    
    # On ARM64, x0 is the first argument (JSObject* p)
    obj_addr = frame.FindRegister("x0")
    obj_val = int(obj_addr.GetValue(), 0)
    
    print(f"  JSObject pointer: {obj_val:#x}")
    
    # Inspect the object
    ptrs = inspect_object_shape(frame, obj_val)
    
    # Try to read shape pointer (typically at offset 16 or 24)
    if ptrs:
        # Shape is often at a specific offset - let's check common ones
        for offset in [16, 24, 32]:
            shape_ptr = ptrs[offset // 8]
            if shape_ptr > 0x1000 and shape_ptr < 0x7f0000000000:
                print(f"  Possible shape at offset {offset}: {shape_ptr:#x}")
                
                # Check if this shape pointer is valid
                error = lldb.SBError()
                shape_data = process.ReadMemory(shape_ptr, 16, error)
                if error.Success():
                    print(f"    Shape appears VALID (readable)")
                    # Set watchpoint on this shape if we haven't seen it
                    if shape_ptr not in shape_addresses:
                        shape_addresses.add(shape_ptr)
                        set_shape_watchpoint(target, shape_ptr)
                else:
                    print(f"    Shape appears INVALID (unreadable)")
    
    # Continue execution
    return False

def on_js_shape_hash_unlink(frame, bp_loc, internal_dict):
    """Called when js_shape_hash_unlink is hit"""
    print_banner("BREAKPOINT: js_shape_hash_unlink")
    
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # x1 is the shape pointer
    shape_addr = frame.FindRegister("x1")
    shape_val = int(shape_addr.GetValue(), 0)
    
    print(f"  Shape pointer: {shape_val:#x}")
    
    # Get backtrace
    print("\n  Backtrace:")
    for i, f in enumerate(thread.frames):
        if i > 5:
            break
        print(f"    {i}: {frame_summary(f)}")
    
    # Inspect the shape
    error = lldb.SBError()
    shape_data = process.ReadMemory(shape_val, 64, error)
    if error.Success():
        import struct
        words = struct.unpack('<' + 'Q'*8, shape_data)
        print(f"\n  Shape data at {shape_val:#x}:")
        for i, w in enumerate(words):
            print(f"    [{i*8:3d}]: {w:#018x}")
    else:
        print(f"\n  ERROR: Cannot read shape at {shape_val:#x}")
    
    return False

def on_JS_SetPropertyStr(frame, bp_loc, internal_dict):
    """Called when JS_SetPropertyStr is hit"""
    thread = frame.GetThread()
    
    # Get the 'this' object (first argument)
    obj_addr = frame.FindRegister("x0")
    obj_val = int(obj_addr.GetValue(), 0)
    
    # Print concise info
    func_name = frame.GetFunctionName()
    file_spec = frame.GetLineEntry().GetFileSpec()
    line = frame.GetLineEntry().GetLine()
    
    print(f"[JS_SetPropertyStr] obj={obj_val:#x} at {file_spec.GetFilename()}:{line}")
    
    return False

def set_shape_watchpoint(target, shape_addr):
    """Set a watchpoint on a shape pointer to detect corruption"""
    global watchpoint_count
    
    if watchpoint_count >= 4:  # Hardware limit is usually 4
        return
    
    # Watch for writes to the shape
    wp = target.WatchAddress(shape_addr, 8, False, True)
    if wp.IsValid():
        watchpoint_count += 1
        print(f"  [WATCHPOINT {watchpoint_count}] Set on shape at {shape_addr:#x}")

def on_crash_detected(event):
    """Called when the process stops (possibly due to crash)"""
    process = event.GetProcess()
    state = process.GetState()
    
    if state == lldb.eStateStopped:
        thread = process.GetThreadAtIndex(0)
        stop_reason = thread.GetStopReason()
        
        if stop_reason == lldb.eStopReasonException:
            print_banner("CRASH DETECTED!")
            print(f"  Stop reason: {thread.GetStopDescription(1000)}")
            
            frame = thread.GetSelectedFrame()
            print(f"\n  Crashed at: {frame_summary(frame)}")
            
            # Show registers
            print("\n  Registers:")
            for reg in ["x0", "x1", "x2", "pc", "lr", "sp"]:
                val = frame.FindRegister(reg)
                print(f"    {reg:3s}: {val.GetValue()}")
            
            # Show backtrace
            print("\n  Backtrace:")
            for i, f in enumerate(thread.frames):
                print(f"    {i}: {frame_summary(f)}")
            
            # Inspect memory around PC
            pc = frame.FindRegister("pc")
            pc_val = int(pc.GetValue(), 0)
            print(f"\n  Memory around PC ({pc_val:#x}):")
            error = lldb.SBError()
            mem = process.ReadMemory(pc_val - 32, 64, error)
            if error.Success():
                import struct
                words = struct.unpack('<' + 'Q'*8, mem)
                for i, w in enumerate(words):
                    addr = pc_val - 32 + i*8
                    marker = " <-- PC" if addr == pc_val else ""
                    print(f"    {addr:#018x}: {w:#018x}{marker}")
            
            return True
    
    return False

# ============================================================================
# Main debugging session setup
# ============================================================================

def setup_debugging(debugger, command, result, internal_dict):
    """Setup the debugging session"""
    target = debugger.GetSelectedTarget()
    
    print_banner("Setting up advanced debugging")
    
    # Set breakpoints with callbacks
    bp1 = target.BreakpointCreateByName("find_own_property")
    if bp1.IsValid():
        print(f"Set breakpoint on find_own_property (ID: {bp1.GetID()})")
    
    bp2 = target.BreakpointCreateByName("js_shape_hash_unlink")
    if bp2.IsValid():
        print(f"Set breakpoint on js_shape_hash_unlink (ID: {bp2.GetID()})")
    
    bp3 = target.BreakpointCreateByName("JS_SetPropertyStr")
    if bp3.IsValid():
        print(f"Set breakpoint on JS_SetPropertyStr (ID: {bp3.GetID()})")
    
    print("\nContinuing execution...")
    result.AppendMessage("Debugging setup complete")

# Register commands
def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_investigate.setup_debugging setup_debug')
    print("LLDB investigation module loaded.")
    print("Run: setup_debug")
