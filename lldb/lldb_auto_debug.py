#!/usr/bin/env python3
"""
LLDB Python script for automated QuickJS shape corruption debugging

Usage:
1. Start app: adb shell am start -n com.bgmdwldr.vulkan/.MainActivity
2. Get PID: adb shell pidof com.bgmdwldr.vulkan
3. Start lldb-server: adb shell /data/local/tmp/lldb-server platform --listen "*:5039" --server
4. Forward port: adb forward tcp:5039 tcp:5039
5. Run: lldb -o "command script import lldb_auto_debug.py" -o "quickjs_debug <PID>"
"""

import lldb
import re
import time

def quickjs_debug(debugger, command, result, internal_dict):
    """Main debugging command"""
    args = command.split()
    if len(args) < 1:
        print("Usage: quickjs_debug <PID>")
        return
    
    pid = args[0]
    target = debugger.GetSelectedTarget()
    
    print(f"=== QuickJS Shape Corruption Debugger ===")
    print(f"Attaching to PID {pid}...")
    
    # Attach to process
    error = lldb.SBError()
    process = target.AttachToProcessWithID(lldb.SBListener(), int(pid), error)
    
    if not error.Success():
        print(f"Failed to attach: {error.GetCString()}")
        return
    
    print(f"Attached successfully!")
    
    # Set breakpoint at JS_AddIntrinsicBasicObjects
    bp1 = target.BreakpointCreateByName("JS_AddIntrinsicBasicObjects")
    print(f"Set breakpoint at JS_AddIntrinsicBasicObjects (ID: {bp1.GetID()})")
    
    # Continue execution
    print("Continuing to JS_AddIntrinsicBasicObjects...")
    process.Continue()
    
    # Wait for breakpoint
    state = process.GetState()
    if state == lldb.eStateStopped:
        print("=== Hit JS_AddIntrinsicBasicObjects ===")
        thread = process.GetSelectedThread()
        frame = thread.GetSelectedFrame()
        print(f"Frame: {frame.GetFunctionName()}")
        
        # Step until global_obj is created (let it run)
        print("Continuing to let global_obj be created...")
        process.Continue()
        time.sleep(2)
        
        # Now set breakpoint at init_browser_stubs
        bp2 = target.BreakpointCreateByName("init_browser_stubs")
        print(f"Set breakpoint at init_browser_stubs (ID: {bp2.GetID()})")
        
        process.Continue()
        
        if process.GetState() == lldb.eStateStopped:
            print("=== Hit init_browser_stubs ===")
            
            # At this point, check logcat for shape address
            print("\n=== ACTION REQUIRED ===")
            print("Check logcat for the shape address:")
            print("  adb logcat -d | grep 'DEBUG.*shape_addr'")
            print("\nThen in LLDB:")
            print("  (lldb) expression JSObject *p = JS_VALUE_GET_OBJ(ctx->global_obj); &p->shape")
            print("  (lldb) watchpoint set expression -w write -- <shape_addr>")
            print("  (lldb) continue")
            
            # Try to get shape address from expression
            frame = process.GetSelectedThread().GetSelectedFrame()
            
            # Get ctx pointer
            ctx_val = frame.FindVariable("ctx")
            if ctx_val.IsValid():
                print(f"\nctx = {ctx_val.GetValue()}")
                
                # Try to evaluate expression to get shape address
                # This might not work due to type info, so we rely on logcat
                result = lldb.SBCommandReturnObject()
                interpreter = debugger.GetCommandInterpreter()
                interpreter.HandleCommand(
                    "expression JSObject *p = JS_VALUE_GET_OBJ(ctx->global_obj); &p->shape", 
                    result
                )
                print(f"Expression result: {result.GetOutput()}")

def __lldb_init_module(debugger, internal_dict):
    """Register commands when module is loaded"""
    debugger.HandleCommand(
        'command script add -f lldb_auto_debug.quickjs_debug quickjs_debug'
    )
    print("QuickJS debugger loaded. Use: quickjs_debug <PID>")
