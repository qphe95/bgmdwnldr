#!/usr/bin/env python3
"""
LLDB script to trace the origin of the 0xc0000000 value in x28 register.
This sets watchpoints and breakpoints to find where the bad value comes from.
"""
import lldb

# Global state to track what we've found
last_known_good_x28 = None
step_count = 0

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_trace_x28.trace_x28_origin trace_x28_origin')
    debugger.HandleCommand('command script add -f lldb_trace_x28.watch_x28 watch_x28')
    debugger.HandleCommand('command script add -f lldb_trace_x28.step_until_bad_x28 step_until_bad_x28')
    print("x28 tracing commands loaded:")
    print("  trace_x28_origin - Set up full trace to find where x28 becomes 0xc0000000")
    print("  watch_x28 - Set a watchpoint on x28 register (if supported)")
    print("  step_until_bad_x28 - Single step until x28 shows bad value")

def trace_x28_origin(debugger, command, result, internal_dict):
    """
    Comprehensive trace to find origin of bad x28 value.
    Strategy:
    1. Break at JS_NewContextRaw (start)
    2. Break at JS_DefineProperty (crash site)
    3. Step through and record when x28 changes
    """
    target = debugger.GetSelectedTarget()
    
    # Break at JS_NewContext to start tracing
    bp_start = target.BreakpointCreateByName("JS_NewContextRaw")
    bp_start.SetScriptCallbackFunction("lldb_trace_x28.on_context_start")
    print(f"[trace_x28_origin] Set start breakpoint at JS_NewContextRaw (bp {bp_start.GetID()})")
    
    # Break at JS_DefineProperty to catch near-crash
    bp_crash = target.BreakpointCreateByName("JS_DefineProperty")
    bp_crash.SetScriptCallbackFunction("lldb_trace_x28.on_js_define_property")
    print(f"[trace_x28_origin] Set crash breakpoint at JS_DefineProperty (bp {bp_crash.GetID()})")

def on_context_start(frame, bp_loc, dict):
    """Called when JS_NewContextRaw starts - begin tracing"""
    print("\n[trace] JS_NewContextRaw started - beginning x28 trace")
    
    # Record initial x28
    reg_x28 = frame.FindRegister("x28")
    x28_val = int(reg_x28.GetValue(), 0)
    print(f"[trace] Initial x28 = 0x{x28_val:016x}")
    
    global last_known_good_x28
    last_known_good_x28 = x28_val
    
    # Set a one-shot breakpoint at the return to check x28 again
    thread = frame.GetThread()
    process = thread.GetProcess()
    target = process.GetTarget()
    
    return False

def on_js_define_property(frame, bp_loc, dict):
    """Called when JS_DefineProperty is entered"""
    reg_x28 = frame.FindRegister("x28")
    x28_val = int(reg_x28.GetValue(), 0)
    
    print(f"\n[trace] JS_DefineProperty entered, x28 = 0x{x28_val:016x}")
    
    # Check if this is the bad value
    if x28_val == 0xc0000000 or x28_val == 0xc0000008:
        print("[trace] *** FOUND BAD x28 VALUE! ***")
        print("[trace] Stopping for inspection")
        return True  # Stop
    
    return False  # Continue

def watch_x28(debugger, command, result, internal_dict):
    """
    Try to set up a watchpoint on x28.
    Note: Hardware watchpoints on registers are tricky; this may not work on all platforms.
    """
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    print("Attempting to watch x28 register...")
    print("Note: Register watchpoints may not be supported on all targets")
    
    # Get current x28 value
    reg_x28 = frame.FindRegister("x28")
    x28_val = int(reg_x28.GetValue(), 0)
    
    print(f"Current x28 = 0x{x28_val:016x}")
    print("Use 'step_until_bad_x28' instead for software stepping")

def step_until_bad_x28(debugger, command, result, internal_dict):
    """
    Single step through code until x28 becomes 0xc0000000.
    This is slow but works everywhere.
    """
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    max_steps = int(command) if command else 1000
    
    print(f"Stepping up to {max_steps} instructions, watching for bad x28...")
    
    prev_x28 = None
    
    for i in range(max_steps):
        frame = thread.GetSelectedFrame()
        reg_x28 = frame.FindRegister("x28")
        x28_val = int(reg_x28.GetValue(), 0)
        pc = frame.FindRegister("pc")
        pc_val = int(pc.GetValue(), 0)
        
        # Check for change
        if x28_val != prev_x28:
            func_name = frame.GetFunctionName() or "??"
            print(f"Step {i}: x28 changed to 0x{x28_val:016x} at 0x{pc_val:016x} ({func_name})")
            prev_x28 = x28_val
        
        # Check for bad value
        if x28_val == 0xc0000000 or x28_val == 0xc0000008:
            print(f"\n*** BAD x28 DETECTED at step {i}! ***")
            print(f"  PC: 0x{pc_val:016x}")
            print(f"  Function: {frame.GetFunctionName()}")
            print(f"  x28: 0x{x28_val:016x}")
            
            # Print backtrace
            print("\nBacktrace:")
            for j, f in enumerate(thread.get_thread_frames()[:10]):
                print(f"  #{j} {f.GetFunctionName()}")
            
            return
        
        # Step one instruction
        thread.StepInstruction(False)
    
    print(f"Completed {max_steps} steps without finding bad x28")

if __name__ == '__main__':
    print("This script is meant to be run within LLDB")
