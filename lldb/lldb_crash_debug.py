#!/usr/bin/env python3
"""
LLDB script to debug the 0xc0000008 crash in JS_DefineProperty.
The crash pattern suggests a JSValue tag is being used as a pointer.
"""
import lldb

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_crash_debug.catch_crash catch_crash')
    debugger.HandleCommand('command script add -f lldb_crash_debug.trace_jsvalue trace_jsvalue')
    debugger.HandleCommand('command script add -f lldb_crash_debug.check_registers check_registers')
    debugger.HandleCommand('command script add -f lldb_crash_debug.break_on_bad_x28 break_on_bad_x28')
    print("Crash debug commands loaded:")
    print("  catch_crash - Set up crash detection")
    print("  trace_jsvalue <addr> - Trace JSValue at address")
    print("  check_registers - Check all registers for suspicious values")
    print("  break_on_bad_x28 - Break when x28 looks like a tagged value")

def catch_crash(debugger, command, result, internal_dict):
    """Set up comprehensive crash detection"""
    target = debugger.GetSelectedTarget()
    
    # Set breakpoint at JS_DefineProperty
    bp = target.BreakpointCreateByName("JS_DefineProperty")
    bp.SetScriptCallbackFunction("lldb_crash_debug.on_js_define_property")
    print(f"Set breakpoint at JS_DefineProperty (bp {bp.GetID()})")
    
    # Set breakpoint at JS_NewContext to trace from the start
    bp2 = target.BreakpointCreateByName("JS_NewContext")
    bp2.SetScriptCallbackFunction("lldb_crash_debug.on_js_newcontext")
    print(f"Set breakpoint at JS_NewContext (bp {bp2.GetID()})")
    
    # Set a breakpoint that checks x28 register at each step
    target.BreakpointCreateByRegex("^JS_")
    
def on_js_newcontext(frame, bp_loc, dict):
    """Called when JS_NewContext is entered"""
    print("\n[JS_NewContext] Entered - tracing from start...")
    
    # Get the rt argument
    rt = frame.FindVariable("rt")
    print(f"  rt = {rt.GetValue()}")
    
    # Step through and watch for x28 changes
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # Continue normal execution but with stepping
    return False

def on_js_define_property(frame, bp_loc, dict):
    """Called when JS_DefineProperty is entered - check for suspicious values"""
    print("\n[JS_DefineProperty] Entered")
    
    # Get arguments
    ctx = frame.FindVariable("ctx")
    obj = frame.FindVariable("obj")
    prop = frame.FindVariable("prop")
    
    print(f"  ctx = {ctx.GetValue()}")
    print(f"  obj = {obj.GetValue()}")
    
    # Check x28 register
    reg_x28 = frame.FindRegister("x28")
    x28_val = int(reg_x28.GetValue(), 0)
    print(f"  x28 = 0x{x28_val:016x}")
    
    # Check if x28 looks like a tagged pointer (has tag bits)
    if (x28_val & 0xF) != 0:
        tag = x28_val & 0xF
        print(f"  WARNING: x28 has tag bits set! tag={tag}")
        if x28_val == 0xc0000000:
            print("  CRITICAL: x28 = 0xc0000000 - this is the crash value!")
            # Stop here
            return True
    
    # Check x0 (first argument, should be ctx)
    reg_x0 = frame.FindRegister("x0")
    x0_val = int(reg_x0.GetValue(), 0)
    print(f"  x0 = 0x{x0_val:016x} (should be ctx)")
    
    # Check if x0 is valid
    if x0_val == 0 or x0_val == 0xc0000000:
        print(f"  ERROR: x0 looks invalid!")
        return True
    
    return False

def trace_jsvalue(debugger, command, result, internal_dict):
    """Trace a JSValue at a given memory address"""
    if not command:
        print("Usage: trace_jsvalue <address>")
        return
    
    addr = int(command, 0)
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    error = lldb.SBError()
    
    # Read 16 bytes (JSValue size)
    data = process.ReadMemory(addr, 16, error)
    if not error.Success():
        print(f"Cannot read memory at 0x{addr:016x}: {error.GetCString()}")
        return
    
    # Interpret based on JSValue representation
    # For non-NaN boxing (struct-based):
    # bytes 0-7: union (handle or pointer)
    # bytes 8-15: tag (int64_t)
    
    import struct
    u_val = struct.unpack('<Q', data[0:8])[0]
    tag = struct.unpack('<q', data[8:16])[0]
    
    print(f"JSValue at 0x{addr:016x}:")
    print(f"  union (u.handle): 0x{u_val:016x} ({u_val})")
    print(f"  tag: 0x{tag:016x} ({tag})")
    
    if tag < 0:
        print(f"  -> Reference type (handle-based)")
        # Try to dereference the handle
        # Need to find the handle table - this is in the global GC state
    else:
        print(f"  -> Value type")

def check_registers(debugger, command, result, internal_dict):
    """Check all registers for suspicious values (like 0xc0000000)"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    suspicious_patterns = [0xc0000000, 0xc0000008]
    
    print("Checking registers for suspicious values...")
    
    # Check all general purpose registers
    for i in range(31):
        reg = frame.FindRegister(f"x{i}")
        val = int(reg.GetValue(), 0)
        
        # Check for suspicious patterns
        for pattern in suspicious_patterns:
            if val == pattern:
                print(f"  x{i} = 0x{val:016x} <- MATCHES CRASH PATTERN!")
                break
        else:
            # Check if value has tag bits set and looks like a JSValue
            if (val & 0xF) != 0 and (val >> 4) == 0:
                tag = val & 0xF
                print(f"  x{i} = 0x{val:016x} (possible tagged value, tag={tag})")

def break_on_bad_x28(debugger, command, result, internal_dict):
    """Set a breakpoint that stops when x28 contains 0xc0000000"""
    target = debugger.GetSelectedTarget()
    
    # Create a breakpoint with a condition
    bp = target.BreakpointCreateByName("JS_DefineProperty")
    
    # This is tricky in Python - we'll use a callback instead
    bp.SetScriptCallbackFunction("lldb_crash_debug.check_x28_callback")
    print(f"Set conditional breakpoint at JS_DefineProperty (bp {bp.GetID()})")

def check_x28_callback(frame, bp_loc, dict):
    """Callback to check x28 and stop if it's bad"""
    reg_x28 = frame.FindRegister("x28")
    x28_val = int(reg_x28.GetValue(), 0)
    
    if x28_val == 0xc0000000 or x28_val == 0xc0000008:
        print(f"\n*** BAD x28 DETECTED: 0x{x28_val:016x} ***")
        print("At:")
        print(f"  Function: {frame.GetFunctionName()}")
        print(f"  File: {frame.GetLineEntry().GetFileSpec().GetFilename()}")
        print(f"  Line: {frame.GetLineEntry().GetLine()}")
        
        # Print backtrace
        print("\nBacktrace:")
        thread = frame.GetThread()
        for i, f in enumerate(thread.get_thread_frames()):
            print(f"  #{i} {f.GetFunctionName()}")
        
        return True  # Stop
    
    return False  # Continue

if __name__ == '__main__':
    print("This script is meant to be run within LLDB")
    print("Load it with: command script import lldb_crash_debug.py")
