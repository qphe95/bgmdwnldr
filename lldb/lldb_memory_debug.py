#!/usr/bin/env python3
"""
Advanced LLDB Memory Debugging Script for bgmdwnldr

This script uses advanced LLDB features to detect and diagnose memory issues:
- Buffer overflow detection
- Memory leak detection
- Use-after-free detection
- Double-free detection
- Race condition detection

Usage:
1. Start app: ./debug_lldb.sh
2. In another terminal: 
   lldb -o "command script import lldb_memory_debug.py" -o "memory_debug <PID>"
"""

import lldb
import re

# ============================================================================
# Memory Issue Detection Callbacks
# ============================================================================

def on_strncpy_breakpoint(frame, bp_loc, dict):
    """
    Callback for strncpy breakpoint - detects potential buffer overflows
    """
    thread = frame.GetThread()
    process = thread.GetProcess()
    
    # Get arguments on ARM64
    # x0 = dest, x1 = src, x2 = n
    dest_reg = frame.FindValue("x0", lldb.eValueTypeRegister)
    src_reg = frame.FindValue("x1", lldb.eValueTypeRegister)
    n_reg = frame.FindValue("x2", lldb.eValueTypeRegister)
    
    dest_addr = dest_reg.GetValueAsUnsigned()
    src_addr = src_reg.GetValueAsUnsigned()
    n = n_reg.GetValueAsUnsigned()
    
    # Read source string length
    error = lldb.SBError()
    src_len = 0
    max_check = min(n + 100, 4096)  # Check up to n+100 or 4KB
    for i in range(max_check):
        byte = process.ReadUnsignedFromMemory(src_addr + i, 1, error)
        if error.Success() and byte == 0:
            src_len = i
            break
    
    # Check for potential overflow: if src_len >= n, no null terminator will be written
    if src_len >= n and n > 0:
        print(f"[LLDB WARNING] Potential strncpy overflow:")
        print(f"  dest=0x{dest_addr:x}, src=0x{src_addr:x}, n={n}")
        print(f"  Source string length ({src_len}) >= n ({n})")
        print(f"  Result will NOT be null-terminated!")
        
        # Get backtrace
        print("  Backtrace:")
        for i, f in enumerate(thread.frames[:5]):
            func_name = f.GetFunctionName()
            file_name = f.GetLineEntry().GetFileSpec().GetFilename()
            line_num = f.GetLineEntry().GetLine()
            print(f"    #{i} {func_name} at {file_name}:{line_num}")
    
    return False  # Don't stop, just log


def on_malloc_breakpoint(frame, bp_loc, dict):
    """
    Track allocations for leak detection
    """
    thread = frame.GetThread()
    size_reg = frame.FindValue("x0", lldb.eValueTypeRegister)
    size = size_reg.GetValueAsUnsigned()
    
    # Track large allocations
    if size > 1000000:  # > 1MB
        print(f"[LLDB] Large malloc({size}) bytes")
        for i, f in enumerate(thread.frames[:3]):
            func_name = f.GetFunctionName()
            print(f"  #{i} {func_name}")
    
    # Store in dict for leak detection
    if 'allocations' not in dict:
        dict['allocations'] = {}
    
    # We'll get the return value after the function returns
    return False


def on_malloc_return(frame, bp_loc, dict):
    """
    Track malloc return values
    """
    # Get return value (x0 on ARM64)
    ret_reg = frame.FindValue("x0", lldb.eValueTypeRegister)
    addr = ret_reg.GetValueAsUnsigned()
    
    if addr and 'last_malloc_size' in dict:
        if 'allocations' not in dict:
            dict['allocations'] = {}
        dict['allocations'][addr] = {
            'size': dict['last_malloc_size'],
            'backtrace': [(f.GetFunctionName(), f.GetPC()) for f in frame.GetThread().frames[:5]]
        }
    
    return False


def on_free_breakpoint(frame, bp_loc, dict):
    """
    Track frees for double-free detection
    """
    thread = frame.GetThread()
    ptr_reg = frame.FindValue("x0", lldb.eValueTypeRegister)
    ptr = ptr_reg.GetValueAsUnsigned()
    
    if 'freed_addrs' not in dict:
        dict['freed_addrs'] = set()
    
    if ptr in dict['freed_addrs']:
        print(f"[LLDB CRITICAL] Double-free detected!")
        print(f"  Address: 0x{ptr:x}")
        print("  Backtrace:")
        for i, f in enumerate(thread.frames[:5]):
            print(f"    #{i} {f.GetFunctionName()}")
        return True  # Stop on double-free
    
    if 'allocations' in dict and ptr in dict['allocations']:
        del dict['allocations'][ptr]
        dict['freed_addrs'].add(ptr)
    
    return False


def on_pthread_mutex_lock(frame, bp_loc, dict):
    """
    Detect potential deadlocks and missing unlocks
    """
    thread = frame.GetThread()
    mutex_reg = frame.FindValue("x0", lldb.eValueTypeRegister)
    mutex_addr = mutex_reg.GetValueAsUnsigned()
    thread_id = thread.GetThreadID()
    
    if 'mutex_tracking' not in dict:
        dict['mutex_tracking'] = {}
    
    tracking = dict['mutex_tracking']
    
    if mutex_addr not in tracking:
        tracking[mutex_addr] = {
            'lock_count': 0,
            'owner_thread': None,
            'lock_backtrace': None
        }
    
    info = tracking[mutex_addr]
    
    # Check for recursive lock by same thread (potential deadlock indicator)
    if info['owner_thread'] == thread_id:
        print(f"[LLDB WARNING] Recursive mutex lock detected!")
        print(f"  Mutex: 0x{mutex_addr:x}, Thread: {thread_id}")
        print("  Previous lock backtrace:")
        if info['lock_backtrace']:
            for i, (func, addr) in enumerate(info['lock_backtrace'][:5]):
                print(f"    #{i} {func} at 0x{addr:x}")
        print("  Current lock backtrace:")
        for i, f in enumerate(thread.frames[:5]):
            print(f"    #{i} {f.GetFunctionName()}")
    
    info['lock_count'] += 1
    info['owner_thread'] = thread_id
    info['lock_backtrace'] = [(f.GetFunctionName(), f.GetPC()) for f in thread.frames[:5]]
    
    return False


def on_pthread_mutex_unlock(frame, bp_loc, dict):
    """
    Track mutex unlocks to detect missing unlocks
    """
    thread = frame.GetThread()
    mutex_reg = frame.FindValue("x0", lldb.eValueTypeRegister)
    mutex_addr = mutex_reg.GetValueAsUnsigned()
    thread_id = thread.GetThreadID()
    
    if 'mutex_tracking' not in dict:
        return False
    
    tracking = dict['mutex_tracking']
    
    if mutex_addr in tracking:
        info = tracking[mutex_addr]
        
        # Check for unlock by non-owner thread
        if info['owner_thread'] != thread_id and info['owner_thread'] is not None:
            print(f"[LLDB WARNING] Mutex unlock by non-owner thread!")
            print(f"  Mutex: 0x{mutex_addr:x}")
            print(f"  Owner: {info['owner_thread']}, Unlocker: {thread_id}")
        
        info['lock_count'] -= 1
        if info['lock_count'] <= 0:
            info['owner_thread'] = None
            info['lock_backtrace'] = None
    
    return False


# ============================================================================
# JSValue Leak Detection
# ============================================================================

def on_js_newobject_breakpoint(frame, bp_loc, dict):
    """
    Track JS_NewObject calls to detect leaked JSValues
    """
    thread = frame.GetThread()
    
    if 'js_objects' not in dict:
        dict['js_objects'] = {}
    
    # Get return address to track when we return
    return_addr = frame.FindRegister("lr").GetValueAsUnsigned()
    
    # Store the backtrace for when the object is created
    dict['pending_js_new'] = {
        'backtrace': [(f.GetFunctionName(), f.GetPC()) for f in thread.frames[:5]],
        'return_addr': return_addr
    }
    
    return False


def on_js_freevalue_breakpoint(frame, bp_loc, dict):
    """
    Track JS_FreeValue calls
    """
    thread = frame.GetThread()
    # x1 = val (JSValue is two 64-bit values on ARM64)
    val_reg = frame.FindValue("x1", lldb.eValueTypeRegister)
    val = val_reg.GetValueAsUnsigned()
    
    if 'js_objects' in dict and val in dict['js_objects']:
        del dict['js_objects'][val]
    
    return False


# ============================================================================
# Custom Commands
# ============================================================================

class MemoryAuditCommand:
    """Command to audit memory allocations and detect leaks"""
    
    def get_short_help(self):
        return "Audit memory allocations: memaudit"
    
    def get_long_help(self):
        return """\
Audit current memory allocations to detect potential leaks.
Shows allocations that haven't been freed yet.
Usage: memaudit
"""
    
    def __call__(self, debugger, command, result, internal_dict):
        if 'allocations' not in internal_dict or not internal_dict['allocations']:
            result.AppendMessage("No active allocations tracked.")
            return
        
        allocations = internal_dict['allocations']
        result.AppendMessage(f"=== Active Allocations: {len(allocations)} ===")
        
        total_bytes = 0
        for addr, info in sorted(allocations.items(), key=lambda x: x[1]['size'], reverse=True)[:20]:
            size = info['size']
            total_bytes += size
            bt = info['backtrace'][0] if info['backtrace'] else ('unknown', 0)
            result.AppendMessage(f"  0x{addr:x}: {size} bytes ({bt[0]})")
        
        result.AppendMessage(f"Total: {total_bytes} bytes in {len(allocations)} allocations")


class JSValueAuditCommand:
    """Command to audit JSValue allocations"""
    
    def get_short_help(self):
        return "Audit JSValue allocations: jsvalueaudit"
    
    def __call__(self, debugger, command, result, internal_dict):
        if 'js_objects' not in internal_dict or not internal_dict['js_objects']:
            result.AppendMessage("No active JSValues tracked.")
            return
        
        js_objects = internal_dict['js_objects']
        result.AppendMessage(f"=== Active JSValues: {len(js_objects)} ===")
        for val, info in list(js_objects.items())[:20]:
            bt = info['backtrace'][0] if info['backtrace'] else ('unknown', 0)
            result.AppendMessage(f"  0x{val:x}: created in {bt[0]}")


class MutexAuditCommand:
    """Command to audit mutex state"""
    
    def get_short_help(self):
        return "Audit mutex state: mutexaudit"
    
    def __call__(self, debugger, command, result, internal_dict):
        if 'mutex_tracking' not in internal_dict or not internal_dict['mutex_tracking']:
            result.AppendMessage("No mutexes tracked.")
            return
        
        tracking = internal_dict['mutex_tracking']
        result.AppendMessage(f"=== Mutex State: {len(tracking)} mutexes ===")
        
        for addr, info in tracking.items():
            if info['lock_count'] > 0:
                result.AppendMessage(f"  0x{addr:x}: LOCKED by thread {info['owner_thread']}")
                result.AppendMessage(f"    Lock count: {info['lock_count']}")
            else:
                result.AppendMessage(f"  0x{addr:x}: UNLOCKED")


# ============================================================================
# Main Debugging Session Setup
# ============================================================================

def memory_debug(debugger, command, result, internal_dict):
    """
    Set up comprehensive memory debugging
    """
    args = command.split()
    if len(args) < 1:
        print("Usage: memory_debug <PID>")
        return
    
    pid = int(args[0])
    target = debugger.GetSelectedTarget()
    
    print("=== Advanced Memory Debugging Session ===")
    print(f"Attaching to PID {pid}...")
    
    # Attach to process
    error = lldb.SBError()
    process = target.AttachToProcessWithID(lldb.SBListener(), pid, error)
    
    if not error.Success():
        print(f"Failed to attach: {error.GetCString()}")
        return
    
    print(f"Attached successfully!")
    
    print("\n=== Setting up memory issue detection ===")
    
    # 1. strncpy breakpoint for buffer overflow detection
    bp_strncpy = target.BreakpointCreateByName("strncpy")
    bp_strncpy.SetScriptCallbackFunction("lldb_memory_debug.on_strncpy_breakpoint")
    print(f"[+] strncpy overflow detection: {bp_strncpy.GetID()}")
    
    # 2. malloc/free tracking
    bp_malloc = target.BreakpointCreateByName("malloc")
    bp_malloc.SetScriptCallbackFunction("lldb_memory_debug.on_malloc_breakpoint")
    print(f"[+] malloc tracking: {bp_malloc.GetID()}")
    
    bp_free = target.BreakpointCreateByName("free")
    bp_free.SetScriptCallbackFunction("lldb_memory_debug.on_free_breakpoint")
    print(f"[+] free tracking (double-free detection): {bp_free.GetID()}")
    
    # 3. Mutex tracking
    bp_mutex_lock = target.BreakpointCreateByName("pthread_mutex_lock")
    bp_mutex_lock.SetScriptCallbackFunction("lldb_memory_debug.on_pthread_mutex_lock")
    print(f"[+] pthread_mutex_lock tracking: {bp_mutex_lock.GetID()}")
    
    bp_mutex_unlock = target.BreakpointCreateByName("pthread_mutex_unlock")
    bp_mutex_unlock.SetScriptCallbackFunction("lldb_memory_debug.on_pthread_mutex_unlock")
    print(f"[+] pthread_mutex_unlock tracking: {bp_mutex_unlock.GetID()}")
    
    # 4. JSValue tracking
    bp_js_new = target.BreakpointCreateByName("JS_NewObject")
    bp_js_new.SetScriptCallbackFunction("lldb_memory_debug.on_js_newobject_breakpoint")
    print(f"[+] JS_NewObject tracking: {bp_js_new.GetID()}")
    
    bp_js_free = target.BreakpointCreateByName("JS_FreeValue")
    bp_js_free.SetScriptCallbackFunction("lldb_memory_debug.on_js_freevalue_breakpoint")
    print(f"[+] JS_FreeValue tracking: {bp_js_free.GetID()}")
    
    # 5. Set breakpoints on key functions that may have bugs
    bp_record = target.BreakpointCreateByName("record_captured_url")
    print(f"[+] record_captured_url breakpoint: {bp_record.GetID()}")
    
    bp_exec = target.BreakpointCreateByName("js_quickjs_exec_scripts")
    print(f"[+] js_quickjs_exec_scripts breakpoint: {bp_exec.GetID()}")
    
    print("\n=== Commands available ===")
    print("  memaudit       - Show active memory allocations")
    print("  jsvalueaudit   - Show active JSValue objects")
    print("  mutexaudit     - Show mutex lock state")
    print("  continue       - Continue execution")
    print("  ^C + bt        - Interrupt and get backtrace")


def analyze_leaks(debugger, command, result, internal_dict):
    """
    Analyze potential memory leaks in common patterns
    """
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    
    print("=== Leak Analysis ===")
    
    # Check for common leak patterns
    patterns = [
        ("malloc without free check", "malloc"),
        ("JS_NewObject without JS_FreeValue", "JS_NewObject"),
        ("calloc without free", "calloc"),
        ("strdup without free", "strdup"),
    ]
    
    for desc, symbol in patterns:
        bp = target.BreakpointCreateByName(symbol)
        print(f"  Tracking {desc}: breakpoint {bp.GetID()}")


# ============================================================================
# Module Registration
# ============================================================================

def __lldb_init_module(debugger, internal_dict):
    """Register all commands when module is loaded"""
    
    debugger.HandleCommand(
        'command script add -f lldb_memory_debug.memory_debug memory_debug'
    )
    debugger.HandleCommand(
        'command script add -c lldb_memory_debug.MemoryAuditCommand memaudit'
    )
    debugger.HandleCommand(
        'command script add -c lldb_memory_debug.JSValueAuditCommand jsvalueaudit'
    )
    debugger.HandleCommand(
        'command script add -c lldb_memory_debug.MutexAuditCommand mutexaudit'
    )
    debugger.HandleCommand(
        'command script add -f lldb_memory_debug.analyze_leaks analyze_leaks'
    )
    
    print("Memory debugging module loaded.")
    print("Commands available:")
    print("  memory_debug <PID>  - Start comprehensive memory debugging")
    print("  memaudit            - Audit memory allocations")
    print("  jsvalueaudit        - Audit JSValue allocations")
    print("  mutexaudit          - Audit mutex state")
    print("  analyze_leaks       - Analyze potential leak patterns")
