# Agent Instructions for bgmdwnldr

## Testing Procedures

### Prerequisites
- Android device connected via ADB
- App installed (via `./rebuild.sh`)

### Proper App Launch Sequence

**IMPORTANT:** Always verify the correct app is running before testing:

```bash
# 1. Kill any running instances
adb shell am force-stop com.bgmdwldr.vulkan
adb shell am force-stop com.oneplus.backuprestore  # Clone Phone often interferes

# 2. Clear logs
adb logcat -c

# 3. Start the app explicitly
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# 4. Verify it's running
adb shell pidof com.bgmdwldr.vulkan

# 5. Check focused activity (should show bgmdwldr.vulkan)
adb shell dumpsys activity activities | grep mFocusedApp
```

### Entering Test URL

```bash
# Tap input field (center of text box)
adb shell input tap 540 600

# Clear existing text
adb shell input keyevent --longpress 67 67 67 67 67 67 67 67

# Enter URL
adb shell input text "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

# Submit
adb shell input keyevent 66

# Wait for processing (10-15 seconds)
sleep 12
```

### Viewing Results

```bash
# Get app PID
APP_PID=$(adb shell pidof com.bgmdwldr.vulkan)

# View js_quickjs logs
adb logcat -d --pid=$APP_PID | grep -E "js_quickjs:|HtmlExtract:|Executed|Captured"
```

## LLDB Debugging Tools

The repository contains extensive LLDB debugging scripts for diagnosing native crashes, particularly the QuickJS shape corruption issue. These scripts are organized into categories:

### Quick Start (Recommended Scripts)

These are the main entry points for debugging:

| Script | Purpose | When to Use |
|--------|---------|-------------|
| `./run_lldb_debug.sh` | Primary debugging script with Python automation | First choice for shape corruption debugging |
| `./debug_session.sh` | Full LLDB session with remote platform connection | When app crashes too fast for normal attach |
| `./debug_shape_lldb.sh` | Advanced shape corruption debugging | For detailed shape tracking |
| `./lldb_debug.sh` | Basic LLDB with breakpoint sequence | Simple step-through debugging |

### Python Debugging Scripts

These provide advanced debugging capabilities when loaded into LLDB:

#### Primary Script
- **`lldb_master_debug.py`** - **Main comprehensive debugger**
  - Commands: `qjs-debug-start`, `qjs-check-obj <addr>`, `qjs-where-shape`, `qjs-analyze`
  - Sets breakpoints at: `init_browser_stubs`, `JS_SetPropertyStr`, `find_own_property`
  - Automatically detects shape corruption and stops
  - Usage: `command script import lldb_master_debug.py && qjs-debug-start`

#### Specialized Scripts
- **`lldb_advanced_shape.py`** - Shape-specific debugging
  - Commands: `start-shape-debug`, `check-obj <addr>`, `watch-shape <addr>`, `find-bad-shape`
  - Features watchpoint-based shape change detection
  
- **`lldb_memory_scan.py`** - Memory analysis utilities
  - Commands: `scan-objects`, `find-shape-refs <addr>`, `dump-region <addr>`, `analyze-crash`
  - Scans heap for JSObject structures

- **`lldb_shape_debug.py`** - Basic shape debugging
  - Command: `shape-debug-start`
  - Tracks shape creation/freeing via `js_new_shape_nohash`/`js_free_shape0`

- **`lldb_advanced_debug.py`** - General debugging utilities
  - Commands: `setup_debug <PID>`, `jsvalue <addr>`, `htmlnode <addr>`, `memscan`, `memleak_check`, `threadcheck`
  - Tracks JS calls, memory allocations, mutex locks

- **`lldb_memory_debug.py`** - Memory error detection
  - Commands: `memory_debug <PID>`, `memaudit`, `jsvalueaudit`, `mutexaudit`
  - Detects buffer overflows, double-frees, use-after-free

- **`lldb_investigate.py`** - Investigation utilities
  - Command: `setup_debug`
  - Focus on `js_shape_hash_unlink` tracking

- **`lldb_auto_debug.py`** - Automated debugging
  - Command: `quickjs_debug <PID>`
  - Automated attach and breakpoint setup

### Shell Scripts Reference

| Script | Description |
|--------|-------------|
| `run_lldb_debug.sh` | Main entry point - starts app, attaches LLDB, loads `lldb_master_debug.py` |
| `debug_session.sh` | Uses `am start -D` (debugger wait) + remote platform for early attach |
| `debug_shape_lldb.sh` | Loads `lldb_advanced_shape.py` for shape-focused debugging |
| `lldb_debug.sh` | Sequential breakpoints through init functions |
| `debug_lldb.sh` | Simple attach with basic breakpoint setup |
| `lldb_debug_crash.sh` | Batch mode debugging for post-crash analysis |

### Command Files (.txt)

These are LLDB command scripts for use with `lldb -s <file>`:

| File | Purpose |
|------|---------|
| `lldb_commands.txt` | Basic connection setup, platform selection |
| `lldb_commands_attach.txt` | Commands to run after attaching (breakpoints at key functions) |
| `lldb_advanced_commands.txt` | Comprehensive breakpoint setup for advanced debugging |
| `lldb_shape_commands.txt` | Shape-focused breakpoints with Python conditionals |
| `lldb_batch_debug.txt` | Batch mode automation using `lldb_master_debug.py` |

### Debugging Workflow for Shape Corruption

The shape corruption crash typically occurs in `init_browser_stubs` when `JS_SetPropertyStr` is called with an invalid object (JS_UNDEFINED tag 0x1 instead of a valid JSObject pointer).

**Recommended approach:**

```bash
# Method 1: Use the main debug script (recommended)
./run_lldb_debug.sh

# Method 2: If app crashes too fast, use debugger-wait mode
./debug_session.sh

# Method 3: Manual LLDB with master debug script
./debug_lldb.sh  # In one terminal
lldb -p $(adb shell pidof com.bgmdwldr.vulkan)
(lldb) command script import lldb_master_debug.py
(lldb) qjs-debug-start
(lldb) continue
```

### Key LLDB Commands Reference

When stopped at a breakpoint:

```bash
# Inspect JSObject at address
(lldb) qjs-check-obj 0x<address>

# Analyze current state
(lldb) qjs-analyze

# Show backtrace
(lldb) bt

# Show registers
(lldb) register read

# Read memory as JSObject
(lldb) memory read 0x<addr> -s 8 -c 4

# Set watchpoint on shape field (offset 8)
(lldb) watchpoint set expression -w write -- (0x<obj_addr> + 8)

# Continue execution
(lldb) continue
```

### Prerequisites for LLDB

```bash
# Ensure lldb-server is on the device
adb shell ls -la /data/local/tmp/lldb-server

# If missing, copy from NDK
adb push $NDK_ROOT/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/*/lib/linux/arm64/lldb-server /data/local/tmp/
adb shell chmod +x /data/local/tmp/lldb-server

# Forward LLDB port
adb forward tcp:5039 tcp:5039
```
