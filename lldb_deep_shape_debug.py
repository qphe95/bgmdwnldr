#!/usr/bin/env python3
"""
Deep investigation script for QuickJS shape corruption.
Traces object creation, shape allocation, and property setting.
"""

import lldb
import sys

sys.path.insert(0, '/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/Python')

# Track allocations
g_allocations = {
    'shapes': {},
    'objects': {},
    'freed': set()
}

def get_registers(frame):
    """Get all registers from frame."""
    regs = {}
    for name in ['x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x6', 'x7', 'lr', 'sp', 'pc']:
        reg = frame.FindRegister(name)
        if reg:
            try:
                regs[name] = int(reg.GetValue(), 0)
            except:
                regs[name] = 0
    return regs

def read_memory_uint64(process, addr):
    """Read 8 bytes as uint64."""
    error = lldb.SBError()
    data = process.ReadMemory(addr, 8, error)
    if error.Success() and data:
        import struct
        return struct.unpack('<Q', data)[0]
    return None

def read_memory_uint32(process, addr):
    """Read 4 bytes as uint32."""
    error = lldb.SBError()
    data = process.ReadMemory(addr, 4, error)
    if error.Success() and data:
        import struct
        return struct.unpack('<I', data)[0]
    return None

def read_memory_uint16(process, addr):
    """Read 2 bytes as uint16."""
    error = lldb.SBError()
    data = process.ReadMemory(addr, 2, error)
    if error.Success() and data:
        import struct
        return struct.unpack('<H', data)[0]
    return None

def get_obj_shape_ptr(process, obj_addr):
    """Get shape pointer from JSObject."""
    # JSObject: offset 8 = shape pointer
    return read_memory_uint64(process, obj_addr + 8)

def get_obj_class_id(process, obj_addr):
    """Get class_id from JSObject."""
    # JSObject: offset 0 = class_id (uint16)
    return read_memory_uint16(process, obj_addr)

def analyze_jsvalue(process, val_addr):
    """Analyze a JSValue structure."""
    # JSValue structure:
    # offset 0: u (union, 8 bytes)
    # offset 8: tag (int64)
    u = read_memory_uint64(process, val_addr)
    tag = read_memory_uint64(process, val_addr + 8)
    return {'u': u, 'tag': tag}

def is_valid_pointer(ptr):
    """Check if pointer looks valid."""
    if ptr is None:
        return False
    # Must be aligned to 8 bytes
    if ptr & 0x7:
        return False
    # Must be in reasonable range (not kernel space on 64-bit)
    if ptr > 0x0000FFFFFFFFFFFF:
        return False
    # Must not be small value
    if ptr < 0x1000:
        return False
    return True

class DeepShapeDebugger:
    def __init__(self, debugger, session_dict):
        self.debugger = debugger
        self.session_dict = session_dict
        self.target = None
        self.bps = []
        
    def start(self, args, result):
        """Start deep debugging."""
        result.write("[DeepShapeDebug] Starting deep investigation...\n")
        
        self.target = self.debugger.GetSelectedTarget()
        if not self.target:
            result.write("[DeepShapeDebug] ERROR: No target\n")
            return
            
        # Set breakpoints
        self._setup_breakpoints(result)
        
        # Set signal handler
        self.target.GetProcess().GetUnixSignals().SetShouldStop(11, True)
        
        result.write("[DeepShapeDebug] Ready. Continue with 'continue'\n")
        
    def _setup_breakpoints(self, result):
        """Set up all breakpoints."""
        bps = [
            ('js_new_shape_nohash', self._on_shape_alloc),
            ('js_free_shape0', self._on_shape_free),
            ('JS_NewObjectFromShape', self._on_object_create),
            ('JS_NewObject', self._on_object_create_simple),
            ('JS_SetPropertyInternal', self._on_set_property),
            ('find_own_property', self._on_find_own_prop),
            ('init_browser_stubs', self._on_init_stubs),
        ]
        
        for name, handler in bps:
            bp = self.target.BreakpointCreateByName(name)
            if bp.GetNumLocations() > 0:
                bp.SetScriptCallbackFunction(f'lldb_deep_shape_debug.DeepShapeDebugger._callback_wrapper')
                # Store handler in breakpoint's associated data
                bp.SetThreadName(f'handler:{name}')
                self.bps.append((bp, handler))
                result.write(f"[DeepShapeDebug] BP: {name}\n")
    
    @staticmethod
    def _callback_wrapper(frame, bp_loc, dict):
        """Static wrapper for callbacks."""
        debugger = lldb.debugger
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        thread = process.GetSelectedThread()
        frame = thread.GetSelectedFrame()
        bp = bp_loc.GetBreakpoint()
        
        # Get handler name from breakpoint's thread name
        handler_name = bp.GetThreadName()
        
        # Find the instance
        # (We need to store instance globally or use another method)
        return False
    
    def _on_shape_alloc(self, frame, result):
        """Called when js_new_shape_nohash is called."""
        regs = get_registers(frame)
        ctx = regs.get('x0', 0)
        proto = regs.get('x1', 0)
        hash_size = regs.get('x2', 0)
        prop_size = regs.get('x3', 0)
        
        print(f"[SHAPE ALLOC] ctx=0x{ctx:x} proto=0x{proto:x} hash={hash_size} props={prop_size}")
        
    def _on_shape_free(self, frame, result):
        """Called when js_free_shape0 is called."""
        regs = get_registers(frame)
        sh = regs.get('x1', 0)
        
        print(f"[SHAPE FREE] sh=0x{sh:x}")
        
        if sh in g_allocations['shapes']:
            g_allocations['freed'].add(sh)
            del g_allocations['shapes'][sh]
    
    def _on_object_create(self, frame, result):
        """Called when JS_NewObjectFromShape is called."""
        regs = get_registers(frame)
        ctx = regs.get('x0', 0)
        sh = regs.get('x1', 0)
        class_id = regs.get('x2', 0)
        
        print(f"[OBJ CREATE] ctx=0x{ctx:x} shape=0x{sh:x} class_id={class_id}")
        
    def _on_object_create_simple(self, frame, result):
        """Called when JS_NewObject is called."""
        regs = get_registers(frame)
        ctx = regs.get('x0', 0)
        
        print(f"[OBJ CREATE SIMPLE] ctx=0x{ctx:x}")
    
    def _on_set_property(self, frame, result):
        """Called when JS_SetPropertyInternal is called."""
        process = self.debugger.GetSelectedTarget().GetProcess()
        regs = get_registers(frame)
        ctx = regs.get('x0', 0)
        this_obj = regs.get('x1', 0)
        
        # Read object header
        shape_ptr = get_obj_shape_ptr(process, this_obj)
        class_id = get_obj_class_id(process, this_obj)
        
        print(f"[SET PROP] obj=0x{this_obj:x} class_id={class_id} shape=0x{shape_ptr:x}")
        
        # Check for corruption
        if shape_ptr == 0xFFFFFFFFFFFFFFFF:
            print(f"*** CORRUPTION: shape = -1 ***")
            print(f"*** Stopping execution ***")
            return True
        elif shape_ptr == 0:
            print(f"*** WARNING: shape is NULL ***")
        elif not is_valid_pointer(shape_ptr):
            print(f"*** WARNING: shape looks invalid: 0x{shape_ptr:x} ***")
            
        return False
    
    def _on_find_own_prop(self, frame, result):
        """Called when find_own_property is called."""
        process = self.debugger.GetSelectedTarget().GetProcess()
        regs = get_registers(frame)
        # p = x1
        p = regs.get('x1', 0)
        
        if p:
            shape_ptr = get_obj_shape_ptr(process, p)
            print(f"[FIND OWN PROP] p=0x{p:x} shape=0x{shape_ptr:x}")
    
    def _on_init_stubs(self, frame, result):
        """Called when init_browser_stubs is entered."""
        print("[INIT] init_browser_stubs entered")
        
    def check_object(self, args, result):
        """Check object at address."""
        if not args:
            result.write("Usage: check-obj <addr>\n")
            return
            
        try:
            addr = int(args, 0)
        except ValueError:
            result.write(f"Invalid address: {args}\n")
            return
            
        process = self.target.GetProcess()
        
        result.write(f"\nObject at 0x{addr:x}:\n")
        result.write("-" * 40 + "\n")
        
        # Read header
        class_id = get_obj_class_id(process, addr)
        shape_ptr = get_obj_shape_ptr(process, addr)
        prop_ptr = read_memory_uint64(process, addr + 16)
        
        result.write(f"  class_id: {class_id}\n")
        result.write(f"  shape: 0x{shape_ptr:x}\n")
        result.write(f"  prop: 0x{prop_ptr:x}\n")
        
        # Validate shape
        if shape_ptr == 0:
            result.write("  Shape: NULL (freed or uninitialized)\n")
        elif shape_ptr == 0xFFFFFFFFFFFFFFFF:
            result.write("  Shape: CORRUPTED (-1)\n")
        elif not is_valid_pointer(shape_ptr):
            result.write(f"  Shape: INVALID (0x{shape_ptr:x})\n")
        else:
            result.write("  Shape: looks valid\n")
            # Try to read shape
            self._check_shape(shape_ptr, result)
            
    def _check_shape(self, addr, result):
        """Check shape at address."""
        process = self.target.GetProcess()
        
        # Read shape header
        # JSShape: offset 0 = is_hashed, offset 4 = hash, offset 8 = prop_hash_mask
        is_hashed = read_memory_uint64(process, addr) & 0xFF
        hash_val = read_memory_uint32(process, addr + 4)
        prop_hash_mask = read_memory_uint32(process, addr + 8)
        
        result.write(f"\n  JSShape at 0x{addr:x}:\n")
        result.write(f"    is_hashed: {is_hashed}\n")
        result.write(f"    hash: 0x{hash_val:x}\n")
        result.write(f"    prop_hash_mask: {prop_hash_mask}\n")

def __lldb_init_module(debugger, internal_dict):
    """Initialize module."""
    debugger.HandleCommand('command script add -f lldb_deep_shape_debug.DeepShapeDebugger.start deep-debug-start')
    debugger.HandleCommand('command script add -f lldb_deep_shape_debug.DeepShapeDebugger.check_object deep-check-obj')
    print("[DeepShapeDebug] Module loaded.")
    print("[DeepShapeDebug] Run 'deep-debug-start' to begin.")
