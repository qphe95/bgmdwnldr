#!/usr/bin/env python3
"""
Advanced LLDB Python script to investigate QuickJS shape allocation and corruption.
Usage: lldb -o 'command script import lldb_shape_investigator.py' -o 'shape-investigate-start'
"""

import lldb
import sys

# Enable Python to find the lldb module
sys.path.insert(0, '/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/Python')

# Global storage for tracking
shape_allocations = {}  # addr -> {size, proto, callstack}
object_allocations = {}  # addr -> {shape_addr, class_id, callstack}

class ShapeInvestigator:
    def __init__(self, debugger, session_dict):
        self.debugger = debugger
        self.session_dict = session_dict
        self.target = None
        self.process = None
        self.breakpoints = {}
        
    def start(self, args, result):
        """Start shape investigation."""
        result.write("[ShapeInvestigator] Starting investigation...\n")
        
        self.target = self.debugger.GetSelectedTarget()
        if not self.target:
            result.write("[ShapeInvestigator] ERROR: No target selected\n")
            return
        
        # Set breakpoints on shape allocation functions
        self._set_breakpoint('js_new_shape_nohash', 'shape_alloc')
        self._set_breakpoint('js_new_shape', 'shape_alloc2')
        self._set_breakpoint('js_free_shape0', 'shape_free')
        self._set_breakpoint('JS_NewObjectFromShape', 'object_create')
        self._set_breakpoint('JS_NewObject', 'object_create_simple')
        self._set_breakpoint('JS_SetPropertyInternal', 'set_property')
        self._set_breakpoint('init_browser_stubs', 'init_stubs')
        
        # Set up SIGSEGV handler
        self.target.GetProcess().GetUnixSignals().SetShouldStop(11, True)
        self.target.GetProcess().GetUnixSignals().SetShouldNotify(11, True)
        
        result.write("[ShapeInvestigator] Breakpoints set. Use 'continue' to run.\n")
        result.write("[ShapeInvestigator] Commands available:\n")
        result.write("  shape-status - Show allocation statistics\n")
        result.write("  shape-check <addr> - Check object/shape at address\n")
        
    def _set_breakpoint(self, name, handler_name):
        """Set a breakpoint with a handler."""
        bp = self.target.BreakpointCreateByName(name)
        if bp.GetNumLocations() > 0:
            bp.SetScriptCallbackFunction(f'lldb_shape_investigator.{handler_name}_callback')
            self.breakpoints[name] = bp
            
    def status(self, args, result):
        """Show current status."""
        result.write(f"[ShapeInvestigator] Tracked shapes: {len(shape_allocations)}\n")
        result.write(f"[ShapeInvestigator] Tracked objects: {len(object_allocations)}\n")
        
        # Show recent allocations
        if shape_allocations:
            result.write("\nRecent shape allocations:\n")
            for addr, info in list(shape_allocations.items())[-5:]:
                result.write(f"  0x{addr:x}: proto=0x{info.get('proto', 0):x} size={info.get('size', 0)}\n")
                
        if object_allocations:
            result.write("\nRecent object allocations:\n")
            for addr, info in list(object_allocations.items())[-5:]:
                shape = info.get('shape_addr', 0)
                result.write(f"  0x{addr:x}: shape=0x{shape:x} class_id={info.get('class_id', '?')}\n")
                
    def check_object(self, args, result):
        """Check an object at given address."""
        if not args:
            result.write("Usage: shape-check <address>\n")
            return
            
        try:
            addr = int(args, 0)
        except ValueError:
            result.write(f"Invalid address: {args}\n")
            return
            
        self._analyze_object(addr, result)
        
    def _analyze_object(self, addr, result):
        """Analyze a JSObject at given address."""
        error = lldb.SBError()
        process = self.target.GetProcess()
        
        result.write(f"\n[Object Analysis] Address: 0x{addr:x}\n")
        result.write("="*50 + "\n")
        
        # Read JSObject header
        # Offset 0: class_id (uint16_t)
        # Offset 2: flags (uint8_t)
        # Offset 4: weakref_count (uint32_t)
        # Offset 8: shape (JSShape*)
        # Offset 16: prop (JSProperty*)
        
        header_data = process.ReadMemory(addr, 24, error)
        if not error.Success() or not header_data:
            result.write(f"ERROR: Cannot read memory at 0x{addr:x}: {error.GetCString()}\n")
            return
            
        import struct
        header = struct.unpack('<HBBIIQQ', header_data)
        class_id = header[0]
        flags = header[1]
        padding = header[2]  # _padding
        weakref_count = header[3]
        shape_ptr = header[4]
        prop_ptr = header[5]
        
        result.write(f"JSObject Header:\n")
        result.write(f"  class_id: {class_id}\n")
        result.write(f"  flags: 0x{flags:x}\n")
        result.write(f"  weakref_count: {weakref_count}\n")
        result.write(f"  shape: 0x{shape_ptr:x}\n")
        result.write(f"  prop: 0x{prop_ptr:x}\n")
        
        # Analyze shape pointer
        result.write(f"\nShape Analysis:\n")
        if shape_ptr == 0:
            result.write("  Shape is NULL (freed or uninitialized)\n")
        elif shape_ptr == 0xFFFFFFFFFFFFFFFF:
            result.write("  Shape is -1 (CORRUPTED - looks like tagged JS_TAG_OBJECT)\n")
        elif shape_ptr < 0x1000:
            result.write(f"  Shape looks like tagged value: {shape_ptr}\n")
            # Decode QuickJS tag
            tag = shape_ptr & 0xF
            tags = {1: 'JS_TAG_INT/UNDEFINED', 3: 'JS_TAG_EXCEPTION', 4: 'JS_TAG_UNDEFINED',
                   5: 'JS_TAG_NULL', 6: 'JS_TAG_BOOL', 7: 'JS_TAG_EXCEPTION2'}
            result.write(f"  -> Tag: 0x{tag:x} = {tags.get(tag, 'UNKNOWN')}\n")
        else:
            result.write("  Shape pointer looks valid\n")
            # Try to read shape header
            self._analyze_shape(shape_ptr, result)
            
        # Check if this object is in our tracking
        if addr in object_allocations:
            info = object_allocations[addr]
            result.write(f"\nTracked Allocation Info:\n")
            result.write(f"  Original shape: 0x{info.get('shape_addr', 0):x}\n")
            result.write(f"  Class ID: {info.get('class_id', '?')}\n")
            if info.get('shape_addr', 0) != shape_ptr:
                result.write("  WARNING: Shape pointer changed since allocation!\n")
                
    def _analyze_shape(self, addr, result):
        """Analyze a JSShape at given address."""
        error = lldb.SBError()
        process = self.target.GetProcess()
        
        # JSShape structure:
        # uint8_t is_hashed
        # uint32_t hash
        # uint32_t prop_hash_mask
        # int prop_size
        # int prop_count
        # int deleted_prop_count
        # JSShape *shape_hash_next
        # JSObject *proto
        # JSShapeProperty prop[0]
        
        shape_data = process.ReadMemory(addr, 40, error)
        if not error.Success() or not shape_data:
            result.write(f"  ERROR: Cannot read shape at 0x{addr:x}\n")
            return
            
        import struct
        # Read shape fields - need to account for padding
        # Structure layout with padding on 64-bit:
        # offset 0: is_hashed (1 byte) + 3 bytes padding
        # offset 4: hash (4 bytes)
        # offset 8: prop_hash_mask (4 bytes) + 4 bytes padding
        # offset 16: prop_size (4 bytes)
        # offset 20: prop_count (4 bytes)
        # offset 24: deleted_prop_count (4 bytes) + 4 bytes padding
        # offset 32: shape_hash_next (8 bytes)
        # offset 40: proto (8 bytes)
        
        data = struct.unpack('<BxxxIIIIxxxxQQ', shape_data)
        is_hashed = data[0]
        hash_val = data[1]
        prop_hash_mask = data[2]
        prop_size = data[3]
        prop_count = data[4]
        shape_hash_next = data[5]
        proto = data[6]
        
        result.write(f"  JSShape at 0x{addr:x}:\n")
        result.write(f"    is_hashed: {is_hashed}\n")
        result.write(f"    hash: 0x{hash_val:x}\n")
        result.write(f"    prop_hash_mask: {prop_hash_mask}\n")
        result.write(f"    prop_size: {prop_size}\n")
        result.write(f"    prop_count: {prop_count}\n")
        result.write(f"    shape_hash_next: 0x{shape_hash_next:x}\n")
        result.write(f"    proto: 0x{proto:x}\n")

# Callback functions for breakpoints
def shape_alloc_callback(frame, bp_loc, dict):
    """Called when js_new_shape_nohash is entered."""
    debugger = lldb.debugger
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    # Get arguments from registers (ARM64 calling convention)
    # x0 = ctx, x1 = proto, x2 = hash_size, x3 = prop_size
    ctx = int(frame.FindRegister('x0').GetValue(), 0)
    proto = int(frame.FindRegister('x1').GetValue(), 0)
    hash_size = int(frame.FindRegister('x2').GetValue(), 0)
    prop_size = int(frame.FindRegister('x3').GetValue(), 0)
    
    # Store in thread-local data to match with return
    thread.SetSelectedFrame(frame)
    
    # Set a completion breakpoint at the return
    result = lldb.SBCommandReturnObject()
    
    return False  # Don't stop

def shape_free_callback(frame, bp_loc, dict):
    """Called when js_free_shape0 is entered."""
    debugger = lldb.debugger
    target = debugger.GetSelectedTarget()
    frame = debugger.GetSelectedTarget().GetProcess().GetSelectedThread().GetSelectedFrame()
    
    # x0 = rt, x1 = sh
    sh = int(frame.FindRegister('x1').GetValue(), 0)
    
    if sh in shape_allocations:
        del shape_allocations[sh]
        
    return False

def object_create_callback(frame, bp_loc, dict):
    """Called when JS_NewObjectFromShape is entered."""
    debugger = lldb.debugger
    frame = debugger.GetSelectedTarget().GetProcess().GetSelectedThread().GetSelectedFrame()
    
    # x0 = ctx, x1 = sh, x2 = class_id
    ctx = int(frame.FindRegister('x0').GetValue(), 0)
    sh = int(frame.FindRegister('x1').GetValue(), 0)
    class_id = int(frame.FindRegister('x2').GetValue(), 0)
    
    # We'll capture the return value in a separate callback
    return False

def set_property_callback(frame, bp_loc, dict):
    """Called when JS_SetPropertyInternal is entered."""
    debugger = lldb.debugger
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    # x0 = ctx, x1 = this_obj
    this_obj = int(frame.FindRegister('x1').GetValue(), 0)
    
    # Check if this_obj has a valid shape
    error = lldb.SBError()
    shape_data = process.ReadMemory(this_obj + 8, 8, error)
    
    if error.Success() and shape_data:
        import struct
        shape_ptr = struct.unpack('<Q', shape_data)[0]
        
        # Check for corruption
        if shape_ptr == 0xFFFFFFFFFFFFFFFF or shape_ptr < 0x1000:
            print(f"\n*** SHAPE CORRUPTION DETECTED ***")
            print(f"Object at 0x{this_obj:x} has invalid shape: 0x{shape_ptr:x}")
            print(f"Stopping for investigation...")
            
            # Print backtrace
            print("\nBacktrace:")
            for i, f in enumerate(thread.frames[:10]):
                print(f"  #{i}: {f.GetFunctionName()}")
            
            return True  # Stop execution
            
    return False

def init_stubs_callback(frame, bp_loc, dict):
    """Called when init_browser_stubs is entered."""
    print("\n[ShapeInvestigator] Entered init_browser_stubs")
    return False

def __lldb_init_module(debugger, internal_dict):
    """Initialize the LLDB module."""
    investigator = ShapeInvestigator(debugger, internal_dict)
    
    # Register commands
    debugger.HandleCommand('command script add -f lldb_shape_investigator.ShapeInvestigator.start shape-investigate-start')
    debugger.HandleCommand('command script add -f lldb_shape_investigator.ShapeInvestigator.status shape-status')
    debugger.HandleCommand('command script add -f lldb_shape_investigator.ShapeInvestigator.check_object shape-check')
    
    print("[ShapeInvestigator] Module loaded.")
    print("[ShapeInvestigator] Run 'shape-investigate-start' to begin.")

# Factory functions for LLDB
def shape_alloc_callback_wrapper(debugger, command, result, internal_dict):
    return shape_alloc_callback(None, None, internal_dict)

def shape_free_callback_wrapper(debugger, command, result, internal_dict):
    return shape_free_callback(None, None, internal_dict)

def object_create_callback_wrapper(debugger, command, result, internal_dict):
    return object_create_callback(None, None, internal_dict)

def set_property_callback_wrapper(debugger, command, result, internal_dict):
    return set_property_callback(None, None, internal_dict)

def init_stubs_callback_wrapper(debugger, command, result, internal_dict):
    return init_stubs_callback(None, None, internal_dict)
