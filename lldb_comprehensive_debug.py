#!/usr/bin/env python3
"""
Comprehensive LLDB debugging script for QuickJS shape corruption.
This is a multi-layered debugging system that tracks:
- Object creation and destruction
- Shape allocation and deallocation  
- GC activity
- Memory corruption patterns
- JSObject structure evolution
"""

import lldb
import struct
import time
from collections import defaultdict

# Global state tracking
_debug_state = {
    'start_time': None,
    'breakpoints_hit': defaultdict(int),
    'objects_created': [],
    'objects_freed': [],
    'shapes_allocated': [],
    'shapes_freed': [],
    'gc_runs': [],
    'corruption_detected': False,
    'object_history': {},  # addr -> {created_at, shape_history, last_seen}
    'global_object': None,
    'init_browser_stubs_active': False,
}

class JSObjectTracker:
    """Tracks JSObject lifecycle and shape changes"""
    
    @staticmethod
    def read_object(process, addr):
        """Read JSObject structure from memory"""
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
    def read_shape(process, addr):
        """Read JSShape structure from memory"""
        if addr < 0x1000:
            return None
        error = lldb.SBError()
        # JSShape: header(8) + prop_hash_mask(4) + prop_count(4) + prop_size(4) + proto(8) ...
        data = process.ReadMemory(addr, 48, error)
        if not error.Success() or len(data) < 32:
            return None
        
        return {
            'addr': addr,
            'header': struct.unpack_from('<Q', data, 0)[0],
            'prop_hash_mask': struct.unpack_from('<I', data, 8)[0],
            'prop_count': struct.unpack_from('<I', data, 12)[0],
            'prop_size': struct.unpack_from('<I', data, 16)[0],
            'proto': struct.unpack_from('<Q', data, 24)[0],
        }
    
    @staticmethod
    def is_valid_object(obj_data):
        """Check if object data looks like a valid JSObject"""
        if not obj_data:
            return False
        if obj_data['class_id'] < 1 or obj_data['class_id'] > 255:
            return False
        return True
    
    @staticmethod
    def check_corruption(obj_data, context=""):
        """Check for shape corruption and print details"""
        if not obj_data:
            return True
        
        shape = obj_data['shape']
        if shape < 0x1000 or shape > 0x7f0000000000:
            print("\n" + "!"*80)
            print(f"!!! SHAPE CORRUPTION DETECTED {context} !!!")
            print(f"!!! Object at 0x{obj_data['addr']:x}")
            print(f"!!! class_id: {obj_data['class_id']}")
            print(f"!!! shape: 0x{shape:x} (INVALID)")
            print(f"!!! prop: 0x{obj_data['prop']:x}")
            print("!"*80)
            return True
        return False


def __lldb_init_module(debugger, internal_dict):
    """Initialize comprehensive debugging module"""
    print("""
╔══════════════════════════════════════════════════════════════════════════════╗
║          COMPREHENSIVE QUICKJS SHAPE CORRUPTION DEBUGGER                     ║
║                                                                              ║
║  This debugger tracks:                                                       ║
║    • Object creation/destruction (js_new_object/js_free_object)             ║
║    • Shape allocation/freeing (js_new_shape/js_free_shape)                  ║
║    • GC cycles and collections                                              ║
║    • Property access patterns                                               ║
║    • JSObject structure evolution                                           ║
║                                                                              ║
║  Commands:                                                                   ║
║    comprehensive-debug-start  - Start full debugging session                ║
║    track-object <addr>        - Track specific object                       ║
║    dump-object-history        - Show all tracked object histories           ║
║    analyze-corruption         - Deep analysis of corruption state           ║
║    memory-scan <addr> <size>  - Scan memory region for objects              ║
╚══════════════════════════════════════════════════════════════════════════════╝
""")
    
    # Register commands
    debugger.HandleCommand('command script add -f lldb_comprehensive_debug.start_debug comprehensive-debug-start')
    debugger.HandleCommand('command script add -f lldb_comprehensive_debug.track_object track-object')
    debugger.HandleCommand('command script add -f lldb_comprehensive_debug.dump_history dump-object-history')
    debugger.HandleCommand('command script add -f lldb_comprehensive_debug.analyze_corruption analyze-corruption')
    debugger.HandleCommand('command script add -f lldb_comprehensive_debug.memory_scan memory-scan')
    
    _debug_state['start_time'] = time.time()


def start_debug(debugger, command, result, internal_dict):
    """Start comprehensive debugging with all breakpoints"""
    target = debugger.GetSelectedTarget()
    
    print("[ComprehensiveDebug] Setting up extensive breakpoint network...")
    
    # ===== LAYER 1: Object Lifecycle Tracking =====
    bp_new_obj = target.BreakpointCreateByName("js_new_object")
    bp_new_obj.SetScriptCallbackFunction("lldb_comprehensive_debug.on_js_new_object")
    print(f"[Layer 1] Object creation tracking: js_new_object (ID: {bp_new_obj.GetID()})")
    
    bp_free_obj = target.BreakpointCreateByName("js_free_object")
    bp_free_obj.SetScriptCallbackFunction("lldb_comprehensive_debug.on_js_free_object")
    print(f"[Layer 1] Object destruction tracking: js_free_object (ID: {bp_free_obj.GetID()})")
    
    # ===== LAYER 2: Shape Tracking =====
    bp_new_shape = target.BreakpointCreateByName("js_new_shape_nohash")
    bp_new_shape.SetScriptCallbackFunction("lldb_comprehensive_debug.on_js_new_shape")
    print(f"[Layer 2] Shape allocation: js_new_shape_nohash (ID: {bp_new_shape.GetID()})")
    
    bp_free_shape = target.BreakpointCreateByName("js_free_shape0")
    bp_free_shape.SetScriptCallbackFunction("lldb_comprehensive_debug.on_js_free_shape")
    print(f"[Layer 2] Shape freeing: js_free_shape0 (ID: {bp_free_shape.GetID()})")
    
    # ===== LAYER 3: GC Tracking =====
    bp_gc_start = target.BreakpointCreateByName("js_gc_start")
    if bp_gc_start.GetNumLocations() > 0:
        bp_gc_start.SetScriptCallbackFunction("lldb_comprehensive_debug.on_gc_start")
        print(f"[Layer 3] GC start: js_gc_start (ID: {bp_gc_start.GetID()})")
    
    bp_gc_end = target.BreakpointCreateByName("js_gc_end")
    if bp_gc_gc_end.GetNumLocations() > 0:
        bp_gc_end.SetScriptCallbackFunction("lldb_comprehensive_debug.on_gc_end")
        print(f"[Layer 3] GC end: js_gc_end (ID: {bp_gc_end.GetID()})")
    
    # ===== LAYER 4: Critical Function Tracking =====
    bp_init = target.BreakpointCreateByName("init_browser_stubs")
    bp_init.SetScriptCallbackFunction("lldb_comprehensive_debug.on_init_browser_stubs")
    print(f"[Layer 4] init_browser_stubs entry (ID: {bp_init.GetID()})")
    
    bp_get_prop = target.BreakpointCreateByName("JS_GetPropertyStr")
    bp_get_prop.SetScriptCallbackFunction("lldb_comprehensive_debug.on_js_get_property")
    print(f"[Layer 4] Property get: JS_GetPropertyStr (ID: {bp_get_prop.GetID()})")
    
    bp_set_prop = target.BreakpointCreateByName("JS_SetPropertyStr")
    bp_set_prop.SetScriptCallbackFunction("lldb_comprehensive_debug.on_js_set_property")
    print(f"[Layer 4] Property set: JS_SetPropertyStr (ID: {bp_set_prop.GetID()})")
    
    bp_set_prop_internal = target.BreakpointCreateByName("JS_SetPropertyInternal")
    bp_set_prop_internal.SetScriptCallbackFunction("lldb_comprehensive_debug.on_js_set_property_internal")
    print(f"[Layer 4] Property set internal: JS_SetPropertyInternal (ID: {bp_set_prop_internal.GetID()})")
    
    # ===== LAYER 5: Crash Point Watch =====
    bp_find_own = target.BreakpointCreateByName("find_own_property")
    bp_find_own.SetScriptCallbackFunction("lldb_comprehensive_debug.on_find_own_property")
    print(f"[Layer 5] CRASH WATCH: find_own_property (ID: {bp_find_own.GetID()})")
    
    bp_add_prop = target.BreakpointCreateByName("add_property")
    bp_add_prop.SetScriptCallbackFunction("lldb_comprehensive_debug.on_add_property")
    print(f"[Layer 5] Property add: add_property (ID: {bp_add_prop.GetID()})")
    
    print("\n[ComprehensiveDebug] All breakpoints set. Ready to run.")
    print("[ComprehensiveDebug] The debugger will stop when corruption is detected.")


# ===== BREAKPOINT HANDLERS =====

def on_js_new_object(frame, bp_loc, dict):
    """Track object creation"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # Get return address (object will be in x0 after function returns)
    # For now, just note that an object was created
    _debug_state['objects_created'].append({
        'time': time.time(),
        'pc': frame.GetPCAddress().GetLoadAddress(frame.GetThread().GetProcess().GetTarget()),
    })
    return False


def on_js_free_object(frame, bp_loc, dict):
    """Track object destruction"""
    thread = frame.GetThread()
    obj_addr = int(frame.FindRegister("x1").GetValue(), 16)
    
    _debug_state['objects_freed'].append({
        'time': time.time(),
        'addr': obj_addr,
    })
    
    # Mark in history
    if obj_addr in _debug_state['object_history']:
        _debug_state['object_history'][obj_addr]['freed_at'] = time.time()
    
    return False


def on_js_new_shape(frame, bp_loc, dict):
    """Track shape allocation"""
    _debug_state['shapes_allocated'].append({
        'time': time.time(),
        'pc': frame.GetPCAddress().GetLoadAddress(frame.GetThread().GetProcess().GetTarget()),
    })
    return False


def on_js_free_shape(frame, bp_loc, dict):
    """Track shape freeing - POTENTIAL CORRUPTION SOURCE"""
    thread = frame.GetThread()
    shape_addr = int(frame.FindRegister("x1").GetValue(), 16)
    
    _debug_state['shapes_freed'].append({
        'time': time.time(),
        'addr': shape_addr,
    })
    
    print(f"\n[SHAPE_FREE] Shape 0x{shape_addr:x} is being freed!")
    print(f"             Active: {_debug_state['init_browser_stubs_active']}")
    
    # Check if any tracked object uses this shape
    for obj_addr, history in _debug_state['object_history'].items():
        if history.get('current_shape') == shape_addr:
            print(f"!!! WARNING: Object 0x{obj_addr:x} still references freed shape 0x{shape_addr:x} !!!")
    
    return False


def on_gc_start(frame, bp_loc, dict):
    """Track GC start"""
    _debug_state['gc_runs'].append({
        'start_time': time.time(),
        'end_time': None,
    })
    print(f"\n[GC] Garbage collection started")
    return False


def on_gc_end(frame, bp_loc, dict):
    """Track GC end"""
    if _debug_state['gc_runs']:
        _debug_state['gc_runs'][-1]['end_time'] = time.time()
        duration = _debug_state['gc_runs'][-1]['end_time'] - _debug_state['gc_runs'][-1]['start_time']
        print(f"[GC] Garbage collection ended (duration: {duration:.3f}s)")
    return False


def on_init_browser_stubs(frame, bp_loc, dict):
    """Track init_browser_stubs entry and exit"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    print("\n" + "="*80)
    print("[INIT_BROWSER_STUBS] ENTERING - Browser environment initialization")
    print("="*80)
    
    _debug_state['init_browser_stubs_active'] = True
    
    # Get parameters
    ctx = int(frame.FindRegister("x0").GetValue(), 16)
    global_obj = int(frame.FindRegister("x1").GetValue(), 16)
    
    print(f"  ctx = 0x{ctx:x}")
    print(f"  global = 0x{global_obj:x}")
    _debug_state['global_object'] = global_obj
    
    # Inspect global object
    obj_data = JSObjectTracker.read_object(process, global_obj)
    if obj_data:
        print(f"  global->class_id = {obj_data['class_id']}")
        print(f"  global->shape = 0x{obj_data['shape']:x}")
        
        # Track global object
        _debug_state['object_history'][global_obj] = {
            'created_at': time.time(),
            'current_shape': obj_data['shape'],
            'shape_history': [(time.time(), obj_data['shape'], 'init_browser_stubs_start')],
            'type': 'global_object',
        }
    
    # Set up return breakpoint
    target = frame.GetThread().GetProcess().GetTarget()
    return_addr = frame.FindRegister("lr").GetValue()
    bp_return = target.BreakpointCreateByAddress(int(return_addr, 16))
    bp_return.SetOneShot(True)
    bp_return.SetScriptCallbackFunction("lldb_comprehensive_debug.on_init_browser_stubs_return")
    
    return False


def on_init_browser_stubs_return(frame, bp_loc, dict):
    """Handle init_browser_stubs return"""
    print("\n" + "="*80)
    print("[INIT_BROWSER_STUBS] RETURNING")
    print("="*80)
    _debug_state['init_browser_stubs_active'] = False
    
    # Check global object state on return
    if _debug_state['global_object']:
        process = frame.GetThread().GetProcess()
        obj_data = JSObjectTracker.read_object(process, _debug_state['global_object'])
        if obj_data:
            print(f"  global->shape on exit = 0x{obj_data['shape']:x}")
    
    return False


def on_js_get_property(frame, bp_loc, dict):
    """Track JS_GetPropertyStr calls"""
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
    
    # Check if object
    if obj_val > 0x1000 and obj_val < 0x7f0000000000:
        obj_data = JSObjectTracker.read_object(process, obj_val)
        if obj_data and JSObjectTracker.is_valid_object(obj_data):
            if _debug_state['init_browser_stubs_active']:
                print(f"\n[GET_PROP] JS_GetPropertyStr(ctx, 0x{obj_val:x}, '{prop_name}')")
                print(f"           class_id={obj_data['class_id']}, shape=0x{obj_data['shape']:x}")
                
                # Check for corruption
                if JSObjectTracker.check_corruption(obj_data, f"in GET '{prop_name}'"):
                    _debug_state['corruption_detected'] = True
                    return trigger_full_analysis(frame, obj_data)
    
    return False


def on_js_set_property(frame, bp_loc, dict):
    """Track JS_SetPropertyStr calls - WHERE CRASH OCCURS"""
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
    
    print(f"\n[SET_PROP] JS_SetPropertyStr(ctx, 0x{obj_val:x}, '{prop_name}', ...)")
    
    # Check if object
    if obj_val > 0x1000 and obj_val < 0x7f0000000000:
        obj_data = JSObjectTracker.read_object(process, obj_val)
        if obj_data:
            print(f"           class_id={obj_data['class_id']}, shape=0x{obj_data['shape']:x}")
            
            # CRITICAL: Check for corruption
            if JSObjectTracker.check_corruption(obj_data, f"in SET '{prop_name}'"):
                _debug_state['corruption_detected'] = True
                return trigger_full_analysis(frame, obj_data, prop_name)
            
            # Update tracking
            if obj_val in _debug_state['object_history']:
                _debug_state['object_history'][obj_val]['shape_history'].append(
                    (time.time(), obj_data['shape'], f'SET {prop_name}')
                )
                _debug_state['object_history'][obj_val]['current_shape'] = obj_data['shape']
    else:
        print(f"           WARNING: obj_val 0x{obj_val:x} looks invalid!")
    
    return False


def on_js_set_property_internal(frame, bp_loc, dict):
    """Track JS_SetPropertyInternal - deeper inspection"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # x1 = ctx, x2 = obj
    obj_val = int(frame.FindRegister("x1").GetValue(), 16)
    
    if obj_val > 0x1000 and obj_val < 0x7f0000000000:
        obj_data = JSObjectTracker.read_object(process, obj_val)
        if obj_data and JSObjectTracker.check_corruption(obj_data, "in SetPropertyInternal"):
            _debug_state['corruption_detected'] = True
            return trigger_full_analysis(frame, obj_data)
    
    return False


def on_find_own_property(frame, bp_loc, dict):
    """Monitor find_own_property - THE CRASH LOCATION"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # x0 = psh (property hash), which contains obj pointer
    psh = int(frame.FindRegister("x0").GetValue(), 16)
    
    print(f"\n[FIND_OWN_PROP] find_own_property called")
    print(f"                psh = 0x{psh:x}")
    
    if psh < 0x1000:
        print("                ERROR: psh is NULL!")
        return True
    
    # Read obj from psh
    error = lldb.SBError()
    obj_ptr = process.ReadPointerFromMemory(psh, error)
    if not error.Success():
        print(f"                ERROR reading obj: {error.GetCString()}")
        return True
    
    print(f"                obj = 0x{obj_ptr:x}")
    
    if obj_ptr < 0x1000 or obj_ptr > 0x7f0000000000:
        print("                ERROR: obj pointer is invalid!")
        return True
    
    # Read and check shape
    obj_data = JSObjectTracker.read_object(process, obj_ptr)
    if obj_data:
        print(f"                shape = 0x{obj_data['shape']:x}")
        
        if obj_data['shape'] < 0x1000:
            print("\n" + "!"*80)
            print("!!! CRASH IMMINENT - SHAPE IS INVALID !!!")
            print(f"!!! This will cause SIGSEGV at address 0x{obj_data['shape']:x}")
            print("!"*80)
            return trigger_full_analysis(frame, obj_data)
    
    return False


def on_add_property(frame, bp_loc, dict):
    """Track add_property calls"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    p = int(frame.FindRegister("x1").GetValue(), 16)
    
    if p > 0x1000:
        obj_data = JSObjectTracker.read_object(process, p)
        if obj_data:
            print(f"\n[ADD_PROP] add_property(obj=0x{p:x}, shape=0x{obj_data['shape']:x})")
            
            if JSObjectTracker.check_corruption(obj_data, "in add_property"):
                return trigger_full_analysis(frame, obj_data)
    
    return False


def trigger_full_analysis(frame, obj_data, context=""):
    """Trigger comprehensive analysis when corruption is detected"""
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    print("\n" + "="*80)
    print("COMPREHENSIVE CORRUPTION ANALYSIS")
    print("="*80)
    
    # Object details
    print("\n--- Corrupted Object ---")
    print(f"  Address: 0x{obj_data['addr']:x}")
    print(f"  Class ID: {obj_data['class_id']}")
    print(f"  Shape: 0x{obj_data['shape']:x} (CORRUPTED)")
    print(f"  Prop: 0x{obj_data['prop']:x}")
    
    # Object history
    if obj_data['addr'] in _debug_state['object_history']:
        history = _debug_state['object_history'][obj_data['addr']]
        print("\n--- Object History ---")
        for ts, shape, event in history.get('shape_history', []):
            status = "OK" if shape > 0x1000 else "BAD"
            print(f"  [{ts:.3f}] {event}: shape=0x{shape:x} [{status}]")
    
    # Memory context
    print("\n--- Memory Context ---")
    error = lldb.SBError()
    for offset in [-64, -32, -16, 0, 16, 32, 64]:
        addr = obj_data['addr'] + offset
        data = process.ReadMemory(addr, 16, error)
        if error.Success():
            hex_str = ' '.join(f'{b:02x}' for b in data)
            marker = " <-- JSObject" if offset == 0 else ""
            print(f"  0x{addr:x}: {hex_str}{marker}")
    
    # Search for nearby valid objects
    print("\n--- Nearby Valid Objects ---")
    found = 0
    for offset in range(-4096, 4097, 64):
        test_addr = obj_data['addr'] + offset
        if test_addr < 0x1000:
            continue
        test_data = JSObjectTracker.read_object(process, test_addr)
        if test_data and JSObjectTracker.is_valid_object(test_data) and test_data['shape'] > 0x1000:
            print(f"  +{offset:+5d}: 0x{test_addr:x} class={test_data['class_id']} shape=0x{test_data['shape']:x}")
            found += 1
            if found >= 5:
                break
    
    # Register state
    print("\n--- Register State ---")
    for reg in ['x0', 'x1', 'x2', 'x3', 'x4', 'x19', 'x20', 'lr']:
        val = frame.FindRegister(reg)
        print(f"  {reg:3}: {val.GetValue()}")
    
    # Backtrace
    print("\n--- Backtrace ---")
    for i, f in enumerate(thread.frames[:15]):
        func = f.GetFunctionName() or "???"
        pc = f.GetPCAddress().GetLoadAddress(thread.GetProcess().GetTarget())
        print(f"  #{i}: 0x{pc:x} {func}")
    
    # GC state
    print("\n--- GC State ---")
    print(f"  GC runs during session: {len(_debug_state['gc_runs'])}")
    print(f"  init_browser_stubs active: {_debug_state['init_browser_stubs_active']}")
    
    # Statistics
    print("\n--- Session Statistics ---")
    print(f"  Objects created: {len(_debug_state['objects_created'])}")
    print(f"  Objects freed: {len(_debug_state['objects_freed'])}")
    print(f"  Shapes allocated: {len(_debug_state['shapes_allocated'])}")
    print(f"  Shapes freed: {len(_debug_state['shapes_freed'])}")
    
    print("\n" + "="*80)
    print("ANALYSIS COMPLETE - Stopping for manual inspection")
    print("Use: continue - to resume execution")
    print("     qjs-analyze - for current state")
    print("     memory-scan <addr> <size> - to scan memory")
    print("="*80)
    
    return True  # STOP


# ===== COMMAND HANDLERS =====

def track_object(debugger, command, result, internal_dict):
    """Command: track-object <addr>"""
    if not command:
        print("Usage: track-object <address>")
        return
    
    try:
        addr = int(command, 16) if command.startswith('0x') else int(command)
    except:
        print(f"Invalid address: {command}")
        return
    
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    
    obj_data = JSObjectTracker.read_object(process, addr)
    if not obj_data:
        print(f"Could not read object at 0x{addr:x}")
        return
    
    print(f"Tracking object 0x{addr:x}:")
    print(f"  class_id: {obj_data['class_id']}")
    print(f"  shape: 0x{obj_data['shape']:x}")
    
    # Also read shape details
    if obj_data['shape'] > 0x1000:
        shape_data = JSObjectTracker.read_shape(process, obj_data['shape'])
        if shape_data:
            print(f"\n  Shape details:")
            print(f"    prop_hash_mask: {shape_data['prop_hash_mask']}")
            print(f"    prop_count: {shape_data['prop_count']}")
            print(f"    proto: 0x{shape_data['proto']:x}")


def dump_history(debugger, command, result, internal_dict):
    """Command: dump-object-history"""
    print("\n=== Tracked Object History ===")
    print(f"Total tracked objects: {len(_debug_state['object_history'])}")
    
    for addr, history in _debug_state['object_history'].items():
        print(f"\nObject 0x{addr:x} ({history.get('type', 'unknown')}):")
        for ts, shape, event in history.get('shape_history', []):
            status = "OK" if shape > 0x1000 else "BAD"
            print(f"  [{ts:.3f}] {event}: shape=0x{shape:x} [{status}]")


def analyze_corruption(debugger, command, result, internal_dict):
    """Command: analyze-corruption"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    print("\n=== Corruption Analysis ===")
    print(f"Corruption detected: {_debug_state['corruption_detected']}")
    
    # Scan registers for object pointers
    print("\n--- Register Analysis ---")
    for reg_name in ['x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x19', 'x20']:
        reg = frame.FindRegister(reg_name)
        try:
            val = int(reg.GetValue(), 16)
            if val > 0x1000 and val < 0x7f0000000000:
                obj_data = JSObjectTracker.read_object(process, val)
                if obj_data and JSObjectTracker.is_valid_object(obj_data):
                    status = "VALID" if obj_data['shape'] > 0x1000 else "CORRUPTED"
                    print(f"  {reg_name}: 0x{val:x} -> JSObject class={obj_data['class_id']}, shape=0x{obj_data['shape']:x} [{status}]")
        except:
            pass
    
    # List all corrupted objects in history
    print("\n--- Known Corrupted Objects ---")
    corrupted = []
    for addr, history in _debug_state['object_history'].items():
        if history.get('current_shape', 0) < 0x1000:
            corrupted.append(addr)
    
    if corrupted:
        for addr in corrupted:
            print(f"  0x{addr:x}")
    else:
        print("  None tracked")


def memory_scan(debugger, command, result, internal_dict):
    """Command: memory-scan <addr> <size>"""
    args = command.split()
    if len(args) != 2:
        print("Usage: memory-scan <start_addr> <size>")
        return
    
    try:
        start = int(args[0], 16) if args[0].startswith('0x') else int(args[0])
        size = int(args[1], 16) if args[1].startswith('0x') else int(args[1])
    except:
        print("Invalid arguments")
        return
    
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    
    print(f"\nScanning memory 0x{start:x} - 0x{start+size:x} for JSObject patterns...")
    
    error = lldb.SBError()
    found = 0
    for addr in range(start, start + size, 8):
        obj_data = JSObjectTracker.read_object(process, addr)
        if obj_data and JSObjectTracker.is_valid_object(obj_data):
            status = "VALID" if obj_data['shape'] > 0x1000 else "CORRUPTED"
            print(f"  0x{addr:x}: class={obj_data['class_id']}, shape=0x{obj_data['shape']:x} [{status}]")
            found += 1
            if found >= 20:
                print("  (more objects...)")
                break
    
    print(f"\nFound {found} potential JSObject(s)")
