# LLDB Debug Scripts Refactoring - Executive Summary

## Problem Statement

The `lldb/` folder contains **49 files** with severe code duplication:
- **22 shell scripts** that all do the same ADB/lldb-server setup
- **24 Python scripts** with duplicated JSObject reading, corruption detection, and breakpoint handling  
- **7 text command files** with overlapping functionality
- Estimated **~4,330 lines of duplicated code**

## Complete File Inventory

### Shell Scripts (22 files, ~2,200 lines)

| File | Lines | Purpose | Unique Features |
|------|-------|---------|-----------------|
| `run_comprehensive_debug.sh` | 120 | Master orchestration | 5-phase setup, logging, cleanup traps |
| `run_full_debug.sh` | 148 | 0xc0000008 crash debug | Conditional breakpoints on x28 |
| `run_lldb_debug.sh` | 99 | Main LLDB launcher | Interactive mode, command helper |
| `run_register_debug.sh` | 56 | x28 register tracking | Debugger-wait mode, JDWP forwarding |
| `run_lldb_batch.sh` | 200 | Batch mode with Python | Inline Python script generation |
| `run_lldb_crash_debug.sh` | 88 | Catch crash with LLDB | SIGSEGV handling |
| `debug_attach.sh` | 51 | Simple attach-and-debug | Auto-start app |
| `debug_shape_crash.sh` | 125 | Automated shape debugger | Python subprocess automation |
| `debug_lldb.sh` | 45 | Basic LLDB setup | Simple template |
| `debug_root_cause.sh` | 67 | Root cause analysis | Uses lldb_root_cause.py |
| `debug_session.sh` | 85 | Comprehensive session | Debugger-wait mode |
| `debug_shape_lldb.sh` | 95 | Advanced with Python | JDWP forwarding, app_process detection |
| `debug_shape_watch.sh` | 57 | Watchpoint-based | wp-start command |
| `debug_with_lldb.sh` | 115 | Automated workflow | **/proc/pid/maps parsing** |
| `lldb_attach.sh` | 89 | Attach with expect | **Expect scripting** |
| `lldb_attach_debug.sh` | 38 | Simple attach | Conditional breakpoint on x28 |
| `lldb_batch.sh` | 59 | Batch mode | Batch mode (-b), tee logging |
| `lldb_simple.sh` | 113 | Simple with expect | **Expect scripting** |
| `lldb_stripped_debug.sh` | 107 | Stripped binary debug | **Address calculation from tombstones** |
| `lldb_debug.sh` | 115 | Main debug script | Async mode settings |
| `lldb_debug_crash.sh` | 51 | Crash debug | Batch mode attach |
| `simple_debug.sh` | 35 | Logcat-only | No LLDB, just logcat |

### Python Scripts (24 files, ~4,300 lines)

| File | Lines | Purpose | Key Features |
|------|-------|---------|--------------|
| `lldb_master_debug.py` | 407 | Master debugger | `analyze_object_state()`, heap scan |
| `lldb_comprehensive_debug.py` | 696 | Comprehensive tracking | `JSObjectTracker`, 5 layers, GC tracking |
| `lldb_shape_debug.py` | 175 | Shape corruption | Watchpoints, `bt-shape` command |
| `lldb_advanced_shape.py` | 325 | Advanced debugging | `inspect_object()`, `watch-shape` command |
| `lldb_memory_scan.py` | 303 | Memory scanning | `scan-objects`, `find-shape-refs` |
| `lldb_crash_debug.py` | 188 | 0xc0000008 crash | `catch_crash()`, `trace_jsvalue()` |
| `lldb_register_debug.py` | 81 | Register analysis | `check_x28()`, JSValue decoding |
| `lldb_root_cause.py` | 447 | Root cause analysis | `RootCauseDebugger`, hardware watchpoints |
| `lldb_shape_investigator.py` | 320 | Shape investigation | `ShapeInvestigator`, allocation tracking |
| `lldb_deep_shape_debug.py` | 289 | Deep investigation | Read helpers, validation |
| `lldb_watchpoint_debug.py` | 187 | Watchpoint debugging | `WatchpointDebugger`, MAX 10 watchpoints |
| `lldb_trace_x28.py` | 144 | x28 origin trace | `step_until_bad_x28()`, instruction stepping |
| `lldb_memory_debug.py` | 455 | Memory debugging | strncpy overflow, mutex tracking |
| `lldb_crash_catch.py` | 171 | Crash catching | `CrashCatchCommand`, SIGSEGV handling |
| `lldb_driver.py` | 543 | Full automation | Main loop with polling, crash triggering |
| `lldb_driver_fixed.py` | 198 | ANR-free driver | **Async mode**, non-blocking |
| `lldb_auto_debug.py` | 102 | Auto debugging | `quickjs_debug()`, function waiting |
| `lldb_investigate.py` | 239 | Investigation | `inspect_object_shape()`, watchpoint setup |

### Text Command Files (7 files, ~300 lines)

| File | Lines | Purpose |
|------|-------|---------|
| `lldb_commands.txt` | 19 | Basic template |
| `lldb_commands_attach.txt` | 26 | Post-attach watchpoint guide |
| `lldb_comprehensive_cmds.txt` | 19 | App startup with PID detection |
| `lldb_batch_debug.txt` | 81 | **Inline Python breakpoint handlers** |
| `lldb_shape_commands.txt` | 41 | Conditional breakpoint commands |
| `lldb_manual_investigation.txt` | 63 | Step-by-step manual guide |
| `lldb_advanced_commands.txt` | 76 | Memory allocation, threading |

## Proposed Architecture

```
lldb/
├── lib/
│   ├── core/                    # Memory, registers, process, automation
│   ├── quickjs/                 # JSObject, JSShape, corruption detection
│   ├── debug/                   # Breakpoints, watchpoints, symbols
│   └── shell/                   # Common shell functions
├── modules/                     # Composable debug modules
│   ├── shape_tracking.py
│   ├── object_tracking.py
│   ├── memory_corruption.py
│   ├── watchpoint_debug.py
│   ├── register_tracking.py
│   └── automation.py            # Expect scripting
├── profiles/                    # Pre-configured combinations
│   ├── comprehensive.py
│   ├── shape_only.py
│   ├── minimal.py
│   ├── register_focus.py
│   └── stripped_binary.py       # For stripped binaries
├── main.py                      # Entry point
├── cli.py                       # Command-line interface
└── scripts/                     # 4 thin wrapper scripts
    ├── debug.sh
    ├── attach.sh
    ├── run.sh
    └── stripped.sh
```

## Duplication Analysis

| Pattern | Files | Lines Duplicated | Solution |
|---------|-------|------------------|----------|
| JSObject reading | 18 Python | ~800 | `lib/quickjs/types.py` |
| Corruption detection | 15 Python | ~500 | `lib/quickjs/corruption.py` |
| Memory reading helpers | 16 Python | ~400 | `lib/core/memory.py` |
| Shell setup (adb/lldb-server) | 22 Shell | ~1200 | `lib/shell/common.sh` |
| Breakpoint callbacks | 14 Python | ~600 | `lib/debug/breakpoints.py` |
| QuickJS constants | 15 Python | ~300 | `lib/quickjs/constants.py` |
| Register reading | 12 Python | ~300 | `lib/core/registers.py` |
| Expect scripting | 3 Shell | ~150 | `lib/core/automation.py` |
| Address calculation | 2 Shell | ~80 | `lib/debug/symbols.py` |
| **TOTAL** | | **~4,330 lines** | |

## New Features Discovered

### 1. Expect Scripting Support
**Files:** `lldb_attach.sh`, `lldb_simple.sh`
```bash
# Pattern found:
/usr/bin/expect << EXPECT_SCRIPT
spawn lldb
expect "(lldb) "
send "platform select remote-android\r"
expect {
    "stop reason" { send "bt\r" }
    timeout { puts "Timeout" }
}
EXPECT_SCRIPT
```
**Solution:** `lib/core/automation.py` - `ExpectScriptBuilder` class

### 2. Stripped Binary Support
**Files:** `lldb_stripped_debug.sh`, `debug_with_lldb.sh`
```bash
# Parse /proc/pid/maps to find library base
BASE=$(adb shell "cat /proc/$PID/maps | grep $LIB_NAME | head -1 | cut -d'-' -f1")
# Calculate absolute address from tombstone offset
ABS_ADDR=$((BASE_DEC + 0xa1f30))
breakpoint set -a 0x$ABS_ADDR
```
**Solution:** `lib/debug/symbols.py` - `StrippedBinaryHelper` class

### 3. Inline Python in Command Files
**Files:** `lldb_batch_debug.txt`, `lldb_shape_commands.txt`
```lldb
breakpoint command add -s python
thread = frame.GetThread()
shape = process.ReadPointerFromMemory(obj + 8, error)
if shape < 0x1000:
    print("CORRUPTION!")
    thread.Stop()
DONE
```
**Solution:** `BreakpointManager.add_with_inline_python()`

### 4. Async Mode Support
**Files:** `lldb_driver_fixed.py`
```python
debugger.SetAsync(True)  # Prevent ANR
process.Continue()  # Non-blocking
```
**Solution:** `ModuleConfig.async_mode` option

### 5. JDWP Port Forwarding
**Files:** `run_register_debug.sh`, `debug_shape_lldb.sh`
```bash
adb forward tcp:8700 jdwp:$PID  # For Java debugging
```

### 6. Library Load Polling
**Files:** `debug_with_lldb.sh`
```bash
for i in {1..20}; do
    BASE=$(adb shell "cat /proc/$PID/maps | grep $LIB | head -1")
    [ -n "$BASE" ] && break
    sleep 0.5
done
```
**Solution:** `StrippedBinaryHelper.wait_for_library_load()`

### 7. Automated Crash Triggering
**Files:** `lldb_driver.py`, `lldb_driver_fixed.py`
```python
subprocess.run(["adb", "shell", "input", "tap", "540", "600"])
subprocess.run(["adb", "shell", "input", "text", "https://..."])
subprocess.run(["adb", "shell", "input", "keyevent", "66"])
```

## Before vs After

### Before (Current State)
```
lldb/
├── 22 shell scripts (duplicate setup code)
├── 24 Python scripts (duplicate JSObject reading)
├── 7 text files (overlapping commands)
└── TOTAL: ~6,800 lines of code
```

### After (Proposed)
```
lldb/
├── lib/
│   ├── core/          # 4 files, ~400 lines (shared)
│   ├── quickjs/       # 4 files, ~500 lines (shared)
│   └── debug/         # 5 files, ~450 lines (shared)
├── modules/           # 7 files, ~700 lines (composable)
├── profiles/          # 6 files, ~100 lines (configurations)
├── main.py            # 1 file, ~100 lines (entry)
├── cli.py             # 1 file, ~80 lines (CLI)
└── scripts/           # 4 files, ~80 lines (wrappers)
    
    TOTAL: ~2,400 lines of code (65% reduction)
```

## Module Composability Examples

### Example 1: Shape Tracking Only
```python
# profiles/shape_only.py
class ShapeOnlyProfile(DebugProfile):
    def configure(self, session):
        session.add_module(ShapeTrackingModule(session))
        session.add_module(ObjectTrackingModule(session))
```

### Example 2: Stripped Binary Debugging
```python
# profiles/stripped_binary.py
class StrippedBinaryProfile(DebugProfile):
    def configure(self, session):
        session.add_module(ShapeTrackingModule(session))
        session.add_module(StrippedBinaryHelper(session))  # Address calculation
        session.add_module(MemoryCorruptionModule(session))
```

### Example 3: Expect Automation
```python
# profiles/automated.py
class AutomatedProfile(DebugProfile):
    def configure(self, session):
        session.add_module(ShapeTrackingModule(session))
        session.add_module(ExpectAutomationModule(session))  # Non-interactive
        session.add_module(CrashAnalysisModule(session))
```

### Example 4: Custom On Command Line
```bash
# Mix and match modules dynamically
lldb -o "command script import main.py" \
     -o "qjs-debug minimal" \
     -o "qjs-module-add watchpoint_debug" \
     -o "qjs-module-add expect_automation" \
     -o "continue"
```

## Benefits Summary

| Aspect | Current | Proposed | Benefit |
|--------|---------|----------|---------|
| **Files** | 49 | ~28 | 43% reduction |
| **Code Lines** | ~6,800 | ~2,400 | 65% reduction |
| **Duplication** | ~4,330 lines | ~200 lines | 95% reduction |
| **Entry Points** | 22 scripts | 4 scripts | 82% reduction |
| **Maintainability** | Poor | Good | Centralized fixes |
| **Testability** | None | Unit tests | Per-module testing |
| **Extensibility** | Copy-paste | Inherit base | Composable |
| **New Features** | Hard to add | Easy to add | Module system |

## Migration Strategy

```
Week 1: Core Library
  ├─ Create lib/core/ (memory, registers, process, automation)
  ├─ Create lib/quickjs/ (constants, types, inspector, corruption)
  └─ Validate with 2-3 ported scripts

Week 2: Debug Framework  
  ├─ Create lib/debug/ (breakpoints, watchpoints, commands, session, symbols)
  ├─ Create modules/base.py
  └─ Port 5 more scripts to modules

Week 3: Profiles & Entry Points
  ├─ Create profiles/ (comprehensive, minimal, stripped_binary, etc.)
  ├─ Create main.py and cli.py
  └─ Create thin shell script wrappers

Week 4: Deprecation
  ├─ Add deprecation warnings to old scripts
  ├─ Update documentation
  └─ Monitor for issues

Week 5+: Cleanup
  └─ Remove old scripts after validation period
```

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Break existing workflows | Keep old scripts during migration (4+ weeks) |
| New system has bugs | Extensive testing before deprecation |
| Learning curve | Comprehensive docs + examples + migration guide |
| Performance regression | Profiling + optimization before release |
| Missing features | Feature parity checklist before deprecation |

## Recommended First Steps

1. **Create `lib/core/memory.py`** - Most widely used (16 files)
2. **Create `lib/quickjs/types.py`** - Central JSObject/JSShape parsing (18 files)
3. **Create `lib/shell/common.sh`** - Shared shell functions (22 files)
4. **Port `lldb_memory_scan.py`** → Use new library → Validate
5. **Port `lldb_shape_debug.py`** → Use new library → Validate
6. **Iterate** on design based on porting experience
