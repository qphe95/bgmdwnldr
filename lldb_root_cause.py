#!/usr/bin/env python3
"""
Root cause analysis for QuickJS shape corruption using LLDB watchpoints.
This script tracks object creation and sets watchpoints to catch corruption.
"""

import lldb
import sys
import time

sys.path.insert(0, '/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/Python')

# Global tracking
class TrackedObject:
    def __init__(self, obj_addr, shape_addr, alloc_pc, name):
        self.obj_addr = obj_addr
        self.shape_addr = shape_addr
        self.alloc_pc = alloc_pc
        self.name = name
        self.watchpoint = None
        self.corrupted = False
        self.history = []
        
tracked_objects = {}  # obj_addr -> TrackedObject
next_obj_id = 0
stop_on_corruption = True

def get_registers(frame):
    """Get ARM64 registers."""
    regs = {}
    for name in ['x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x6', 'x7', 'x8', 
                 'x9', 'x10', 'x11', 'x12', 'x13', 'x14', 'x15',
                 'x16', 'x17', 'x18', 'x19', 'x20', 'x21', 'x22', 'x23',
                 'x24', 'x25', 'x26', 'x27', 'x28', 'fp', 'lr', 'sp', 'pc']:
        reg = frame.FindRegister(name)
        if reg and reg.GetValue():
            try:
                regs[name] = int(reg.GetValue(), 0)
            except:
                regs[name] = 0
    return regs

def read_u64(process, addr):
    """Read 8 bytes."""
    error = lldb.SBError()
    data = process.ReadMemory(addr, 8, error)
    if error.Success() and data and len(data) == 8:
        import struct
        return struct.unpack('<Q', data)[0]
    return None

def get_obj_info(process, obj_addr):
    """Get JSObject info."""
    class_id = read_u64(process, obj_addr) & 0xFFFF
    shape_ptr = read_u64(process, obj_addr + 8)
    prop_ptr = read_u64(process, obj_addr + 16)
    return {
        'class_id': class_id,
        'shape': shape_ptr,
        'prop': prop_ptr
    }

def is_valid_obj_addr(addr):
    """Check if address looks like a valid heap object."""
    if addr is None or addr == 0:
        return False
    if addr < 0x1000:
        return False
    if addr > 0x0000FFFFFFFFFFFF:
        return False
    if addr & 0x7:  # Must be 8-byte aligned
        return False
    return True

def get_backtrace(thread, max_frames=10):
    """Get backtrace as string list."""
    frames = []
    for i, f in enumerate(thread.frames[:max_frames]):
        name = f.GetFunctionName() or f.GetSymbol().GetName() or "??"
        pc = f.GetPC()
        frames.append(f"#{i}: {name} @ 0x{pc:x}")
    return frames

class RootCauseDebugger:
    def __init__(self, debugger, session_dict):
        self.debugger = debugger
        self.session_dict = session_dict
        self.target = None
        self.process = None
        self.bp_newobj = None
        self.bp_init = None
        self.bp_crash = None
        
    def start(self, args, result):
        """Start root cause debugging."""
        global stop_on_corruption
        
        self.target = self.debugger.GetSelectedTarget()
        if not self.target:
            result.write("[RootCause] ERROR: No target\n")
            return
            
        self.process = self.target.GetProcess()
        
        result.write("[RootCause] Starting root cause analysis...\n")
        result.write("[RootCause] This will track objects and catch corruption\n")
        
        # Set breakpoints
        self._setup_breakpoints(result)
        
        # Set signal handler for SIGSEGV
        self.process.GetUnixSignals().SetShouldStop(11, True)
        self.process.GetUnixSignals().SetShouldNotify(11, True)
        
        result.write("\n[RootCause] Ready! Run 'continue' to start.\n")
        result.write("[RootCause] Commands:\n")
        result.write("  rc-status    - Show tracked objects\n")
        result.write("  rc-history   - Show corruption history\n")
        result.write("  rc-check <addr> - Check specific object\n")
        result.write("  rc-stop-on-corruption [on/off] - Toggle stop behavior\n")
        
    def _setup_breakpoints(self, result):
        """Set up key breakpoints."""
        global next_obj_id
        
        # Breakpoint 1: JS_NewObjectFromShape - track object creation
        bp1 = self.target.BreakpointCreateByName("JS_NewObjectFromShape")
        if bp1.GetNumLocations() > 0:
            bp1.SetScriptCallbackFunction("lldb_root_cause.on_new_object")
            result.write(f"[RootCause] BP on JS_NewObjectFromShape ({bp1.GetNumLocations()} locs)\n")
        else:
            result.write("[RootCause] WARNING: JS_NewObjectFromShape not found\n")
            
        # Breakpoint 2: init_browser_stubs - to know when we're in init
        bp2 = self.target.BreakpointCreateByName("init_browser_stubs")
        if bp2.GetNumLocations() > 0:
            bp2.SetScriptCallbackFunction("lldb_root_cause.on_init_stubs")
            result.write(f"[RootCause] BP on init_browser_stubs ({bp2.GetNumLocations()} locs)\n")
            
        # Breakpoint 3: js_new_shape_nohash - track shape creation
        bp3 = self.target.BreakpointCreateByName("js_new_shape_nohash")
        if bp3.GetNumLocations() > 0:
            bp3.SetScriptCallbackFunction("lldb_root_cause.on_new_shape")
            result.write(f"[RootCause] BP on js_new_shape_nohash ({bp3.GetNumLocations()} locs)\n")
            
        # Breakpoint 4: memmove, memcpy, memset - potential corruption sources
        for func in ['memmove', 'memcpy', 'memset']:
            bp = self.target.BreakpointCreateByName(func)
            if bp.GetNumLocations() > 0:
                bp.SetScriptCallbackFunction("lldb_root_cause.on_mem_op")
                result.write(f"[RootCause] BP on {func} ({bp.GetNumLocations()} locs)\n")
        
    def status(self, args, result):
        """Show tracked objects."""
        global tracked_objects
        
        result.write(f"\nTracked Objects: {len(tracked_objects)}\n")
        result.write("="*60 + "\n")
        
        for addr, obj in sorted(tracked_objects.items()):
            status = "CORRUPTED" if obj.corrupted else "OK"
            wp_status = "WP" if obj.watchpoint and obj.watchpoint.IsValid() else "no-WP"
            result.write(f"\nObject 0x{addr:x} ({obj.name}): {status} [{wp_status}]\n")
            result.write(f"  Shape: 0x{obj.shape_addr:x}\n")
            result.write(f"  Allocated at: 0x{obj.alloc_pc:x}\n")
            if obj.history:
                result.write(f"  History ({len(obj.history)} events):\n")
                for event in obj.history[-3:]:
                    result.write(f"    {event}\n")
                    
    def history(self, args, result):
        """Show full corruption history."""
        global tracked_objects
        
        corrupted = [(addr, obj) for addr, obj in tracked_objects.items() if obj.corrupted]
        
        result.write(f"\nCorrupted Objects: {len(corrupted)}\n")
        result.write("="*60 + "\n")
        
        for addr, obj in corrupted:
            result.write(f"\n### Object 0x{addr:x} ({obj.name}) ###\n")
            result.write(f"Original shape: 0x{obj.shape_addr:x}\n")
            result.write(f"Allocation backtrace:\n")
            for line in obj.alloc_pc:
                result.write(f"  {line}\n")
            result.write(f"\nFull history:\n")
            for event in obj.history:
                result.write(f"  {event}\n")
                
    def check_object(self, args, result):
        """Check specific object address."""
        if not args:
            result.write("Usage: rc-check <address>\n")
            return
            
        try:
            addr = int(args, 0)
        except ValueError:
            result.write(f"Invalid address: {args}\n")
            return
            
        if addr in tracked_objects:
            obj = tracked_objects[addr]
            result.write(f"Object 0x{addr:x} ({obj.name}):\n")
            result.write(f"  Original shape: 0x{obj.shape_addr:x}\n")
            
            # Read current state
            info = get_obj_info(self.process, addr)
            result.write(f"  Current shape: 0x{info['shape']:x}\n")
            result.write(f"  Class ID: {info['class_id']}\n")
            
            if info['shape'] != obj.shape_addr:
                result.write("  *** SHAPE CHANGED ***\n")
        else:
            result.write(f"Object 0x{addr:x} not tracked\n")
            # Try to read anyway
            info = get_obj_info(self.process, addr)
            result.write(f"  Current shape: 0x{info['shape']:x}\n")
            result.write(f"  Class ID: {info['class_id']}\n")
            
    def stop_toggle(self, args, result):
        """Toggle stop-on-corruption behavior."""
        global stop_on_corruption
        if args.strip().lower() == 'off':
            stop_on_corruption = False
            result.write("[RootCause] Will NOT stop on corruption (just log)\n")
        else:
            stop_on_corruption = True
            result.write("[RootCause] Will stop on corruption\n")

# Global debugger instance
g_debugger = None

def on_new_object(frame, bp_loc, dict):
    """Called when JS_NewObjectFromShape returns."""
    global tracked_objects, next_obj_id, g_debugger
    
    debugger = lldb.debugger
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    # Get return value (x0 on ARM64)
    try:
        ret_val = int(frame.FindRegister('x0').GetValue(), 0)
    except:
        return False
        
    # Validate it's a heap pointer
    if not is_valid_obj_addr(ret_val):
        return False
        
    # Read current shape
    shape_ptr = read_u64(process, ret_val + 8)
    
    # Skip if already corrupted
    if shape_ptr == 0 or shape_ptr == 0xFFFFFFFFFFFFFFFF or shape_ptr < 0x1000:
        print(f"[RC] WARNING: New object 0x{ret_val:x} already has bad shape 0x{shape_ptr:x}")
        return False
        
    # Create tracked object
    next_obj_id += 1
    obj_name = f"obj_{next_obj_id}"
    
    obj = TrackedObject(
        obj_addr=ret_val,
        shape_addr=shape_ptr,
        alloc_pc=get_backtrace(thread, 5),
        name=obj_name
    )
    
    tracked_objects[ret_val] = obj
    
    # Set watchpoint on shape field (offset 8)
    # Only set watchpoints for first 20 objects to avoid limit
    if next_obj_id <= 20:
        try:
            shape_field_addr = ret_val + 8
            wp = target.WatchAddress(shape_field_addr, 8, False, True)
            if wp and wp.IsValid():
                obj.watchpoint = wp
                wp.SetScriptCallbackFunction("lldb_root_cause.on_shape_write")
                print(f"[RC] #{next_obj_id}: Watching 0x{ret_val:x} (shape=0x{shape_ptr:x}) [WP set]")
            else:
                print(f"[RC] #{next_obj_id}: Watching 0x{ret_val:x} (shape=0x{shape_ptr:x}) [NO WP]")
        except Exception as e:
            print(f"[RC] #{next_obj_id}: Watching 0x{ret_val:x} (shape=0x{shape_ptr:x}) [WP ERROR: {e}]")
    else:
        print(f"[RC] #{next_obj_id}: Watching 0x{ret_val:x} (shape=0x{shape_ptr:x}) [no WP - limit]")
    
    return False

def on_shape_write(frame, wp_loc, dict):
    """Called when a watched shape field is written."""
    global tracked_objects, stop_on_corruption
    
    debugger = lldb.debugger
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    # Get the watchpoint address
    wp = wp_loc.GetWatchpoint()
    wp_addr = wp.GetWatchAddress() if wp else 0
    obj_addr = wp_addr - 8 if wp_addr else 0
    
    # Find the object
    if obj_addr not in tracked_objects:
        return False
        
    obj = tracked_objects[obj_addr]
    
    # Get new shape value
    new_shape = read_u64(process, wp_addr)
    
    # Get PC and backtrace
    pc = frame.FindRegister('pc')
    pc_val = int(pc.GetValue(), 0) if pc else 0
    
    # Create event record
    timestamp = time.time()
    bt = get_backtrace(thread, 5)
    
    event = {
        'time': timestamp,
        'old_shape': obj.shape_addr,
        'new_shape': new_shape,
        'pc': pc_val,
        'backtrace': bt
    }
    
    obj.history.append(event)
    
    # Check if this is corruption
    is_corruption = (new_shape == 0 or new_shape == 0xFFFFFFFFFFFFFFFF or 
                     new_shape < 0x1000 or new_shape > 0x0000FFFFFFFFFFFF)
    
    if is_corruption:
        obj.corrupted = True
        obj.shape_addr = new_shape  # Update to track changes
        
        print(f"\n{'='*60}")
        print(f"[RC] *** SHAPE CORRUPTION DETECTED ***")
        print(f"[RC] Object: 0x{obj_addr:x} ({obj.name})")
        print(f"[RC] New shape: 0x{new_shape:x}")
        print(f"[RC] PC: 0x{pc_val:x}")
        print(f"[RC] Backtrace:")
        for line in bt:
            print(f"[RC]   {line}")
        print(f"{'='*60}\n")
        
        if stop_on_corruption:
            return True  # Stop execution
    else:
        # Legitimate shape change (e.g., property added)
        obj.shape_addr = new_shape
        print(f"[RC] Shape change: 0x{obj_addr:x} -> 0x{new_shape:x} (PC: 0x{pc_val:x})")
        
    return False

def on_init_stubs(frame, bp_loc, dict):
    """Called when entering init_browser_stubs."""
    print("[RC] >>> Entering init_browser_stubs <<<")
    return False

def on_new_shape(frame, bp_loc, dict):
    """Called when js_new_shape_nohash returns."""
    debugger = lldb.debugger
    thread = debugger.GetSelectedTarget().GetProcess().GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    try:
        ret_val = int(frame.FindRegister('x0').GetValue(), 0)
        print(f"[RC] Shape created: 0x{ret_val:x}")
    except:
        pass
    return False

def on_mem_op(frame, bp_loc, dict):
    """Called on memory operations to detect potential buffer overflows."""
    debugger = lldb.debugger
    process = debugger.GetSelectedTarget().GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    # Get destination address (x0 for memset/memcpy/memmove)
    try:
        dst = int(frame.FindRegister('x0').GetValue(), 0)
        size_reg = int(frame.FindRegister('x2').GetValue(), 0)  # size is usually x2
    except:
        return False
        
    # Check if destination overlaps with any tracked object
    for obj_addr, obj in tracked_objects.items():
        # Check if write overlaps with object header (shape field at offset 8)
        if dst <= obj_addr + 8 < dst + size_reg:
            pc_val = int(frame.FindRegister('pc').GetValue(), 0)
            func_name = frame.GetFunctionName()
            
            print(f"\n[RC] WARNING: {func_name} writing to tracked object area!")
            print(f"[RC]   Dst: 0x{dst:x}, Size: {size_reg}")
            print(f"[RC]   Object: 0x{obj_addr:x} ({obj.name})")
            print(f"[RC]   PC: 0x{pc_val:x}")
            
            # Get backtrace
            bt = get_backtrace(thread, 5)
            print(f"[RC]   Backtrace:")
            for line in bt:
                print(f"[RC]     {line}")
                
            if stop_on_corruption:
                return True
                
    return False

def __lldb_init_module(debugger, internal_dict):
    """Initialize the module."""
    global g_debugger
    
    g_debugger = RootCauseDebugger(debugger, internal_dict)
    
    # Register commands
    debugger.HandleCommand('command script add -f lldb_root_cause.RootCauseDebugger.start rc-start')
    debugger.HandleCommand('command script add -f lldb_root_cause.RootCauseDebugger.status rc-status')
    debugger.HandleCommand('command script add -f lldb_root_cause.RootCauseDebugger.history rc-history')
    debugger.HandleCommand('command script add -f lldb_root_cause.RootCauseDebugger.check_object rc-check')
    debugger.HandleCommand('command script add -f lldb_root_cause.RootCauseDebugger.stop_toggle rc-stop-on-corruption')
    
    print("="*60)
    print("[RootCause] QuickJS Shape Corruption Root Cause Analyzer")
    print("="*60)
    print("\nThis script uses watchpoints to catch corruption in real-time.")
    print("\nCommands:")
    print("  rc-start          - Start tracking and set watchpoints")
    print("  rc-status         - Show tracked objects")
    print("  rc-history        - Show corruption history")
    print("  rc-check <addr>   - Check specific object")
    print("  rc-stop-on-corruption [on/off] - Control stop behavior")
    print("\nUsage:")
    print("  1. Attach to app: lldb -p $(adb shell pidof com.bgmdwldr.vulkan)")
    print("  2. Load script: command script import lldb_root_cause.py")
    print("  3. Start tracking: rc-start")
    print("  4. Continue: continue")
    print("  5. When corruption is caught, examine the backtrace")
    print("="*60)
