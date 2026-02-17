#!/usr/bin/env python3
"""
LLDB Python script to catch and analyze the QuickJS shape corruption crash.
Usage: lldb -o 'command script import lldb_crash_catch.py' -o 'crash-catch-start'
"""

import lldb
import sys

# Enable Python to find the lldb module
sys.path.insert(0, '/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/Python')

def print_obj_info(obj_ptr, debugger, result):
    """Print information about a JSObject at given pointer."""
    target = debugger.GetSelectedTarget()
    error = lldb.SBError()
    
    # JSObject layout on ARM64:
    # offset 0: class_id (uint16)
    # offset 4: weakref_count (uint32)
    # offset 8: shape pointer (JSShape*)
    
    # Read shape pointer (8 bytes at offset 8)
    shape_ptr = target.GetProcess().ReadMemory(obj_ptr + 8, 8, error)
    if error.Success() and shape_ptr:
        import struct
        shape_addr = struct.unpack('<Q', shape_ptr)[0]
        result.write(f"  JSObject at 0x{obj_ptr:x}:\n")
        result.write(f"    shape pointer: 0x{shape_addr:x}")
        
        # Check if shape pointer looks valid
        if shape_addr < 0x1000:
            result.write(f" (INVALID - looks like tagged value!)\n")
            # Try to interpret as QuickJS tag
            tag = shape_addr & 0xf
            if tag == 1:
                result.write(f"    -> Tag 0x1 = JS_TAG_INT or JS_UNDEFINED low bits\n")
            elif tag == 3:
                result.write(f"    -> Tag 0x3 = JS_TAG_EXCEPTION\n")
            elif tag == 4:
                result.write(f"    -> Tag 0x4 = JS_TAG_UNDEFINED\n")
            elif tag == 5:
                result.write(f"    -> Tag 0x5 = JS_TAG_NULL\n")
            elif tag == 6:
                result.write(f"    -> Tag 0x6 = JS_TAG_BOOL\n")
            elif tag == 7:
                result.write(f"    -> Tag 0x7 = JS_TAG_EXCEPTION (alternate)\n")
        else:
            result.write(f" (looks valid)\n")
            
        # Read class_id (2 bytes at offset 0)
        class_data = target.GetProcess().ReadMemory(obj_ptr, 2, error)
        if error.Success() and class_data:
            class_id = struct.unpack('<H', class_data)[0]
            result.write(f"    class_id: {class_id}\n")
    else:
        result.write(f"  Failed to read memory at 0x{obj_ptr:x}: {error.GetCString()}\n")

def analyze_crash(thread, debugger, result):
    """Analyze crash state and print useful information."""
    frame = thread.GetSelectedFrame()
    
    result.write("\n" + "="*60 + "\n")
    result.write("CRASH ANALYSIS\n")
    result.write("="*60 + "\n")
    
    # Get function name
    func_name = frame.GetFunctionName()
    result.write(f"Crashed in: {func_name}\n")
    
    # Get registers
    result.write("\nRegisters:\n")
    for reg_name in ['x0', 'x1', 'x2', 'lr', 'sp', 'pc']:
        reg = frame.FindRegister(reg_name)
        result.write(f"  {reg_name}: {reg.GetValue()}\n")
    
    # In JS_SetPropertyInternal, x0 is the object being modified
    if 'JS_SetProperty' in func_name:
        x0 = frame.FindRegister('x0')
        obj_ptr = int(x0.GetValue(), 0)
        result.write(f"\nObject being modified:\n")
        print_obj_info(obj_ptr, debugger, result)
    
    # Print backtrace
    result.write("\nBacktrace:\n")
    for i, f in enumerate(thread.frames[:10]):
        result.write(f"  #{i}: {f.GetFunctionName()} at 0x{f.GetPC():x}\n")
    
    result.write("="*60 + "\n")

class CrashCatchCommand:
    def __init__(self, debugger, session_dict):
        self.debugger = debugger
        self.session_dict = session_dict
        self.target = None
        self.process = None
        
    def start(self, args, result):
        """Start catching crashes."""
        result.write("[CrashCatch] Starting crash catcher...\n")
        
        self.target = self.debugger.GetSelectedTarget()
        if not self.target:
            result.write("[CrashCatch] ERROR: No target selected\n")
            return
            
        # Set up SIGSEGV handler
        self.target.GetProcess().GetUnixSignals().SetShouldStop(11, True)  # SIGSEGV
        self.target.GetProcess().GetUnixSignals().SetShouldNotify(11, True)
        result.write("[CrashCatch] SIGSEGV will stop execution\n")
        
        # Set pending breakpoints on key functions
        bp_names = ['init_browser_stubs', 'JS_SetPropertyStr', 'JS_SetPropertyInternal']
        for name in bp_names:
            bp = self.target.BreakpointCreateByName(name)
            bp.SetEnabled(True)
            result.write(f"[CrashCatch] Set breakpoint on {name} ({bp.GetNumLocations()} locations)\n")
        
        result.write("[CrashCatch] Ready. Continue execution with 'continue'\n")
        
    def check(self, args, result):
        """Check current state."""
        if not self.target:
            result.write("[CrashCatch] No target\n")
            return
            
        process = self.target.GetProcess()
        state = process.GetState()
        
        result.write(f"[CrashCatch] Process state: {lldb.SBDebugger.StateAsCString(state)}\n")
        
        if state == lldb.eStateStopped:
            thread = process.GetSelectedThread()
            stop_reason = thread.GetStopReason()
            result.write(f"[CrashCatch] Stop reason: {stop_reason}\n")
            
            if stop_reason == lldb.eStopReasonSignal:
                sig = thread.GetStopReasonDataAtIndex(0)
                result.write(f"[CrashCatch] Signal: {sig} (SIGSEGV={11})\n")
                if sig == 11:  # SIGSEGV
                    analyze_crash(thread, self.debugger, result)
                    
            elif stop_reason == lldb.eStopReasonBreakpoint:
                bp_id = thread.GetStopReasonDataAtIndex(0)
                result.write(f"[CrashCatch] Hit breakpoint {bp_id}\n")
                
                # Print current function
                frame = thread.GetSelectedFrame()
                result.write(f"[CrashCatch] At: {frame.GetFunctionName()}\n")
                
                # If in JS_SetPropertyStr, analyze the object
                if 'JS_SetProperty' in frame.GetFunctionName():
                    x0 = frame.FindRegister('x0')
                    obj_ptr = int(x0.GetValue(), 0)
                    result.write(f"\nObject being modified:\n")
                    print_obj_info(obj_ptr, self.debugger, result)

def __lldb_init_module(debugger, internal_dict):
    """Initialize the LLDB module."""
    command = CrashCatchCommand(debugger, internal_dict)
    
    # Register commands
    debugger.HandleCommand('command script add -f lldb_crash_catch.CrashCatchCommand.start crash-catch-start')
    debugger.HandleCommand('command script add -f lldb_crash_catch.CrashCatchCommand.check crash-check')
    
    print("[CrashCatch] Module loaded. Commands: crash-catch-start, crash-check")

# For direct import
def CrashCatchCommand_factory(debugger, internal_dict, args):
    command = CrashCatchCommand(debugger, internal_dict)
    return command
