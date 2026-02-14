#!/usr/bin/env python3
"""
Advanced LLDB script for debugging QuickJS shape corruption.
Uses watchpoints, memory inspection, and conditional breakpoints.
"""

import lldb

# Global state to track objects we're monitoring
monitored_objects = {}

def __lldb_init_module(debugger, internal_dict):
    """Initialize the module"""
    print("""
╔══════════════════════════════════════════════════════════════╗
║           QuickJS Shape Corruption Debugger v2.0             ║
╚══════════════════════════════════════════════════════════════╝
Commands:
  start-shape-debug    - Start comprehensive shape debugging
  check-obj <addr>     - Inspect a JSObject at address
  watch-shape <addr>   - Set watchpoint on shape field
  find-bad-shape       - Scan for objects with invalid shapes
""")
    
    debugger.HandleCommand('command script add -f lldb_advanced_shape.start_debug start-shape-debug')
    debugger.HandleCommand('command script add -f lldb_advanced_shape.check_obj check-obj')
    debugger.HandleCommand('command script add -f lldb_advanced_shape.watch_shape watch-shape')
    debugger.HandleCommand('command script add -f lldb_advanced_shape.find_bad_shape find-bad-shape')
    debugger.HandleCommand('command script add -f lldb_advanced_shape.show_globals show-globals')

def start_debug(debugger, command, result, internal_dict):
    """Start comprehensive debugging"""
    target = debugger.GetSelectedTarget()
    
    print("[ShapeDebug] Starting comprehensive shape debugging...")
    
    # Create breakpoint at init_browser_stubs
    bp = target.BreakpointCreateByName("init_browser_stubs")
    bp.SetScriptCallbackFunction("lldb_advanced_shape.on_enter_init_browser_stubs")
    print(f"[+] Breakpoint at init_browser_stubs (ID: {bp.GetID()})")
    
    # Create breakpoint at JS_NewCFunction2 to track object creation
    bp2 = target.BreakpointCreateByName("JS_NewCFunction2")
    bp2.SetScriptCallbackFunction("lldb_advanced_shape.on_new_cfunction")
    print(f"[+] Breakpoint at JS_NewCFunction2 (ID: {bp2.GetID()})")
    
    # Create breakpoint at find_own_property with condition
    bp3 = target.BreakpointCreateByName("find_own_property")
    bp3.SetScriptCallbackFunction("lldb_advanced_shape.on_find_own_property")
    print(f"[+] Breakpoint at find_own_property (ID: {bp3.GetID()})")
    
    # Create breakpoint at JS_SetConstructor
    bp4 = target.BreakpointCreateByName("JS_SetConstructor")
    bp4.SetScriptCallbackFunction("lldb_advanced_shape.on_set_constructor")
    print(f"[+] Breakpoint at JS_SetConstructor (ID: {bp4.GetID()})")
    
    print("[ShapeDebug] Setup complete. Type 'continue' to run.")

def on_enter_init_browser_stubs(frame, bp_loc, dict):
    """Called when entering init_browser_stubs"""
    print("\n" + "="*60)
    print("[ShapeDebug] ENTERED init_browser_stubs")
    print("="*60)
    
    # Get thread and process
    thread = frame.GetThread()
    process = thread.GetProcess()
    target = process.GetTarget()
    
    # Get arguments
    ctx = frame.FindRegister("x0")
    global_obj = frame.FindRegister("x1")
    
    print(f"[Args] ctx = {ctx.GetValue()}")
    print(f"[Args] global = {global_obj.GetValue()}")
    
    # Set a breakpoint at line 2618 (where the crash happens)
    # This is approximate - we need to find the actual line
    res = lldb.SBCommandReturnObject()
    target.GetDebugger().GetCommandInterpreter().HandleCommand(
        "breakpoint set -n JS_SetPropertyStr -c '(int)((JSObject*)$x1)->shape < 0x1000'", 
        res
    )
    print(f"[+] Set conditional breakpoint on JS_SetPropertyStr")
    
    return False  # Continue execution

def on_new_cfunction(frame, bp_loc, dict):
    """Track creation of C functions - these become constructors"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    ctx = frame.FindRegister("x0")
    func_name_ptr = frame.FindRegister("x1")
    
    # Read the function name
    error = lldb.SBError()
    name_bytes = process.ReadMemory(int(func_name_ptr.GetValue(), 16), 32, error)
    if error.Success() and name_bytes:
        try:
            name = name_bytes.split(b'\x00')[0].decode('utf-8')
            print(f"\n[ShapeDebug] Creating C function: '{name}'")
            
            if name == "DOMException":
                print("[!!!] DOMException constructor being created - will monitor")
                # Store this for later monitoring
                monitored_objects['dom_exception_creating'] = True
        except:
            pass
    
    return False

def on_set_constructor(frame, bp_loc, dict):
    """Track when JS_SetConstructor is called"""
    thread = frame.GetThread()
    
    func_obj = frame.FindRegister("x1")
    proto = frame.FindRegister("x2")
    
    func_val = int(func_obj.GetValue(), 16)
    proto_val = int(proto.GetValue(), 16)
    
    print(f"\n[ShapeDebug] JS_SetConstructor called:")
    print(f"  func_obj = 0x{func_val:x}")
    print(f"  proto = 0x{proto_val:x}")
    
    # Inspect the function object
    if func_val > 0x1000:
        inspect_object(frame, func_val, "func_obj")
    
    return False

def on_find_own_property(frame, bp_loc, dict):
    """Called when find_own_property is entered"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    psh = frame.FindRegister("x0")
    p_val = int(psh.GetValue(), 16)
    
    print(f"\n[ShapeDebug] find_own_property called")
    print(f"  psh (JSObject**) = 0x{p_val:x}")
    
    if p_val < 0x1000:
        print("  WARNING: psh is invalid!")
        return True  # Stop
    
    # Read the JSObject pointer
    error = lldb.SBError()
    obj_ptr = process.ReadPointerFromMemory(p_val, error)
    if not error.Success():
        print(f"  ERROR reading object pointer: {error.GetCString()}")
        return True  # Stop
    
    print(f"  JSObject* = 0x{obj_ptr:x}")
    
    if obj_ptr < 0x1000 or obj_ptr > 0x7f0000000000:
        print("  WARNING: JSObject* is invalid!")
        return True  # Stop
    
    # Read the shape pointer (offset 8 in JSObject)
    shape_ptr = process.ReadPointerFromMemory(obj_ptr + 8, error)
    if not error.Success():
        print(f"  ERROR reading shape: {error.GetCString()}")
        return True  # Stop
    
    print(f"  shape = 0x{shape_ptr:x}")
    
    # Check if shape is invalid
    if shape_ptr < 0x1000 or shape_ptr > 0x7f0000000000:
        print("  *** INVALID SHAPE POINTER - CRASH IMMINENT ***")
        print("\nBacktrace:")
        for i, f in enumerate(thread.frames[:8]):
            print(f"    #{i}: {f.GetFunctionName()} @ {f.GetPCAddress()}")
        
        # Print more details about the object
        print("\nObject details:")
        inspect_object(frame, obj_ptr, "corrupted_obj")
        
        return True  # Stop for inspection
    
    return False  # Continue

def inspect_object(frame, addr, name):
    """Inspect a JSObject at the given address"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    error = lldb.SBError()
    
    print(f"\n  --- JSObject at 0x{addr:x} ({name}) ---")
    
    # Read JSObject structure
    # Offset 0: class_id (uint16_t) + flags (bitfield)
    # Offset 4: weakref_count (uint32_t)
    # Offset 8: shape (JSShape*)
    # Offset 16: prop (JSProperty*)
    
    # Read class_id (2 bytes at offset 0)
    data = process.ReadMemory(addr, 24, error)
    if error.Success() and len(data) >= 24:
        import struct
        class_id = struct.unpack_from('<H', data, 0)[0]
        weakref_count = struct.unpack_from('<I', data, 4)[0]
        shape = struct.unpack_from('<Q', data, 8)[0]  # 64-bit pointer
        prop = struct.unpack_from('<Q', data, 16)[0]  # 64-bit pointer
        
        print(f"    class_id = {class_id}")
        print(f"    weakref_count = {weakref_count}")
        print(f"    shape = 0x{shape:x}")
        print(f"    prop = 0x{prop:x}")
        
        # If shape looks valid, read it too
        if shape > 0x1000 and shape < 0x7f0000000000:
            shape_data = process.ReadMemory(shape, 48, error)
            if error.Success() and len(shape_data) >= 48:
                is_hashed = shape_data[0]
                hash_val = struct.unpack_from('<I', shape_data, 4)[0]
                prop_hash_mask = struct.unpack_from('<I', shape_data, 8)[0]
                prop_size = struct.unpack_from('<i', shape_data, 12)[0]
                prop_count = struct.unpack_from('<i', shape_data, 16)[0]
                
                print(f"    Shape details:")
                print(f"      is_hashed = {is_hashed}")
                print(f"      hash = {hash_val}")
                print(f"      prop_hash_mask = {prop_hash_mask}")
                print(f"      prop_size = {prop_size}")
                print(f"      prop_count = {prop_count}")
    else:
        print(f"    ERROR reading object: {error.GetCString()}")

def check_obj(debugger, command, result, internal_dict):
    """Command: check-obj <address> - Inspect a JSObject"""
    if not command:
        print("Usage: check-obj <address>")
        return
    
    try:
        addr = int(command, 16) if command.startswith('0x') else int(command)
    except:
        print(f"Invalid address: {command}")
        return
    
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    inspect_object(frame, addr, "user_request")

def watch_shape(debugger, command, result, internal_dict):
    """Command: watch-shape <obj_address> - Set watchpoint on shape field"""
    if not command:
        print("Usage: watch-shape <JSObject_address>")
        return
    
    try:
        addr = int(command, 16) if command.startswith('0x') else int(command)
    except:
        print(f"Invalid address: {command}")
        return
    
    target = debugger.GetSelectedTarget()
    shape_field_addr = addr + 8  # shape is at offset 8
    
    # Set a watchpoint on the shape field
    error = lldb.SBError()
    wp = target.WatchAddress(shape_field_addr, 8, False, True, error)
    
    if error.Success():
        print(f"[+] Watchpoint set on shape field at 0x{shape_field_addr:x}")
        print(f"    Watchpoint ID: {wp.GetID()}")
        wp.SetScriptCallbackFunction("lldb_advanced_shape.on_shape_changed")
    else:
        print(f"[-] Failed to set watchpoint: {error.GetCString()}")

def on_shape_changed(frame, wp_loc, dict):
    """Called when a watched shape field changes"""
    print("\n" + "!"*60)
    print("[ShapeDebug] SHAPE FIELD MODIFIED!")
    print("!"*60)
    
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # Get the watchpoint address
    wp = wp_loc.GetWatchpoint()
    addr = wp.GetWatchAddress()
    obj_addr = addr - 8
    
    print(f"Object address: 0x{obj_addr:x}")
    print(f"Shape field address: 0x{addr:x}")
    
    # Read the new shape value
    error = lldb.SBError()
    new_shape = process.ReadPointerFromMemory(addr, error)
    print(f"New shape value: 0x{new_shape:x}")
    
    # Print backtrace
    print("\nBacktrace:")
    for i, f in enumerate(thread.frames[:10]):
        print(f"  #{i}: {f.GetFunctionName()}")
    
    return True  # Stop so we can inspect

def find_bad_shape(debugger, command, result, internal_dict):
    """Command: find-bad-shape - Scan heap for objects with invalid shapes"""
    print("Scanning for objects with invalid shapes...")
    print("(This may take a while and require heap knowledge)")
    print("Not fully implemented - use check-obj on specific addresses instead")

def show_globals(debugger, command, result, internal_dict):
    """Command: show-globals - Show global variables related to shapes"""
    target = debugger.GetSelectedTarget()
    
    # Try to find and display shape_hash_count, shape_hash_bits, etc.
    print("Global shape statistics:")
    
    for module in target.module_iter():
        for sym in module:
            name = sym.GetName()
            if name and ("shape_hash" in name or "js_class" in name):
                print(f"  {name}: {sym.GetAddress()}")

if __name__ == "__main__":
    print("Load with: command script import lldb_advanced_shape.py")
