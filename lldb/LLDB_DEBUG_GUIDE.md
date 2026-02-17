# QuickJS Shape Corruption - LLDB Advanced Debugging Guide

## Overview

This debugging setup uses LLDB's advanced Python scripting capabilities to:
1. **Track object creation** - Monitor when JSObjects are created and their shapes assigned
2. **Detect corruption** - Automatically detect when `JSObject->shape` becomes invalid
3. **Memory analysis** - Scan heap memory and analyze object structures
4. **Conditional breakpoints** - Stop only when specific corruption conditions are met

## Quick Start

### Method 1: Interactive Debugging (Recommended)

```bash
./run_lldb_debug.sh
```

This will:
1. Start the app on the emulator
2. Attach LLDB with Python debugging scripts loaded
3. Automatically set up breakpoints at key functions
4. Stop when shape corruption is detected

### Method 2: Manual LLDB Session

```bash
# Terminal 1: Start app
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# Get PID
PID=$(adb shell pidof com.bgmdwldr.vulkan)

# Terminal 2: Attach LLDB
lldb -p $PID

# In LLDB:
(lldb) command script import lldb_master_debug.py
(lldb) qjs-debug-start
(lldb) continue
```

## Available Commands

Once LLDB is running with the scripts loaded, use these commands:

### Main Commands

| Command | Description |
|---------|-------------|
| `qjs-debug-start` | Start comprehensive debugging with all breakpoints |
| `qjs-check-obj <addr>` | Inspect a JSObject structure at given address |
| `qjs-where-shape` | Track where the current object's shape came from |
| `qjs-analyze` | Full state analysis of current execution |

### Memory Scan Commands

| Command | Description |
|---------|-------------|
| `scan-objects` | Scan heap for JSObject structures |
| `find-shape-refs <addr>` | Find all pointers to a shape |
| `dump-region <start> [size]` | Dump memory region with structure interpretation |
| `analyze-crash` | Analyze current state for corruption |

### Standard LLDB Commands

| Command | Description |
|---------|-------------|
| `frame variable` | Show local variables |
| `register read` | Show all registers |
| `memory read <addr>` | Read memory at address |
| `bt` | Show backtrace |
| `continue` | Continue execution |
| `step` | Step one instruction |

## Debugging Workflow

### 1. Start Debugging

```bash
./run_lldb_debug.sh
```

The debugger will automatically stop when:
- `init_browser_stubs` is entered
- `JS_SetPropertyStr` detects an object with invalid shape
- `find_own_property` is about to crash (shape < 0x1000)

### 2. When Corruption is Detected

The debugger stops automatically. Use these commands:

```
(lldb) qjs-analyze          # Full state analysis
(lldb) bt                   # Show backtrace
(lldb) register read        # Show registers
(lldb) qjs-where-shape      # Track shape origin
```

### 3. Inspect Specific Objects

If you know an object address:

```
(lldb) qjs-check-obj 0xb4000078b03d5000
```

### 4. Find All Objects

```
(lldb) scan-objects
```

This scans memory regions for valid JSObject structures.

## How the Scripts Work

### `lldb_master_debug.py`

Main debugging script that sets up:

1. **Breakpoint at `init_browser_stubs`** - Entry point tracking
2. **Breakpoint at `JS_NewCFunction2`** - Track DOMException creation
3. **Breakpoint at `JS_SetConstructor`** - Monitor constructor setup
4. **Breakpoint at `JS_SetPropertyStr`** - Detect corruption before crash
5. **Breakpoint at `find_own_property`** - Catch crash before it happens
6. **Breakpoint at `add_property`** - Track property additions

Each breakpoint has a Python callback that:
- Inspects register values (ARM64: x0-x5)
- Reads memory to parse JSObject structures
- Checks if `shape` pointer is valid (> 0x1000)
- Stops execution if corruption is detected

### `lldb_memory_scan.py`

Advanced memory scanning utilities:
- `scan-objects`: Scans heap for valid JSObject patterns
- `find-shape-refs`: Finds all pointers to a given shape
- `dump-region`: Dumps memory with structure interpretation
- `analyze-crash`: Comprehensive crash analysis

## Key Debugging Scenarios

### Scenario 1: Catch the Exact Moment of Corruption

The debugger automatically stops when `JS_SetPropertyStr` is called with an object whose `shape` is invalid (< 0x1000). This catches the corruption before the crash.

### Scenario 2: Find What Corrupted the Shape

When corruption is detected:

```
(lldb) qjs-analyze          # Shows object history if tracked
(lldb) scan-objects         # Find nearby valid objects
(lldb) find-shape-refs <old_shape_addr>  # Find who references old shape
```

### Scenario 3: Watch a Specific Object

If you want to monitor a specific object:

```
(lldb) qjs-check-obj 0x<addr>   # Verify object is valid
(lldb) watchpoint set expression -w write -- (long*)0x<addr+8>  # Watch shape field
```

## Technical Details

### JSObject Structure (ARM64)

```
Offset 0:   uint16_t class_id
Offset 2:   uint8_t flags (bitfield)
Offset 4:   uint32_t weakref_count
Offset 8:   JSShape* shape    <-- This gets corrupted!
Offset 16:  JSProperty* prop
```

### ARM64 Register Conventions

- `x0`: First argument (ctx, psh, etc.)
- `x1`: Second argument (this_obj, func_obj, etc.)
- `x2`: Third argument (property name, proto, etc.)
- `lr`: Link register (return address)

### Corruption Detection

A shape pointer is considered invalid if:
- `< 0x1000` (likely NULL or small integer)
- `> 0x7f0000000000` (kernel space)

## Troubleshooting

### LLDB Can't Find Symbols

Make sure the app is built with debug symbols:

```bash
./rebuild.sh  # Should build with -g flag
```

### Python Scripts Not Loading

Check Python path:

```
(lldb) script print(sys.path)
```

### Breakpoints Not Hitting

Verify function names:

```
(lldb) image lookup -n init_browser_stubs
```

## Files

| File | Description |
|------|-------------|
| `run_lldb_debug.sh` | Main entry point - interactive debugging |
| `lldb_master_debug.py` | Main Python debugging script |
| `lldb_memory_scan.py` | Memory scanning utilities |
| `lldb_advanced_shape.py` | Alternative advanced debugger |
| `lldb_batch_debug.txt` | Batch mode debugging commands |

## Tips

1. **Run with fresh app**: Always kill the app before starting a new debug session
2. **Use `qjs-analyze`**: This gives the most comprehensive view when corruption is detected
3. **Check backtrace**: The call stack shows exactly which code path led to corruption
4. **Memory scan**: `scan-objects` can find valid objects even when some are corrupted
