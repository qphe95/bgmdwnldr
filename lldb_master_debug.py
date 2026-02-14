#!/usr/bin/env python3
"""
Master LLDB debugging script for QuickJS shape corruption.
This is the primary debugging script - load this one.
"""

import lldb
import struct

# Module globals
_shape_creation_log = []
_object_shape_map = {}

def __lldb_init_module(debugger, internal_dict):
    """Initialize module and register commands"""
    print("""
╔══════════════════════════════════════════════════════════════════╗
║     QuickJS Shape Corruption Master Debugger                     ║
║                                                                  ║
║  Commands:                                                       ║
║    qjs-debug-start       - Start comprehensive debugging         ║
║    qjs-check-obj <addr>  - Inspect JSObject structure            ║
║    qjs-where-shape       - Track where current shape came from   ║
║    qjs-analyze           - Full analysis of current state        ║
╚══════════════════════════════════════════════════════════════════╝
""")
    
    # Register commands
    debugger.HandleCommand('command script add -f lldb_master_debug.start_debug qjs-debug-start')
    debugger.HandleCommand('command script add -f lldb_master_debug.check_obj qjs-check-obj')
    debugger.HandleCommand('command script add -f lldb_master_debug.where_shape qjs-where-shape')
    debugger.HandleCommand('command script add -f lldb_master_debug.analyze_state qjs-analyze')
    
    # Load sub-modules
    debugger.HandleCommand('command script import /Users/qingpinghe/Documents/bgmdwnldr/lldb_memory_scan.py')

def start_debug(debugger, command, result, internal_dict):
    """Start the comprehensive debugging session"""
    target = debugger.GetSelectedTarget()
    
    print("[ShapeDebug] Setting up comprehensive debugging...")
    
    # Breakpoint 1: init_browser_stubs - track entry
    bp1 = target.BreakpointCreateByName("init_browser_stubs")
    bp1.SetScriptCallbackFunction("lldb_master_debug.on_init_browser_stubs")
    print(f"[+] Breakpoint 1: init_browser_stubs (ID: {bp1.GetID()})")
    
    # Breakpoint 2: JS_NewCFunction2 - track function creation
    bp2 = target.BreakpointCreateByName("JS_NewCFunction2")
    bp2.SetScriptCallbackFunction("lldb_master_debug.on_new_cfunction")
    print(f"[+] Breakpoint 2: JS_NewCFunction2 (ID: {bp2.GetID()})")
    
    # Breakpoint 3: JS_SetConstructor - track constructor setup
    bp3 = target.BreakpointCreateByName("JS_SetConstructor")
    bp3.SetScriptCallbackFunction("lldb_master_debug.on_set_constructor")
    print(f"[+] Breakpoint 3: JS_SetConstructor (ID: {bp3.GetID()})")
    
    # Breakpoint 4: JS_SetPropertyStr - track property setting (with condition)
    bp4 = target.BreakpointCreateByName("JS_SetPropertyStr")
    bp4.SetScriptCallbackFunction("lldb_master_debug.on_set_property")
    print(f"[+] Breakpoint 4: JS_SetPropertyStr (ID: {bp4.GetID()})")
    
    # Breakpoint 5: find_own_property - CRASH LOCATION
    bp5 = target.BreakpointCreateByName("find_own_property")
    bp5.SetScriptCallbackFunction("lldb_master_debug.on_find_own_property")
    print(f"[+] Breakpoint 5: find_own_property (ID: {bp5.GetID()}) - CRASH WATCH")
    
    # Breakpoint 6: add_property - track property additions
    bp6 = target.BreakpointCreateByName("add_property")
    bp6.SetScriptCallbackFunction("lldb_master_debug.on_add_property")
    print(f"[+] Breakpoint 6: add_property (ID: {bp6.GetID()})")
    
    print("\n[ShapeDebug] Setup complete. Type 'continue' to run.")
    print("[ShapeDebug] The debugger will stop automatically when corruption is detected.")

def on_init_browser_stubs(frame, bp_loc, dict):
    """Track init_browser_stubs entry"""
    print("\n" + "="*70)
    print("[ENTRY] init_browser_stubs - Browser stubs initialization starting")
    print("="*70)
    
    thread = frame.GetThread()
    ctx = frame.FindRegister("x0")
    print(f"  ctx = {ctx.GetValue()}")
    
    return False

def on_new_cfunction(frame, bp_loc, dict):
    """Track C function creation"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # Get function name
    name_ptr = frame.FindRegister("x1")
    error = lldb.SBError()
    name_bytes = process.ReadMemory(int(name_ptr.GetValue(), 16), 64, error)
    
    if error.Success() and name_bytes:
        try:
            name = name_bytes.split(b'\x00')[0].decode('utf-8')
            if name == "DOMException":
                print(f"\n[TRACK] Creating DOMException constructor function")
                global _dom_exception_func_obj
                _dom_exception_func_obj = None  # Will be set when we see the return
        except:
            pass
    
    return False

def on_set_constructor(frame, bp_loc, dict):
    """Track JS_SetConstructor calls"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    func_obj = int(frame.FindRegister("x1").GetValue(), 16)
    proto = int(frame.FindRegister("x2").GetValue(), 16)
    
    print(f"\n[CONSTRUCTOR] JS_SetConstructor called")
    print(f"  func_obj = 0x{func_obj:x}")
    print(f"  proto = 0x{proto:x}")
    
    # Inspect the function object
    if func_obj > 0x1000:
        error = lldb.SBError()
        obj_data = process.ReadMemory(func_obj, 24, error)
        if error.Success() and len(obj_data) == 24:
            class_id = struct.unpack_from('<H', obj_data, 0)[0]
            shape = struct.unpack_from('<Q', obj_data, 8)[0]
            print(f"  func_obj->class_id = {class_id}")
            print(f"  func_obj->shape = 0x{shape:x}")
            
            # Store for tracking
            _object_shape_map[func_obj] = {
                'shape': shape,
                'when': 'JS_SetConstructor',
                'frame': frame.GetFunctionName()
            }
    
    return False

def on_set_property(frame, bp_loc, dict):
    """Track JS_SetPropertyStr calls and check for corruption"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # Get this_obj from x1
    this_obj = int(frame.FindRegister("x1").GetValue(), 16)
    prop_ptr = frame.FindRegister("x2")
    
    # Read property name
    error = lldb.SBError()
    prop_bytes = process.ReadMemory(int(prop_ptr.GetValue(), 16), 64, error)
    prop_name = "?"
    if error.Success() and prop_bytes:
        try:
            prop_name = prop_bytes.split(b'\x00')[0].decode('utf-8')
        except:
            pass
    
    # Check if this_obj looks like a JSObject
    if this_obj > 0x1000 and this_obj < 0x7f0000000000:
        obj_data = process.ReadMemory(this_obj, 24, error)
        if error.Success() and len(obj_data) == 24:
            class_id = struct.unpack_from('<H', obj_data, 0)[0]
            shape = struct.unpack_from('<Q', obj_data, 8)[0]
            
            print(f"\n[PROPERTY] JS_SetPropertyStr('{prop_name}')")
            print(f"  this_obj = 0x{this_obj:x}")
            print(f"  class_id = {class_id}")
            print(f"  shape = 0x{shape:x}")
            
            # Check for corruption
            if shape < 0x1000:
                print("\n" + "!"*70)
                print("!!! CORRUPTION DETECTED !!!")
                print(f"!!! Shape pointer is invalid: 0x{shape:x}")
                print("!"*70)
                
                # Print backtrace
                print("\nBacktrace:")
                for i, f in enumerate(thread.frames[:10]):
                    print(f"  #{i}: {f.GetFunctionName()}")
                
                # Store corruption info
                _object_shape_map[this_obj] = {
                    'corrupted_shape': shape,
                    'when': f'JS_SetPropertyStr({prop_name})',
                    'class_id': class_id
                }
                
                return True  # STOP HERE
    
    return False

def on_find_own_property(frame, bp_loc, dict):
    """Monitor find_own_property - the crash location"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    psh = int(frame.FindRegister("x0").GetValue(), 16)
    
    print(f"\n[FIND_PROP] find_own_property called")
    print(f"  psh = 0x{psh:x}")
    
    if psh < 0x1000:
        print("  ERROR: psh is NULL!")
        return True
    
    error = lldb.SBError()
    obj_ptr = process.ReadPointerFromMemory(psh, error)
    if not error.Success():
        print(f"  ERROR reading obj_ptr: {error.GetCString()}")
        return True
    
    print(f"  obj = 0x{obj_ptr:x}")
    
    if obj_ptr < 0x1000 or obj_ptr > 0x7f0000000000:
        print("  ERROR: obj is invalid!")
        return True
    
    # Read shape
    shape_ptr = process.ReadPointerFromMemory(obj_ptr + 8, error)
    if not error.Success():
        print(f"  ERROR reading shape: {error.GetCString()}")
        return True
    
    print(f"  shape = 0x{shape_ptr:x}")
    
    if shape_ptr < 0x1000:
        print("\n" + "!"*70)
        print("!!! CRASH IMMINENT - SHAPE IS INVALID !!!")
        print(f"!!! shape = 0x{shape_ptr:x}")
        print("!"*70)
        
        # Full analysis
        analyze_object_state(thread, obj_ptr)
        
        return True  # STOP!
    
    return False

def on_add_property(frame, bp_loc, dict):
    """Track add_property calls"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    p = int(frame.FindRegister("x1").GetValue(), 16)
    
    if p > 0x1000:
        error = lldb.SBError()
        shape_ptr = process.ReadPointerFromMemory(p + 8, error)
        if error.Success():
            print(f"\n[ADD_PROP] add_property on obj=0x{p:x}, shape=0x{shape_ptr:x}")
    
    return False

def analyze_object_state(thread, obj_addr):
    """Perform full analysis of object state"""
    process = thread.GetProcess()
    error = lldb.SBError()
    
    print("\n" + "="*70)
    print("FULL OBJECT STATE ANALYSIS")
    print("="*70)
    
    # Read object data
    obj_data = process.ReadMemory(obj_addr, 64, error)
    if not error.Success():
        print(f"Failed to read object: {error.GetCString()}")
        return
    
    print(f"\nJSObject at 0x{obj_addr:x}:")
    print("-" * 40)
    
    # Parse structure
    class_id = struct.unpack_from('<H', obj_data, 0)[0]
    flags_byte = obj_data[2] if len(obj_data) > 2 else 0
    weakref_count = struct.unpack_from('<I', obj_data, 4)[0]
    shape = struct.unpack_from('<Q', obj_data, 8)[0]
    prop = struct.unpack_from('<Q', obj_data, 16)[0]
    
    print(f"  Offset 0 (class_id):     {class_id}")
    print(f"  Offset 2 (flags):        0x{flags_byte:02x}")
    print(f"  Offset 4 (weakref_count): {weakref_count}")
    print(f"  Offset 8 (shape):        0x{shape:x} {'VALID' if shape > 0x1000 else 'INVALID!'}")
    print(f"  Offset 16 (prop):        0x{prop:x}")
    
    # Check tracked history
    if obj_addr in _object_shape_map:
        history = _object_shape_map[obj_addr]
        print(f"\n  [HISTORY] Object was tracked:")
        print(f"    Last known good shape: 0x{history.get('shape', 'unknown'):x}")
        print(f"    When: {history.get('when', 'unknown')}")
    
    # If shape is invalid, try to find nearby objects
    if shape < 0x1000:
        print(f"\n  [HEAP SCAN] Searching for nearby valid objects...")
        for offset in range(-1024, 1025, 8):
            test_addr = obj_addr + offset
            if test_addr < 0x1000:
                continue
            test_data = process.ReadMemory(test_addr, 24, error)
            if error.Success() and len(test_data) == 24:
                test_class = struct.unpack_from('<H', test_data, 0)[0]
                test_shape = struct.unpack_from('<Q', test_data, 8)[0]
                if 1 <= test_class <= 200 and test_shape > 0x1000:
                    print(f"    Found valid object at offset {offset:+d}: class={test_class}, shape=0x{test_shape:x}")
    
    # Print backtrace
    print("\nBacktrace:")
    for i, f in enumerate(thread.frames[:12]):
        print(f"  #{i}: {f.GetFunctionName()}")
    
    print("\n" + "="*70)

def check_obj(debugger, command, result, internal_dict):
    """Command: qjs-check-obj <address>"""
    if not command:
        print("Usage: qjs-check-obj <address>")
        return
    
    try:
        addr = int(command, 16) if command.startswith('0x') else int(command)
    except:
        print(f"Invalid address: {command}")
        return
    
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    
    analyze_object_state(thread, addr)

def where_shape(debugger, command, result, internal_dict):
    """Command: qjs-where-shape - Show where the current shape came from"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    # Try to find the shape pointer from current context
    print("Scanning registers for object pointers...")
    
    for reg_name in ['x0', 'x1', 'x2', 'x3', 'x4']:
        reg = frame.FindRegister(reg_name)
        try:
            val = int(reg.GetValue(), 16)
            if val > 0x1000 and val < 0x7f0000000000:
                error = lldb.SBError()
                obj_data = process.ReadMemory(val, 24, error)
                if error.Success():
                    class_id = struct.unpack_from('<H', obj_data, 0)[0]
                    if 1 <= class_id <= 200:
                        shape = struct.unpack_from('<Q', obj_data, 8)[0]
                        print(f"{reg_name} (0x{val:x}): looks like JSObject, class={class_id}, shape=0x{shape:x}")
                        if val in _object_shape_map:
                            print(f"  History: {_object_shape_map[val]}")
        except:
            pass

def analyze_state(debugger, command, result, internal_dict):
    """Command: qjs-analyze - Full state analysis"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    print("="*70)
    print("QUICKJS STATE ANALYSIS")
    print("="*70)
    
    print(f"\nCurrent function: {frame.GetFunctionName()}")
    print(f"Stopped at: {frame.GetPCAddress()}")
    
    # Analyze all registers that might hold object pointers
    print("\n--- Register Analysis ---")
    for reg_name in ['x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x19', 'x20']:
        reg = frame.FindRegister(reg_name)
        try:
            val = int(reg.GetValue(), 16)
            if val > 0x1000 and val < 0x7f0000000000:
                error = lldb.SBError()
                # Try to read as JSObject
                obj_data = process.ReadMemory(val, 24, error)
                if error.Success():
                    class_id = struct.unpack_from('<H', obj_data, 0)[0]
                    shape = struct.unpack_from('<Q', obj_data, 8)[0]
                    if 1 <= class_id <= 200:
                        status = "VALID" if shape > 0x1000 else "CORRUPTED!"
                        print(f"  {reg_name}: 0x{val:x} -> JSObject class={class_id}, shape=0x{shape:x} [{status}]")
        except:
            pass
    
    # Show backtrace
    print("\n--- Backtrace ---")
    for i, f in enumerate(thread.frames[:15]):
        func = f.GetFunctionName() or "???"
        print(f"  #{i}: {func}")
    
    # Show tracked objects
    if _object_shape_map:
        print(f"\n--- Tracked Objects ({len(_object_shape_map)} total) ---")
        for addr, info in list(_object_shape_map.items())[-5:]:
            print(f"  0x{addr:x}: {info}")

if __name__ == "__main__":
    print("Load with: command script import lldb_master_debug.py")
