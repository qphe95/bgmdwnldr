# LLDB Debug Scripts Guide

This guide documents all the LLDB debugging scripts in the `lldb/` folder for debugging the QuickJS shape corruption issue in the bgmdwnldr app.

## Overview

These scripts are designed to debug a specific crash where the `JSObject->shape` pointer becomes corrupted (typically showing values like `0xc0000000` or `0xc0000008`, which are actually QuickJS tagged value constants, not valid pointers).

---

## Quick Start

For most debugging scenarios, use one of these entry points:

```bash
# Comprehensive automated debugging (recommended)
./lldb/run_comprehensive_debug.sh

# Or simple attach-and-debug
./lldb/debug_attach.sh

# Or the main LLDB launcher
./lldb/run_lldb_debug.sh
```

---

## Shell Scripts (Entry Points)

### Primary Scripts

| Script | Purpose | When to Use |
|--------|---------|-------------|
| `run_comprehensive_debug.sh` | Master orchestration script with pre-flight checks, environment setup, and comprehensive debugging | **Recommended first choice** - Full debugging session with logging |
| `debug_attach.sh` | Simple attach-and-debug for shape corruption | Quick debugging of running app |
| `run_lldb_debug.sh` | Main LLDB debugging session launcher | When you want interactive debugging |
| `debug_shape_crash.sh` | Automated shape corruption debugger | Automated workflow with auto-detection |

### Specialized Scripts

| Script | Purpose |
|--------|---------|
| `run_full_debug.sh` | Full debugging for the specific `0xc0000008` crash with conditional breakpoints |
| `run_register_debug.sh` | Debug the `x28=0xc0000000` crash specifically |
| `run_lldb_crash_debug.sh` | Catch the `0xc0000008` crash with LLDB and Python scripting |
| `run_lldb_batch.sh` | Batch-mode LLDB driver with Python scripting (non-interactive) |
| `run_comprehensive_debug.sh` | MASTER orchestration script with 5 phases: pre-flight, setup, lldb-server, script prep, and launch |

### Alternative/Experimental Scripts

| Script | Purpose |
|--------|---------|
| `debug_lldb.sh` | Basic LLDB setup with remote-android platform |
| `debug_session.sh` | Comprehensive debugging session with cleanup |
| `debug_root_cause.sh` | Root cause analysis using LLDB watchpoints |
| `debug_shape_lldb.sh` | Advanced LLDB with Python scripting |
| `debug_shape_watch.sh` | Watchpoint-based shape corruption detection |
| `debug_with_lldb.sh` | Automated LLDB debugging workflow |
| `lldb_attach.sh` | Attach and catch crash using expect script |
| `lldb_attach_debug.sh` | Attach LLDB to already running app |
| `lldb_batch.sh` | Simplified batch mode debugging |
| `lldb_debug.sh` | LLDB debugging for QuickJS shape corruption |
| `lldb_debug_crash.sh` | Debug script for bgmdwldr crash |
| `lldb_simple.sh` | Simple debugging with file-based breakpoints (uses expect) |
| `lldb_stripped_debug.sh` | Debugging for stripped binaries using address-based breakpoints |
| `simple_debug.sh` | Simple crash debugging using logcat and tombstones only |

---

## Python Scripts (LLDB Modules)

### Primary/Master Scripts

#### `lldb_master_debug.py` ⭐ PRIMARY
The main debugging script to load. Provides comprehensive shape corruption detection.

**Commands:**
- `qjs-debug-start` - Start comprehensive debugging
- `qjs-check-obj <addr>` - Inspect JSObject structure
- `qjs-where-shape` - Track shape origins
- `qjs-analyze` - Full state analysis

**Usage:**
```bash
lldb -o "command script import lldb_master_debug.py" -o "qjs-debug-start"
```

#### `lldb_comprehensive_debug.py`
Multi-layered debugging system tracking object lifecycle, shape allocation, GC activity, and memory corruption patterns.

**Commands:**
- `comprehensive-debug-start` - Start full debugging session
- `track-object <addr>` - Track specific object
- `dump-object-history` - Show tracked object histories
- `analyze-corruption` - Deep analysis of corruption state
- `memory-scan <addr> <size>` - Scan memory region

### Shape Corruption Specific Scripts

| Script | Purpose | Key Features |
|--------|---------|--------------|
| `lldb_shape_debug.py` | Shape corruption detection with watchpoints | `shape-debug-start`, `bt-shape` commands |
| `lldb_advanced_shape.py` | Advanced debugging with conditional breakpoints | `start-shape-debug`, `check-obj`, `watch-shape`, `find-bad-shape` |
| `lldb_shape_investigator.py` | Shape allocation and corruption investigation | `shape-investigate-start`, `shape-status`, `shape-check` |
| `lldb_deep_shape_debug.py` | Deep investigation tracing object creation | `deep-debug-start`, `deep-check-obj` |
| `lldb_watchpoint_debug.py` | Catch corruption using hardware watchpoints | `wp-start`, `wp-list`, `wp-check` |
| `lldb_root_cause.py` | Root cause analysis with watchpoints | `rc-start`, `rc-status`, `rc-history`, `rc-check` |

### Crash Analysis Scripts

| Script | Purpose |
|--------|---------|
| `lldb_crash_debug.py` | Debug the `0xc0000008` crash in `JS_DefineProperty` - traces x28 register corruption |
| `lldb_crash_catch.py` | Catch and analyze QuickJS shape corruption crash with SIGSEGV handling |
| `lldb_register_debug.py` | Debug `x28=0xc0000000` crash pattern - decodes JSValue tags |
| `lldb_trace_x28.py` | Trace origin of `0xc0000000` value in x28 register |

### Memory Debugging Scripts

| Script | Purpose |
|--------|---------|
| `lldb_memory_debug.py` | Advanced memory debugging - detects buffer overflows, leaks, use-after-free, double-free, race conditions |
| `lldb_memory_scan.py` | Memory scanning utilities - scan for JSObject patterns, find shape references, dump regions |

### Driver Scripts (Standalone)

| Script | Purpose |
|--------|---------|
| `lldb_driver.py` | Comprehensive LLDB driver that automates entire session including triggering the crash |
| `lldb_driver_fixed.py` | Fixed version that attaches without causing ANR (uses async mode) |
| `lldb_auto_debug.py` | Automated debugging with `quickjs_debug <PID>` command |
| `lldb_investigate.py` | Advanced investigation with hardware watchpoints and memory inspection |

---

## Text Files (Command Scripts)

These are LLDB command scripts that can be sourced with `lldb -s <file>`:

| File | Purpose |
|------|---------|
| `lldb_comprehensive_cmds.txt` | Comprehensive debug script template with app startup and PID detection |
| `lldb_batch_debug.txt` | Batch debugging commands with Python breakpoint handlers |
| `lldb_advanced_commands.txt` | Advanced LLDB commands for memory allocation tracking, threading functions |
| `lldb_commands.txt` | Basic LLDB commands template |
| `lldb_commands_attach.txt` | Commands to run after attaching (watchpoint setup guide) |
| `lldb_manual_investigation.txt` | Manual investigation guide with step-by-step commands |
| `lldb_shape_commands.txt` | Shape corruption specific commands with condition checking |

---

## Common Debugging Workflow

### 1. Automated Debugging (Recommended)
```bash
# Run the comprehensive debugger
./lldb/run_comprehensive_debug.sh

# Or for quick attach
./lldb/debug_attach.sh
```

### 2. Manual LLDB Session
```bash
# Terminal 1: Start app
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# Terminal 2: Get PID and start lldb-server
PID=$(adb shell pidof com.bgmdwldr.vulkan)
adb forward tcp:5039 tcp:5039
adb shell /data/local/tmp/lldb-server platform --listen '*:5039' --server &

# Terminal 3: Start LLDB
lldb -o "command script import lldb_master_debug.py" -o "qjs-debug-start"

# In LLDB:
(lldb) platform select remote-android
(lldb) platform connect connect://localhost:5039
(lldb) attach -p $PID
(lldb) continue
```

### 3. Using Python Scripts Directly
```bash
# Attach to running process
lldb -p $(adb shell pidof com.bgmdwldr.vulkan)

# In LLDB:
(lldb) command script import lldb_master_debug.py
(lldb) qjs-debug-start
(lldb) continue
```

---

## Key Breakpoints and Functions

The scripts set breakpoints on these critical functions:

| Function | Purpose |
|----------|---------|
| `init_browser_stubs` | Browser environment initialization (where corruption often starts) |
| `JS_SetPropertyStr` | Property setting (where crash often occurs) |
| `JS_SetPropertyInternal` | Internal property setting |
| `find_own_property` | **Crash location** - finds properties on objects |
| `js_new_shape_nohash` | Shape allocation tracking |
| `js_free_shape0` | Shape freeing tracking |
| `JS_NewObjectFromShape` | Object creation tracking |
| `JS_DefineProperty` | Property definition (0xc0000008 crash site) |
| `JS_NewContext` / `JS_NewContextRaw` | Context creation |

---

## Corruption Detection

The scripts detect corruption by checking:

1. **Invalid shape pointers**: Values `< 0x1000` or `> 0x7f0000000000`
2. **Tagged value patterns**: `0xc0000000`, `0xc0000008` (JSValue tags)
3. **NULL shapes**: `0x0` or `0xFFFFFFFFFFFFFFFF`
4. **Memory changes**: Hardware watchpoints on shape fields

---

## Tips

- **Hardware watchpoint limit**: Most ARM64 devices support only 4 hardware watchpoints
- **Async mode**: Use `debugger.SetAsync(True)` to prevent ANR when attaching
- **SIGSEGV handling**: The scripts catch SIGSEGV to analyze crashes before they kill the app
- **Logcat**: Always check `adb logcat` for debug output from the app

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| lldb-server not found | Push from NDK: `adb push $NDK/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/*/lib/linux/arm64/lldb-server /data/local/tmp/` |
| ANR when attaching | Use `lldb_driver_fixed.py` or set async mode |
| Breakpoints not resolving | Wait for library to load, use pending breakpoints (`-P true`) |
| Watchpoints fail | Hardware limit reached (4 max), remove some watchpoints |
