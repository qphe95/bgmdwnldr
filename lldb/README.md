# QuickJS Debug System

A composable, modular debugging system for investigating QuickJS shape corruption issues in the bgmdwnldr app.

## Quick Start

### Prerequisites

- Android device connected via ADB
- LLDB installed (included with Xcode on macOS or Android NDK)
- App installed: `./rebuild.sh`

### 1. Start Debugging (Simplest)

```bash
# From the project root
./lldb/scripts/debug.sh comprehensive
```

This will:
1. Start the app if not running
2. Get the app PID
3. Attach LLDB with the comprehensive debug profile
4. Automatically detect and stop on corruption

### 2. Attach to Running App

```bash
# If app is already running
./lldb/scripts/attach.sh minimal
```

### 3. Debug Stripped Binary

```bash
# For production builds without symbols
./lldb/scripts/stripped.sh
```

## Debug Profiles

Profiles are pre-configured combinations of debug modules:

| Profile | Best For | What It Does |
|---------|----------|--------------|
| `comprehensive` | First-time debugging | All features: shape tracking, object tracking, corruption detection, watchpoints, register monitoring |
| `minimal` | Quick check | Essential corruption detection only, fastest startup |
| `shape_only` | Shape lifecycle issues | Tracks shape allocation/freeing, object lifecycle |
| `register_focus` | x28 corruption | Monitors x28 register for 0xc0000000 pattern |
| `stripped` | Production builds | Uses address-based breakpoints for stripped binaries |

### Using Profiles

```bash
# With shell scripts
./lldb/scripts/debug.sh comprehensive
./lldb/scripts/debug.sh minimal

# Or directly in LLDB
(lldb) command script import lldb/main.py
(lldb) qjs-debug comprehensive
```

## Interactive Debugging Commands

Once LLDB is running, you have these commands available:

```bash
# Check current debug session status
(lldb) qjs-status

# Inspect an object at address
(lldb) qjs-dump-obj 0xb4000078857cb8c8

# Add a module dynamically
(lldb) qjs-module-add watchpoint_debug

# List available profiles
(lldb) qjs-list-profiles

# List available modules
(lldb) qjs-list-modules

# Continue execution (runs until corruption detected)
(lldb) continue
```

## When Corruption is Detected

The debugger will automatically stop and show:

```
======================================================================
!!! SHAPE CORRUPTION DETECTED !!!
======================================================================
Type: tagged_value_as_pointer
Object: 0xb4000078857cb8c8
Shape value: 0xc0000008
Description: Shape looks like tagged JSValue: JS_TAG_EXCEPTION

Backtrace:
  #0: find_own_property
  #1: JS_SetPropertyStr
  #2: init_browser_stubs
  ...
======================================================================
```

Then you can:
- `(lldb) bt` - Show full backtrace
- `(lldb) register read` - Show all registers
- `(lldb) qjs-dump-obj <addr>` - Deep inspection of corrupted object
- `(lldb) continue` - Continue execution
- `(lldb) quit` - Exit

## Manual LLDB Session

If you prefer manual control:

```bash
# Terminal 1: Start app
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# Get PID
PID=$(adb shell pidof com.bgmdwldr.vulkan)

# Terminal 2: Start LLDB
lldb -p $PID

# In LLDB:
(lldb) command script import lldb/main.py
(lldb) qjs-debug comprehensive
(lldb) continue

# In Terminal 3: Trigger the crash
adb shell input tap 540 600
adb shell input text "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
adb shell input keyevent 66
```

## Troubleshooting

### "No Android device connected"
```bash
# Check device
adb devices
# Should show: XXXXXXXX device

# If not, restart adb
adb kill-server
adb start-server
```

### "Could not get app PID"
```bash
# Start app manually
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# Then retry
./lldb/scripts/attach.sh
```

### lldb-server not found
```bash
# The script will try to find it in NDK, or you can push manually:
NDK=$HOME/Library/Android/sdk/ndk/26.2.11394342
adb push $NDK/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/17/lib/linux/arm64/lldb-server /data/local/tmp/
adb shell chmod +x /data/local/tmp/lldb-server
```

### ANR (App Not Responding) when attaching
Use async mode:
```bash
# In LLDB before attaching:
(lldb) settings set target.async true
```

## Advanced: Custom Module Composition

Instead of using a profile, you can mix and match modules:

```bash
(lldb) command script import lldb/main.py
(lldb) qjs-debug minimal              # Start with minimal
(lldb) qjs-module-add shape_tracking  # Add shape tracking
(lldb) qjs-module-add watchpoint_debug # Add watchpoints
(lldb) continue
```

## Advanced: Python CLI

For non-interactive automation:

```bash
# Run with comprehensive profile, wait for crash
python3 lldb/cli.py --launch --profile comprehensive --wait-for-crash

# Attach to existing process
python3 lldb/cli.py --attach --pid 1234 --profile minimal

# Trigger crash automatically and capture output
python3 lldb/cli.py --launch --profile comprehensive \
    --trigger-crash --output results.log
```

## Environment Variables

```bash
# Customize defaults
export QJS_APP_PACKAGE="com.bgmdwldr.vulkan"
export QJS_ACTIVITY=".MainActivity"
export QJS_LLDB_SERVER="/data/local/tmp/lldb-server"
export QJS_LIB_NAME="libminimalvulkan.so"
```

## Files Overview

```
lldb/
├── scripts/           # Thin shell wrappers (start here)
│   ├── debug.sh      # Main entry point
│   ├── attach.sh     # Attach only
│   ├── run.sh        # Full orchestration
│   └── stripped.sh   # Stripped binary support
├── main.py           # LLDB entry point
├── cli.py            # Standalone CLI
├── lib/              # Core libraries
│   ├── core/         # Memory, registers, process, automation
│   ├── quickjs/      # JSObject, JSShape types
│   ├── debug/        # Breakpoints, watchpoints
│   └── shell/        # Shell utilities
├── modules/          # Debug modules (composable)
└── profiles/         # Pre-configured setups
```

## Migration from Old Scripts

If you were using the old fragmented scripts:

| Old Script | New Command |
|------------|-------------|
| `run_comprehensive_debug.sh` | `./lldb/scripts/debug.sh comprehensive` |
| `debug_attach.sh` | `./lldb/scripts/attach.sh` |
| `run_full_debug.sh` | `./lldb/scripts/debug.sh comprehensive` |
| `lldb_stripped_debug.sh` | `./lldb/scripts/stripped.sh` |
| `lldb_master_debug.py` | `qjs-debug comprehensive` |
| `lldb_shape_debug.py` | `qjs-debug shape_only` |
| `lldb_register_debug.py` | `qjs-debug register_focus` |

## Getting Help

1. Check LLDB_DEBUG_GUIDE.md for detailed LLDB commands
2. Run `qjs-list-profiles` or `qjs-list-modules` in LLDB
3. Check `qjs-status` to see current debug state
