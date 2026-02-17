#!/usr/bin/env python3
"""
LLDB script to catch shape corruption using watchpoints.
This sets a watchpoint on the shape field of newly created objects.
"""

import lldb
import sys

sys.path.insert(0, '/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/Python')

# Track objects we're watching
watched_objects = {}

def get_registers(frame):
    """Get registers from frame."""
    regs = {}
    for name in ['x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x6', 'x7', 'lr']:
        reg = frame.FindRegister(name)
        if reg:
            try:
                regs[name] = int(reg.GetValue(), 0)
            except:
                regs[name] = 0
    return regs

class WatchpointDebugger:
    def __init__(self):
        self.target = None
        self.watchpoints = []
        self.object_count = 0
        
    def start(self, debugger, command, result, internal_dict):
        """Start watchpoint debugging."""
        self.target = debugger.GetSelectedTarget()
        if not self.target:
            result.write("[WatchDebug] No target\n")
            return
            
        # Set breakpoint on JS_NewObjectFromShape
        bp = self.target.BreakpointCreateByName("JS_NewObjectFromShape")
        if bp.GetNumLocations() > 0:
            bp.SetScriptCallbackFunction("lldb_watchpoint_debug.on_object_created")
            result.write(f"[WatchDebug] Set breakpoint on JS_NewObjectFromShape ({bp.GetNumLocations()} locations)\n")
        else:
            result.write("[WatchDebug] WARNING: JS_NewObjectFromShape not found\n")
        
        # Alternative: breakpoint on JS_NewObject
        bp2 = self.target.BreakpointCreateByName("JS_NewObject")
        if bp2.GetNumLocations() > 0:
            result.write(f"[WatchDebug] Found JS_NewObject ({bp2.GetNumLocations()} locations)\n")
            
        # Set breakpoint on init_browser_stubs
        bp3 = self.target.BreakpointCreateByName("init_browser_stubs")
        if bp3.GetNumLocations() > 0:
            result.write(f"[WatchDebug] Found init_browser_stubs ({bp3.GetNumLocations()} locations)\n")
            
        result.write("[WatchDebug] Ready. Objects will be watched automatically.\n")
        result.write("[WatchDebug] Commands:\n")
        result.write("  wp-list      - List watched objects\n")
        result.write("  wp-check <n> - Check object #n\n")
        
    def list_watched(self, debugger, command, result, internal_dict):
        """List watched objects."""
        if not watched_objects:
            result.write("No objects being watched\n")
            return
            
        result.write(f"Watching {len(watched_objects)} objects:\n")
        for obj_id, info in watched_objects.items():
            result.write(f"  #{obj_id}: addr=0x{info['addr']:x} shape=0x{info['shape']:x} '{info['name']}'\n")
            
    def check_object(self, debugger, command, result, internal_dict):
        """Check specific object."""
        if not command:
            result.write("Usage: wp-check <object_id>\n")
            return
            
        try:
            obj_id = int(command)
        except ValueError:
            result.write(f"Invalid object ID: {command}\n")
            return
            
        if obj_id not in watched_objects:
            result.write(f"Object #{obj_id} not found\n")
            return
            
        info = watched_objects[obj_id]
        addr = info['addr']
        
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        error = lldb.SBError()
        
        # Read current shape
        shape_data = process.ReadMemory(addr + 8, 8, error)
        if error.Success() and shape_data:
            import struct
            shape = struct.unpack('<Q', shape_data)[0]
            result.write(f"Object #{obj_id} at 0x{addr:x}:\n")
            result.write(f"  Original shape: 0x{info['shape']:x}\n")
            result.write(f"  Current shape:  0x{shape:x}\n")
            if shape != info['shape']:
                result.write("  *** SHAPE CHANGED! ***\n")
        else:
            result.write(f"Cannot read object #{obj_id}\n")

# Global instance
g_debugger = WatchpointDebugger()

def on_object_created(frame, bp_loc, dict):
    """Called when JS_NewObjectFromShape returns."""
    global g_debugger, watched_objects
    
    debugger = lldb.debugger
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    # Get return value (x0 on ARM64)
    ret_val = int(frame.FindRegister('x0').GetValue(), 0)
    
    # Only watch if it looks like a valid object pointer
    if ret_val < 0x1000 or ret_val > 0x0000FFFFFFFFFFFF:
        return False
        
    # Get the JSObject pointer from JSValue
    # For pointer-based JSValue, it's the pointer itself
    obj_ptr = ret_val
    
    # Read shape pointer (offset 8)
    error = lldb.SBError()
    shape_data = process.ReadMemory(obj_ptr + 8, 8, error)
    if not error.Success() or not shape_data:
        return False
        
    import struct
    shape_ptr = struct.unpack('<Q', shape_data)[0]
    
    # Skip if shape is already invalid
    if shape_ptr == 0 or shape_ptr == 0xFFFFFFFFFFFFFFFF or shape_ptr < 0x1000:
        print(f"[WatchDebug] Object 0x{obj_ptr:x} created with INVALID shape 0x{shape_ptr:x}")
        return False
        
    # Add to watched objects
    g_debugger.object_count += 1
    obj_id = g_debugger.object_count
    
    watched_objects[obj_id] = {
        'addr': obj_ptr,
        'shape': shape_ptr,
        'name': f"obj_{obj_id}",
        'watchpoint': None
    }
    
    print(f"[WatchDebug] #{obj_id}: Watching object 0x{obj_ptr:x} with shape 0x{shape_ptr:x}")
    
    # Try to set a watchpoint on the shape field
    # Note: This may fail if too many watchpoints are set
    try:
        # Calculate address of shape field
        shape_field_addr = obj_ptr + 8
        
        # We can't set unlimited watchpoints, so only watch first 10 objects
        if obj_id <= 10:
            wp = target.WatchAddress(shape_field_addr, 8, False, True)
            if wp and wp.IsValid():
                watched_objects[obj_id]['watchpoint'] = wp
                print(f"[WatchDebug] #{obj_id}: Set watchpoint on shape field at 0x{shape_field_addr:x}")
    except Exception as e:
        print(f"[WatchDebug] #{obj_id}: Could not set watchpoint: {e}")
    
    return False

def __lldb_init_module(debugger, internal_dict):
    """Initialize module."""
    global g_debugger
    g_debugger = WatchpointDebugger()
    
    debugger.HandleCommand('command script add -f lldb_watchpoint_debug.WatchpointDebugger.start wp-start')
    debugger.HandleCommand('command script add -f lldb_watchpoint_debug.WatchpointDebugger.list_watched wp-list')
    debugger.HandleCommand('command script add -f lldb_watchpoint_debug.WatchpointDebugger.check_object wp-check')
    
    print("[WatchDebug] Module loaded.")
    print("[WatchDebug] Run 'wp-start' to begin watching objects.")
