#!/usr/bin/env python3
"""
LLDB memory scanning utilities for QuickJS debugging.
Can scan memory regions for JSObject patterns and detect corruption.
"""

import lldb
import struct

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_memory_scan.scan_objects scan-objects')
    debugger.HandleCommand('command script add -f lldb_memory_scan.find_shape_refs find-shape-refs')
    debugger.HandleCommand('command script add -f lldb_memory_scan.dump_region dump-region')
    debugger.HandleCommand('command script add -f lldb_memory_scan.analyze_crash analyze-crash')
    print("Memory scan commands loaded: scan-objects, find-shape-refs, dump-region, analyze-crash")

def scan_for_objects(process, start_addr, end_addr, target_class_id=None):
    """
    Scan a memory region for potential JSObject structures.
    Returns list of (address, class_id, shape_ptr) tuples.
    """
    found = []
    error = lldb.SBError()
    
    # Read the entire region
    size = end_addr - start_addr
    if size > 100 * 1024 * 1024:  # Limit to 100MB at a time
        size = 100 * 1024 * 1024
    
    data = process.ReadMemory(start_addr, size, error)
    if not error.Success():
        return found
    
    # Scan for object patterns
    # JSObject starts with class_id (uint16_t) and has shape at offset 8
    for offset in range(0, len(data) - 24, 8):  # Align to 8 bytes
        addr = start_addr + offset
        
        # Parse structure
        class_id = struct.unpack_from('<H', data, offset)[0]
        # weakref_count at offset 4
        weakref_count = struct.unpack_from('<I', data, offset + 4)[0]
        # shape at offset 8
        shape = struct.unpack_from('<Q', data, offset + 8)[0]
        # prop at offset 16
        prop = struct.unpack_from('<Q', data, offset + 16)[0]
        
        # Heuristic: valid class_id range for QuickJS is roughly 1-200
        # shape should be a valid pointer or NULL
        if 1 <= class_id <= 200:
            if shape == 0 or (shape > 0x1000 and shape < 0x7f0000000000):
                # Looks like a valid JSObject
                if target_class_id is None or class_id == target_class_id:
                    found.append((addr, class_id, shape, prop))
    
    return found

def scan_objects(debugger, command, result, internal_dict):
    """
    Command: scan-objects [start] [end] [class_id]
    Scan memory for JSObject structures.
    """
    args = command.split()
    
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    
    # Get memory regions
    regions = []
    region = lldb.SBMemoryRegionInfo()
    addr = 0
    
    while process.GetMemoryRegionInfo(addr, region):
        start = region.GetRegionBase()
        end = region.GetRegionEnd()
        
        if region.IsExecutable() and not region.IsWritable():
            # Skip code regions
            addr = end
            continue
        
        if end - start > 0:
            regions.append((start, end))
        
        addr = end
        if addr == 0:
            break
    
    print(f"Found {len(regions)} memory regions")
    
    # Scan each region
    target_class = None
    if args and len(args) >= 3:
        target_class = int(args[2])
    
    total_objects = 0
    for start, end in regions[:10]:  # Limit to first 10 regions
        if end - start > 10 * 1024 * 1024:  # Skip huge regions
            continue
        
        print(f"Scanning region 0x{start:x} - 0x{end:x}...")
        objects = scan_for_objects(process, start, end, target_class)
        
        if objects:
            print(f"  Found {len(objects)} potential objects")
            for addr, class_id, shape, prop in objects[:5]:  # Show first 5
                print(f"    0x{addr:x}: class={class_id}, shape=0x{shape:x}, prop=0x{prop:x}")
                
                # Check if shape is valid
                if shape > 0 and shape < 0x1000:
                    print(f"      *** INVALID SHAPE ***")
        
        total_objects += len(objects)
    
    print(f"\nTotal potential objects found: {total_objects}")

def find_shape_refs(debugger, command, result, internal_dict):
    """
    Command: find-shape-refs <shape_address>
    Find all pointers to a given shape (likely JSObject->shape references).
    """
    if not command:
        print("Usage: find-shape-refs <shape_address>")
        return
    
    try:
        shape_addr = int(command, 16) if command.startswith('0x') else int(command)
    except:
        print(f"Invalid address: {command}")
        return
    
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    
    print(f"Scanning for references to shape 0x{shape_addr:x}...")
    print("(Looking for pointers at offset 8 in JSObject structures)")
    
    # Pack the address as bytes (little-endian)
    shape_bytes = struct.pack('<Q', shape_addr)
    
    # Scan memory regions
    found_refs = []
    region = lldb.SBMemoryRegionInfo()
    addr = 0
    
    while process.GetMemoryRegionInfo(addr, region):
        start = region.GetRegionBase()
        end = region.GetRegionEnd()
        
        if not region.IsReadable() or region.IsExecutable():
            addr = end
            continue
        
        # Read region in chunks
        chunk_size = 1024 * 1024  # 1MB at a time
        for chunk_start in range(start, end, chunk_size):
            actual_size = min(chunk_size, end - chunk_start)
            error = lldb.SBError()
            data = process.ReadMemory(chunk_start, actual_size, error)
            
            if not error.Success():
                continue
            
            # Search for the pattern
            offset = 0
            while True:
                idx = data.find(shape_bytes, offset)
                if idx == -1:
                    break
                
                ref_addr = chunk_start + idx
                # Check if this looks like a JSObject->shape (at offset 8)
                # The object would start 8 bytes before
                obj_addr = ref_addr - 8
                if obj_addr >= start:
                    found_refs.append(obj_addr)
                
                offset = idx + 1
        
        addr = end
        if addr == 0:
            break
    
    print(f"Found {len(found_refs)} potential references:")
    for ref in found_refs[:20]:  # Show first 20
        print(f"  JSObject at 0x{ref:x}")

def dump_region(debugger, command, result, internal_dict):
    """
    Command: dump-region <start> [size]
    Dump memory region with structure interpretation.
    """
    args = command.split()
    if not args:
        print("Usage: dump-region <start_address> [size_in_bytes]")
        return
    
    try:
        start = int(args[0], 16) if args[0].startswith('0x') else int(args[0])
        size = int(args[1]) if len(args) > 1 else 256
    except:
        print(f"Invalid arguments: {command}")
        return
    
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    error = lldb.SBError()
    
    data = process.ReadMemory(start, size, error)
    if not error.Success():
        print(f"Error reading memory: {error.GetCString()}")
        return
    
    print(f"Memory dump at 0x{start:x} ({len(data)} bytes):")
    print("Offset   | Hex Data                                 | ASCII")
    print("-" * 70)
    
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_part = ' '.join(f'{b:02x}' for b in chunk)
        hex_part = hex_part.ljust(41)
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"{i:08x} | {hex_part} | {ascii_part}")
    
    # Try to interpret as JSObject
    if len(data) >= 24:
        print("\nInterpreted as JSObject:")
        class_id = struct.unpack_from('<H', data, 0)[0]
        weakref_count = struct.unpack_from('<I', data, 4)[0]
        shape = struct.unpack_from('<Q', data, 8)[0]
        prop = struct.unpack_from('<Q', data, 16)[0]
        
        print(f"  class_id: {class_id}")
        print(f"  weakref_count: {weakref_count}")
        print(f"  shape: 0x{shape:x}")
        print(f"  prop: 0x{prop:x}")

def analyze_crash(debugger, command, result, internal_dict):
    """
    Command: analyze-crash
    Analyze the current state for shape corruption.
    """
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    
    print("=" * 60)
    print("CRASH ANALYSIS - Shape Corruption Debug")
    print("=" * 60)
    
    # Show current function
    func_name = frame.GetFunctionName()
    print(f"\nCurrent function: {func_name}")
    print(f"PC: {frame.GetPCAddress()}")
    
    # Get registers
    print("\nRegister state:")
    for reg_name in ['x0', 'x1', 'x2', 'x3', 'x4', 'lr', 'sp']:
        reg = frame.FindRegister(reg_name)
        print(f"  {reg_name}: {reg.GetValue()}")
    
    # If we're in find_own_property, analyze the object
    if func_name and "find_own_property" in func_name:
        print("\n--- Analyzing find_own_property crash ---")
        
        psh = frame.FindRegister("x0")
        p_val = int(psh.GetValue(), 16)
        
        print(f"psh (JSObject**) = 0x{p_val:x}")
        
        if p_val > 0x1000:
            error = lldb.SBError()
            obj_ptr = process.ReadPointerFromMemory(p_val, error)
            
            if error.Success():
                print(f"JSObject* = 0x{obj_ptr:x}")
                
                # Read and dump the object
                print("\nObject memory dump:")
                dump_region(debugger, f"0x{obj_ptr:x} 64", result, internal_dict)
                
                # Read shape
                shape_ptr = process.ReadPointerFromMemory(obj_ptr + 8, error)
                print(f"\nShape pointer: 0x{shape_ptr:x}")
                
                if shape_ptr > 0x1000:
                    print("\nShape memory dump:")
                    dump_region(debugger, f"0x{shape_ptr:x} 64", result, internal_dict)
                else:
                    print("Shape is NULL or invalid!")
                    
                    # Try to find what was at this address before
                    print("\nSearching for references to this object...")
                    find_shape_refs(debugger, f"0x{obj_ptr:x}", result, internal_dict)
    
    # Show backtrace
    print("\nBacktrace:")
    for i, f in enumerate(thread.frames[:10]):
        print(f"  #{i}: {f.GetFunctionName()} @ {f.GetPCAddress()}")

if __name__ == "__main__":
    print("Load with: command script import lldb_memory_scan.py")
