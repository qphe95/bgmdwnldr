#!/usr/bin/env python3
"""
LLDB script to debug the 0xc0000008 crash.
The crash address pattern suggests a JSValue tag (0xc...) is being used as a pointer.
"""
import lldb

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_register_debug.check_x28 check_x28')
    debugger.HandleCommand('command script add -f lldb_register_debug.check_jsvalue check_jsvalue')
    print("Register debug commands loaded:")
    print("  check_x28 - Check x28 register for suspicious values")
    print("  check_jsvalue <addr> - Decode JSValue at address")

def check_x28(debugger, command, result, internal_dict):
    """Check x28 register which holds the suspicious 0xc0000000 value"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    # Read x28 register
    reg_x28 = frame.FindRegister("x28")
    val = int(reg_x28.GetValue(), 0)
    
    print(f"x28 = 0x{val:016x}")
    
    # Check if this looks like a tagged pointer
    if (val & 0xF) != 0:
        tag = val & 0xF
        ptr_val = val & ~0xF
        print(f"  Looks like tagged pointer: tag={tag}, ptr=0x{ptr_val:016x}")
        
        # Try to read memory at this address
        error = lldb.SBError()
        target.ReadMemory(lldb.SBAddress(ptr_val, target), 16, error)
        if error.Success():
            print(f"  Memory at 0x{ptr_val:016x} is readable")
        else:
            print(f"  Memory at 0x{ptr_val:016x} is NOT readable: {error.GetCString()}")
    
    # Check for specific patterns
    if val == 0xc0000000:
        print("  WARNING: x28 = 0xc0000000 - this is a JSValue tag pattern!")
        print("  This suggests a JSValue is being used as a pointer")

def check_jsvalue(debugger, command, result, internal_dict):
    """Decode JSValue at given address"""
    if not command:
        print("Usage: check_jsvalue <address>")
        return
    
    addr = int(command, 0)
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    
    # Read 8 bytes (JSValue size for non-NaN boxing)
    error = lldb.SBError()
    data = process.ReadMemory(addr, 16, error)
    
    if not error.Success():
        print(f"Cannot read memory at 0x{addr:016x}: {error.GetCString()}")
        return
    
    # Interpret as JSValue struct (tag + union)
    import struct
    tag = struct.unpack('<q', data[8:16])[0]  # int64_t tag
    u_val = struct.unpack('<Q', data[0:8])[0]  # union value
    
    print(f"JSValue at 0x{addr:016x}:")
    print(f"  tag: 0x{tag:016x} ({tag})")
    print(f"  u:   0x{u_val:016x}")
    
    if tag < 0:
        print(f"  Type: Reference (handle-based)")
        print(f"  Handle: {u_val & 0xFFFFFFFF}")
    else:
        print(f"  Type: Value type")

if __name__ == '__main__':
    print("This script is meant to be run within LLDB")
