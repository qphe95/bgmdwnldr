#!/usr/bin/env python3
"""
Advanced LLDB Debugging Script for bgmdwnldr

This script uses advanced LLDB features to debug issues:
- Scripted breakpoints with custom actions
- Memory inspection and watchpoints
- Stack tracing and frame analysis
- Custom commands for inspecting data structures

Usage:
1. Start app: ./debug_lldb.sh
2. In another terminal: lldb -s lldb_advanced_commands.txt
"""

import lldb
import re

# ============================================================================
# Custom LLDB Commands for Debugging
# ============================================================================

class JSValueInspectorCommand:
    """Command to inspect QuickJS JSValue structures"""
    
    def __init__(self, debugger, session_dict):
        self.debugger = debugger
        self.session_dict = session_dict
    
    def get_short_help(self):
        return "Inspect a QuickJS JSValue: jsvalue <address>"
    
    def get_long_help(self):
        return """\
Inspect a QuickJS JSValue structure at the given memory address.
Usage: jsvalue <address>
Example: jsvalue 0x7ffffff12340
"""
    
    def __call__(self, debugger, command, result, internal_dict):
        args = command.split()
        if len(args) < 1:
            result.SetError("Usage: jsvalue <address>")
            return
        
        addr = args[0]
        target = debugger.GetSelectedTarget()
        
        # Read the JSValue struct (typically 16 bytes: 8 for tag, 8 for value)
        error = lldb.SBError()
        process = target.GetProcess()
        
        # Try to interpret as JSValue
        val_addr = int(addr, 0)
        tag = process.ReadUnsignedFromMemory(val_addr, 8, error)
        if error.Success():
            value = process.ReadUnsignedFromMemory(val_addr + 8, 8, error)
            result.AppendMessage(f"JSValue at {addr}:")
            result.AppendMessage(f"  tag:  0x{tag:016x}")
            result.AppendMessage(f"  u.ptr: 0x{value:016x}")
            
            # Interpret tag
            if tag & 0xFF == 0xFF:
                result.AppendMessage(f"  type: INT ({value})")
            elif tag == 0:
                result.AppendMessage(f"  type: OBJECT/POINTER")
            elif tag & 0x7 == 0x7:
                result.AppendMessage(f"  type: PRIMITIVE (bool/null/undefined)")
            else:
                result.AppendMessage(f"  type: UNKNOWN")
        else:
            result.SetError(f"Failed to read memory at {addr}: {error.GetCString()}")


class HTMLNodeInspectorCommand:
    """Command to inspect HTML DOM nodes"""
    
    def get_short_help(self):
        return "Inspect HTML node: htmlnode <address>"
    
    def get_long_help(self):
        return """\
Inspect an HtmlNode structure at the given memory address.
Usage: htmlnode <address>
"""
    
    def __call__(self, debugger, command, result, internal_dict):
        args = command.split()
        if len(args) < 1:
            result.SetError("Usage: htmlnode <address>")
            return
        
        addr = int(args[0], 0)
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        error = lldb.SBError()
        
        # Read HtmlNode fields (assuming standard layout)
        # struct HtmlNode {
        #     HtmlNodeType type;      // 4 bytes
        #     char tag_name[64];      // 64 bytes
        #     HtmlAttribute *attributes; // 8 bytes
        #     char *text_content;     // 8 bytes
        #     ...
        # }
        
        node_type = process.ReadUnsignedFromMemory(addr, 4, error)
        if not error.Success():
            result.SetError(f"Failed to read node type: {error.GetCString()}")
            return
        
        # Read tag name (64 bytes buffer)
        tag_data = process.ReadMemory(addr + 4, 64, error)
        tag_name = tag_data.decode('utf-8', errors='replace').split('\x00')[0] if tag_data else ""
        
        result.AppendMessage(f"HtmlNode at 0x{addr:x}:")
        result.AppendMessage(f"  type: {node_type} ({'ELEMENT' if node_type == 1 else 'TEXT' if node_type == 3 else 'OTHER'})")
        result.AppendMessage(f"  tag_name: '{tag_name}'")
        
        # Read pointer fields
        attrs_ptr = process.ReadUnsignedFromMemory(addr + 72, 8, error)
        text_ptr = process.ReadUnsignedFromMemory(addr + 80, 8, error)
        parent_ptr = process.ReadUnsignedFromMemory(addr + 88, 8, error)
        first_child_ptr = process.ReadUnsignedFromMemory(addr + 96, 8, error)
        
        result.AppendMessage(f"  attributes: 0x{attrs_ptr:x}")
        result.AppendMessage(f"  text_content: 0x{text_ptr:x}")
        result.AppendMessage(f"  parent: 0x{parent_ptr:x}")
        result.AppendMessage(f"  first_child: 0x{first_child_ptr:x}")


class MemoryRegionScannerCommand:
    """Command to scan memory regions for patterns"""
    
    def get_short_help(self):
        return "Scan memory for pattern: memscan <start> <size> <pattern>"
    
    def get_long_help(self):
        return """\
Scan a memory region for a byte pattern.
Usage: memscan <start_addr> <size> <hex_pattern>
Example: memscan 0x7fff0000 4096 "48 65 6c 6c 6f"
"""
    
    def __call__(self, debugger, command, result, internal_dict):
        args = command.split()
        if len(args) < 3:
            result.SetError("Usage: memscan <start_addr> <size> <hex_pattern>")
            return
        
        start_addr = int(args[0], 0)
        size = int(args[1], 0)
        pattern_hex = ' '.join(args[2:])
        
        # Parse hex pattern
        pattern_bytes = bytes.fromhex(pattern_hex.replace(' ', ''))
        
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        error = lldb.SBError()
        
        # Read memory
        data = process.ReadMemory(start_addr, size, error)
        if not error.Success():
            result.SetError(f"Failed to read memory: {error.GetCString()}")
            return
        
        # Search for pattern
        matches = []
        for i in range(len(data) - len(pattern_bytes) + 1):
            if data[i:i+len(pattern_bytes)] == pattern_bytes:
                matches.append(start_addr + i)
        
        result.AppendMessage(f"Found {len(matches)} matches for pattern '{pattern_hex}':")
        for match in matches[:10]:  # Limit to first 10
            result.AppendMessage(f"  0x{match:x}")
        if len(matches) > 10:
            result.AppendMessage(f"  ... and {len(matches) - 10} more")


# ============================================================================
# Scripted Breakpoints
# ============================================================================

def on_js_call_breakpoint(frame, bp_loc, dict):
    """
    Callback for JS_Call breakpoint - inspects JavaScript calls
    """
    thread = frame.GetThread()
    process = thread.GetProcess()
    target = process.GetTarget()
    
    # Get arguments (ctx, func_obj, this_obj, argc, argv)
    # On ARM64: x0=ctx, x1=func_obj, x2=this_obj, x3=argc, x4=argv
    registers = frame.FindValue("x1", lldb.eValueTypeRegister)
    func_obj_addr = registers.GetValueAsUnsigned()
    
    print(f"[LLDB] JS_Call invoked")
    print(f"[LLDB]   func_obj: 0x{func_obj_addr:x}")
    
    # Get argc
    argc_reg = frame.FindValue("x3", lldb.eValueTypeRegister)
    argc = argc_reg.GetValueAsUnsigned()
    print(f"[LLDB]   argc: {argc}")
    
    return False  # Don't stop, just log


def on_malloc_breakpoint(frame, bp_loc, dict):
    """
    Callback for malloc breakpoint - tracks allocations
    """
    thread = frame.GetThread()
    
    # Get size argument (x0 on ARM64)
    size_reg = frame.FindValue("x0", lldb.eValueTypeRegister)
    size = size_reg.GetValueAsUnsigned()
    
    print(f"[LLDB] malloc({size}) called from:")
    
    # Print backtrace
    for i, f in enumerate(thread.frames[:5]):
        func_name = f.GetFunctionName()
        addr = f.GetPC()
        print(f"[LLDB]   #{i} 0x{addr:x} {func_name}")
    
    return False


def on_mutex_lock_breakpoint(frame, bp_loc, dict):
    """
    Callback for pthread_mutex_lock - detects potential deadlocks
    """
    thread = frame.GetThread()
    process = thread.GetProcess()
    target = process.GetTarget()
    
    # Get mutex address
    mutex_reg = frame.FindValue("x0", lldb.eValueTypeRegister)
    mutex_addr = mutex_reg.GetValueAsUnsigned()
    
    thread_id = thread.GetThreadID()
    
    # Store in dict to track which thread holds which mutex
    if 'mutex_owners' not in dict:
        dict['mutex_owners'] = {}
    
    dict['mutex_owners'][mutex_addr] = thread_id
    
    print(f"[LLDB] Thread {thread_id} locking mutex 0x{mutex_addr:x}")
    
    return False


# ============================================================================
# Main Debugging Session Setup
# ============================================================================

def setup_debugging_session(debugger, command, result, internal_dict):
    """
    Main command to set up comprehensive debugging
    """
    args = command.split()
    if len(args) < 1:
        print("Usage: setup_debug <PID>")
        return
    
    pid = int(args[0])
    target = debugger.GetSelectedTarget()
    
    print("=== Advanced LLDB Debugging Session ===")
    print(f"Attaching to PID {pid}...")
    
    # Attach to process
    error = lldb.SBError()
    process = target.AttachToProcessWithID(lldb.SBListener(), pid, error)
    
    if not error.Success():
        print(f"Failed to attach: {error.GetCString()}")
        return
    
    print(f"Attached successfully!")
    
    # Set up scripted breakpoints
    print("\n=== Setting up scripted breakpoints ===")
    
    # 1. Breakpoint on JS_Call to track JavaScript execution
    bp_js_call = target.BreakpointCreateByName("JS_Call")
    bp_js_call.SetScriptCallbackFunction("lldb_advanced_debug.on_js_call_breakpoint")
    print(f"[+] JS_Call breakpoint: {bp_js_call.GetID()}")
    
    # 2. Breakpoint on malloc to track memory allocations
    bp_malloc = target.BreakpointCreateByName("malloc")
    bp_malloc.SetScriptCallbackFunction("lldb_advanced_debug.on_malloc_breakpoint")
    print(f"[+] malloc breakpoint: {bp_malloc.GetID()}")
    
    # 3. Breakpoint on pthread_mutex_lock to detect deadlocks
    bp_mutex = target.BreakpointCreateByName("pthread_mutex_lock")
    bp_mutex.SetScriptCallbackFunction("lldb_advanced_debug.on_mutex_lock_breakpoint")
    print(f"[+] pthread_mutex_lock breakpoint: {bp_mutex.GetID()}")
    
    # 4. Set breakpoint on js_quickjs_exec_scripts to catch JS execution
    bp_exec = target.BreakpointCreateByName("js_quickjs_exec_scripts")
    print(f"[+] js_quickjs_exec_scripts breakpoint: {bp_exec.GetID()}")
    
    # 5. Set breakpoint on url_analyze to catch URL analysis
    bp_url = target.BreakpointCreateByName("url_analyze")
    print(f"[+] url_analyze breakpoint: {bp_url.GetID()}")
    
    print("\n=== Setting up watchpoints for key globals ===")
    
    # Watch g_captured_url_count for changes
    # This requires finding the symbol address first
    g_url_count = target.FindSymbols("g_captured_url_count")
    if g_url_count.GetSize() > 0:
        sym = g_url_count.GetSymbolAtIndex(0)
        addr = sym.GetStartAddress()
        print(f"[+] g_captured_url_count at {addr}")
        # Note: Can't set watchpoint until process is running
    
    print("\n=== Debugging session ready ===")
    print("Use 'continue' to start execution")
    print("Use 'jsvalue <addr>' to inspect JSValue structures")
    print("Use 'htmlnode <addr>' to inspect HTML DOM nodes")
    print("Use 'memscan <start> <size> <pattern>' to scan memory")


def find_memory_leaks(debugger, command, result, internal_dict):
    """
    Command to help find memory leaks by analyzing heap allocations
    """
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    
    print("=== Memory Leak Analysis ===")
    print("This command helps identify potential memory leaks.")
    print("Run this after executing operations that should free memory.\n")
    
    # Get heap info using process statistics
    for module in target.module_iter():
        for section in module.section_iter():
            name = section.GetName()
            if 'heap' in name.lower() or 'data' in name.lower():
                addr = section.GetLoadAddress(target)
                size = section.GetByteSize()
                print(f"Section {name}: 0x{addr:x} - 0x{addr+size:x} ({size} bytes)")


def check_thread_safety(debugger, command, result, internal_dict):
    """
    Command to check for thread safety issues
    """
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    
    print("=== Thread Safety Check ===")
    print(f"Number of threads: {process.GetNumThreads()}")
    
    for i in range(process.GetNumThreads()):
        thread = process.GetThreadAtIndex(i)
        thread_id = thread.GetThreadID()
        
        # Get current function
        frame = thread.GetSelectedFrame()
        func_name = frame.GetFunctionName()
        
        print(f"\nThread {thread_id}:")
        print(f"  Current function: {func_name}")
        print(f"  Stop reason: {thread.GetStopDescription(100)}")
        
        # Check if holding any locks
        # This requires tracking in breakpoint callbacks


# ============================================================================
# Module Registration
# ============================================================================

def __lldb_init_module(debugger, internal_dict):
    """Register all commands when module is loaded"""
    
    # Register commands
    debugger.HandleCommand(
        'command script add -f lldb_advanced_debug.setup_debugging_session setup_debug'
    )
    debugger.HandleCommand(
        'command script add -c lldb_advanced_debug.JSValueInspectorCommand jsvalue'
    )
    debugger.HandleCommand(
        'command script add -c lldb_advanced_debug.HTMLNodeInspectorCommand htmlnode'
    )
    debugger.HandleCommand(
        'command script add -c lldb_advanced_debug.MemoryRegionScannerCommand memscan'
    )
    debugger.HandleCommand(
        'command script add -f lldb_advanced_debug.find_memory_leaks memleak_check'
    )
    debugger.HandleCommand(
        'command script add -f lldb_advanced_debug.check_thread_safety threadcheck'
    )
    
    print("Advanced LLDB debugging module loaded.")
    print("Commands available:")
    print("  setup_debug <PID>    - Set up comprehensive debugging session")
    print("  jsvalue <addr>       - Inspect QuickJS JSValue")
    print("  htmlnode <addr>      - Inspect HTML DOM node")
    print("  memscan <start> <size> <pattern> - Scan memory for pattern")
    print("  memleak_check        - Analyze potential memory leaks")
    print("  threadcheck          - Check thread safety issues")
