# QuickJS Debug System - Improved

A fixed and improved debugging system for investigating QuickJS shape corruption issues in the bgmdwnldr app.

## Quick Start (Recommended)

The fastest way to start debugging:

```bash
./lldb/quickstart.sh
```

This will:
1. Check prerequisites (adb, lldb, device)
2. Start lldb-server on the device
3. Start the app if not running
4. Launch LLDB with the comprehensive debug profile
5. Provide instructions for triggering the crash

Then in another terminal:
```bash
./lldb/scripts/trigger-crash.sh
```

## What's Fixed

### 1. Python Syntax Error (cli.py)
- **Problem**: `async` is a reserved keyword in Python 3.7+
- **Fix**: Renamed parameter to `--async-mode` / `async_mode`

### 2. Remote Debugging
- **Problem**: Old scripts used `lldb -p` for local processes
- **Fix**: Now properly uses `platform select remote-android` and `platform connect`

### 3. Interactive Mode
- **Problem**: Batch mode with `continue` doesn't stop on crash
- **Fix**: Interactive session allows proper debugging when crash occurs

### 4. Crash Detection
- **Problem**: No automatic crash detection or capture
- **Fix**: New `debug-catch.sh` monitors logcat and captures tombstones

## Available Scripts

| Script | Purpose | Usage |
|--------|---------|-------|
| `quickstart.sh` | Easiest way to start | `./lldb/quickstart.sh [profile]` |
| `scripts/debug.sh` | Interactive debugging | `./lldb/scripts/debug.sh [profile]` |
| `scripts/debug-catch.sh` | Auto crash detection | `./lldb/scripts/debug-catch.sh [profile] [output_dir]` |
| `scripts/trigger-crash.sh` | Trigger crash via UI | `./lldb/scripts/trigger-crash.sh [url]` |
| `scripts/attach.sh` | Attach to running app | `./lldb/scripts/attach.sh [profile]` |

## Debug Profiles

- `comprehensive` - All features (shape tracking, corruption detection, watchpoints)
- `minimal` - Essential corruption detection only (fastest)
- `shape_only` - Shape lifecycle tracking
- `register_focus` - Monitor x28 register for 0xc0000000 pattern
- `stripped` - For production builds without symbols

## Example Workflows

### Interactive Debugging
```bash
# Terminal 1: Start debugger
./lldb/quickstart.sh comprehensive

# Terminal 2: Trigger crash
./lldb/scripts/trigger-crash.sh "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

# Back in Terminal 1: When crash occurs, LLDB will show prompt
(lldb) bt              # Show backtrace
(lldb) register read   # Show registers
(lldb) qjs-dump-obj <addr>  # Inspect object
```

### Automatic Crash Capture
```bash
# Terminal 1: Start crash catcher
./lldb/scripts/debug-catch.sh comprehensive /tmp/crash1

# Terminal 2: Trigger crash
./lldb/scripts/trigger-crash.sh

# Results saved to /tmp/crash1/
```

### Manual Control
```bash
# Start app
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# Get PID
adb shell pidof com.bgmdwldr.vulkan

# Start lldb-server
adb shell "/data/local/tmp/lldb-server platform --listen '*:5039' --server" &

# Forward port
adb forward tcp:5039 tcp:5039

# Run LLDB
lldb
(lldb) platform select remote-android
(lldb) platform connect connect://localhost:5039
(lldb) process attach -p <PID>
(lldb) command script import lldb/main.py
(lldb) qjs-debug comprehensive
```

## Troubleshooting

### "No Android device connected"
```bash
adb devices
# Should show: emulator-5554 device

# If not:
adb kill-server
adb start-server
```

### "lldb-server not found"
```bash
# Find lldb-server in NDK
find $ANDROID_HOME/ndk -name "lldb-server" -type f | grep aarch64

# Push to device
adb push <path>/lldb-server /data/local/tmp/
adb shell chmod +x /data/local/tmp/lldb-server
```

### "attach failed: Operation not permitted"
The old scripts tried to use local process attachment. Use the new scripts which properly use remote platform debugging.

### Crash not caught
Make sure to trigger the crash AFTER LLDB has attached and shown the "READY" message.

## Files Overview

```
lldb/
├── quickstart.sh          # <-- Start here
├── cli.py                 # Fixed Python syntax error
├── main.py               # LLDB entry point
├── scripts/
│   ├── debug.sh          # Interactive debugging
│   ├── debug-catch.sh    # Auto crash detection
│   ├── trigger-crash.sh  # UI automation
│   ├── attach.sh         # Attach mode
│   └── ...
├── lib/                  # Core libraries
├── modules/              # Debug modules
└── profiles/             # Debug profiles
```

## Migration from Old Scripts

| Old Command | New Command |
|-------------|-------------|
| `./lldb/scripts/debug.sh` | Same, but actually works now |
| `lldb -p <pid>` | Use `./lldb/quickstart.sh` instead |
| Python CLI with `--async` | Use `--async-mode` (or just use shell scripts) |

## Requirements

- Android device connected via ADB (or emulator)
- LLDB installed (Xcode on macOS or Android NDK)
- Python 3.7+ (for CLI)
- adb in PATH
