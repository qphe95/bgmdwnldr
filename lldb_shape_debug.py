#!/usr/bin/env python3
"""
Advanced LLDB debugging script for shape corruption issue.
This script sets up watchpoints and conditional breakpoints to catch
the exact moment when JSObject->shape becomes invalid.
"""

import lldb

def __lldb_init_module(debugger, internal_dict):
    """Called when module is loaded"""
    print("[ShapeDebug] Module loaded. Use 'shape-debug-start' to begin.")
    debugger.HandleCommand('command script add -f lldb_shape_debug.start_shape_debug shape-debug-start')
    debugger.HandleCommand('command script add -f lldb_shape_debug.bt_shape bt-shape')

def start_shape_debug(debugger, command, result, internal_dict):
    """
    Start debugging the shape corruption issue.
    Sets up breakpoints at key locations.
    """
    target = debugger.GetSelectedTarget()
    
    print("[ShapeDebug] Setting up shape debugging...")
    
    # Breakpoint 1: Break at init_browser_stubs entry
    bp1 = target.BreakpointCreateByName("init_browser_stubs")
    bp1.SetScriptCallbackFunction("lldb_shape_debug.on_init_browser_stubs_entry")
    print(f"[ShapeDebug] Set breakpoint at init_browser_stubs (ID: {bp1.GetID()})")
    
    # Breakpoint 2: Break at JS_SetPropertyStr  
    bp2 = target.BreakpointCreateByName("JS_SetPropertyStr")
    bp2.SetScriptCallbackFunction("lldb_shape_debug.on_set_property")
    print(f"[ShapeDebug] Set breakpoint at JS_SetPropertyStr (ID: {bp2.GetID()})")
    
    # Breakpoint 3: Break at find_own_property (where crash happens)
    bp3 = target.BreakpointCreateByName("find_own_property")
    bp3.SetScriptCallbackFunction("lldb_shape_debug.on_find_own_property")
    print(f"[ShapeDebug] Set breakpoint at find_own_property (ID: {bp3.GetID()})")
    
    # Breakpoint 4: Break at js_new_shape_nohash to track shape creation
    bp4 = target.BreakpointCreateByName("js_new_shape_nohash")
    bp4.SetScriptCallbackFunction("lldb_shape_debug.on_new_shape")
    print(f"[ShapeDebug] Set breakpoint at js_new_shape_nohash (ID: {bp4.GetID()})")
    
    # Breakpoint 5: Break at js_free_shape0 to track shape freeing
    bp5 = target.BreakpointCreateByName("js_free_shape0")
    bp5.SetScriptCallbackFunction("lldb_shape_debug.on_free_shape")
    print(f"[ShapeDebug] Set breakpoint at js_free_shape0 (ID: {bp5.GetID()})")
    
    print("[ShapeDebug] Setup complete. Continue execution.")

def on_init_browser_stubs_entry(frame, bp_loc, dict):
    """Called when init_browser_stubs is entered"""
    print("\n[ShapeDebug] === init_browser_stubs ENTERED ===")
    thread = frame.GetThread()
    process = thread.GetProcess()
    target = process.GetTarget()
    
    # Get the ctx pointer (first argument on ARM64: x0)
    ctx = frame.FindRegister("x0")
    print(f"[ShapeDebug] ctx = {ctx.GetValue()}")
    
    return False  # Don't stop, just log

def on_set_property(frame, bp_loc, dict):
    """Called when JS_SetPropertyStr is called"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    target = process.GetTarget()
    
    # Get arguments: ctx (x0), this_obj (x1), prop (x2), val (x3)
    ctx = frame.FindRegister("x0")
    this_obj = frame.FindRegister("x1")
    prop = frame.FindRegister("x2")
    
    print(f"\n[ShapeDebug] JS_SetPropertyStr called:")
    print(f"[ShapeDebug]   ctx = {ctx.GetValue()}")
    print(f"[ShapeDebug]   this_obj = {this_obj.GetValue()}")
    print(f"[ShapeDebug]   prop = {prop.GetValue()}")
    
    # Try to extract JSObject from this_obj (JSValue)
    # JSValue is a struct with u.ptr and tag
    # On ARM64, it's passed in registers - x1 might be the ptr
    
    error = lldb.SBError()
    obj_ptr = int(this_obj.GetValue(), 16)
    
    # Read the shape pointer from JSObject (offset 8)
    if obj_ptr > 0 and obj_ptr < 0x7f0000000000:  # Sanity check
        shape_ptr = process.ReadPointerFromMemory(obj_ptr + 8, error)
        if error.Success():
            print(f"[ShapeDebug]   JSObject->shape = 0x{shape_ptr:x}")
            
            # Check if shape is valid
            if shape_ptr < 0x1000 or shape_ptr > 0x7f0000000000:
                print(f"[ShapeDebug]   *** WARNING: INVALID SHAPE POINTER! ***")
                print(f"[ShapeDebug]   Stopping for inspection...")
                return True  # Stop here
        else:
            print(f"[ShapeDebug]   ERROR reading shape: {error.GetCString()}")
    
    return False  # Continue

def on_find_own_property(frame, bp_loc, dict):
    """Called when find_own_property is entered"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # psh is first argument (x0 on ARM64)
    psh_reg = frame.FindRegister("x0")
    p = int(psh_reg.GetValue(), 16)
    
    print(f"\n[ShapeDebug] find_own_property called with p=0x{p:x}")
    
    if p > 0 and p < 0x7f0000000000:
        error = lldb.SBError()
        # JSObject shape is at offset 8
        shape = process.ReadPointerFromMemory(p + 8, error)
        if error.Success():
            print(f"[ShapeDebug]   p->shape = 0x{shape:x}")
            if shape < 0x1000:
                print(f"[ShapeDebug]   *** CRASH IMMINENT: shape is NULL/invalid! ***")
                thread.Stop()
                return True
    
    return False

def on_new_shape(frame, bp_loc, dict):
    """Called when js_new_shape_nohash creates a shape"""
    print("\n[ShapeDebug] js_new_shape_nohash called - new shape being created")
    # Don't stop, just log
    return False

def on_free_shape(frame, bp_loc, dict):
    """Called when js_free_shape0 frees a shape"""
    thread = frame.GetThread()
    sh = frame.FindRegister("x1")  # Second argument
    print(f"\n[ShapeDebug] js_free_shape0 called - shape {sh.GetValue()} being freed")
    
    # Get backtrace
    print("[ShapeDebug] Backtrace:")
    for i, f in enumerate(frame.GetThread().frames):
        if i > 5:
            break
        print(f"[ShapeDebug]   #{i}: {f.GetFunctionName()}")
    
    return False

def bt_shape(debugger, command, result, internal_dict):
    """
    Print detailed backtrace with shape-related info.
    """
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    
    print("\n[ShapeDebug] === Detailed Backtrace ===")
    for i, frame in enumerate(thread.frames):
        func_name = frame.GetFunctionName()
        print(f"\n[ShapeDebug] Frame #{i}: {func_name}")
        
        # Print relevant registers for each frame
        if func_name and "init_browser_stubs" in func_name:
            x0 = frame.FindRegister("x0")
            print(f"[ShapeDebug]   x0 (ctx) = {x0.GetValue()}")
        elif func_name and "JS_SetPropertyStr" in func_name:
            x1 = frame.FindRegister("x1")
            print(f"[ShapeDebug]   x1 (this_obj) = {x1.GetValue()}")
        elif func_name and "find_own_property" in func_name:
            x0 = frame.FindRegister("x0")
            print(f"[ShapeDebug]   x0 (psh/obj) = {x0.GetValue()}")

# Entry point for LLDB
if __name__ == "__main__":
    print("Load this script in LLDB with: command script import lldb_shape_debug.py")
