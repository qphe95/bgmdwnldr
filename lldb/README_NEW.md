# QuickJS Debug System (Refactored)

This is the refactored debugging system for QuickJS shape corruption issues. It replaces 49 fragmented scripts with a composable, modular architecture.

## Quick Start

```bash
# From the lldb/scripts/ directory
cd lldb/scripts/

# Debug with full features (recommended)
./debug.sh comprehensive

# Attach to running app with minimal setup
./attach.sh minimal

# Full orchestration with remote platform
./run.sh comprehensive

# Debug stripped binary
./stripped.sh
```

## Architecture

```
lldb/
├── lib/                          # Core libraries
│   ├── core/                     # Memory, registers, process, automation
│   │   ├── memory.py            # Memory reading with caching
│   │   ├── registers.py         # ARM64 register access
│   │   ├── process.py           # Process control
│   │   └── automation.py        # Expect scripting, ADB automation
│   ├── quickjs/                  # QuickJS-specific
│   │   ├── constants.py         # Offsets, tags, patterns
│   │   ├── types.py             # JSObject, JSShape, JSValue
│   │   ├── inspector.py         # Object inspection
│   │   └── corruption.py        # Corruption detection
│   ├── debug/                    # Debugging framework
│   │   ├── breakpoints.py       # Breakpoint management
│   │   ├── watchpoints.py       # Watchpoint management
│   │   ├── commands.py          # Command registration
│   │   ├── session.py           # Session management
│   │   └── symbols.py           # Stripped binary support
│   └── shell/common.sh          # Shared shell functions
├── modules/                      # Composable debug modules
│   ├── shape_tracking.py        # Track shape lifecycle
│   ├── object_tracking.py       # Track object lifecycle
│   ├── memory_corruption.py     # Detect corruption
│   ├── watchpoint_debug.py      # Hardware watchpoints
│   ├── register_tracking.py     # Track x28, etc.
│   └── crash_analysis.py        # Crash analysis
├── profiles/                     # Pre-configured setups
│   ├── comprehensive.py         # All modules
│   ├── minimal.py               # Essential only
│   ├── shape_only.py            # Shape tracking focus
│   ├── register_focus.py        # Register tracking
│   └── stripped_binary.py       # Stripped binary support
├── main.py                      # Main entry point
└── scripts/                     # Thin shell wrappers
    ├── debug.sh                 # Main debug script
    ├── attach.sh                # Attach only
    ├── run.sh                   # Full orchestration
    └── stripped.sh              # Stripped binary
```

## Usage

### Within LLDB

```bash
# Load the debug system
(lldb) command script import main.py

# Start with a profile
(lldb) qjs-debug comprehensive
(lldb) qjs-debug minimal
(lldb) qjs-debug shape_only

# Add modules dynamically
(lldb) qjs-module-add watchpoint_debug

# Check status
(lldb) qjs-status

# Inspect an object
(lldb) qjs-dump-obj 0xb4000078857cb8c8

# Continue execution
(lldb) continue
```

### Available Profiles

| Profile | Description | Modules |
|---------|-------------|---------|
| `comprehensive` | Full debugging | All modules |
| `minimal` | Essential only | Corruption detection only |
| `shape_only` | Shape lifecycle | Shape + Object tracking |
| `register_focus` | Register tracking | x28 + Corruption + Crash |
| `stripped` | Stripped binaries | Address-based breakpoints |

### Available Modules

| Module | Purpose |
|--------|---------|
| `shape_tracking` | Track shape allocation/freeing |
| `object_tracking` | Track object lifecycle |
| `memory_corruption` | Detect corruption patterns |
| `watchpoint_debug` | Hardware watchpoints on shapes |
| `register_tracking` | Monitor x28 and other registers |
| `crash_analysis` | Analyze crash state |

## Migration from Old Scripts

| Old Script | New Equivalent |
|------------|----------------|
| `run_comprehensive_debug.sh` | `./scripts/debug.sh comprehensive` |
| `debug_attach.sh` | `./scripts/attach.sh` |
| `lldb_master_debug.py` | `qjs-debug comprehensive` |
| `lldb_shape_debug.py` | `qjs-debug shape_only` |
| `lldb_register_debug.py` | `qjs-debug register_focus` |
| `lldb_stripped_debug.sh` | `./scripts/stripped.sh` |

## Benefits

- **65% less code**: ~2,400 lines vs ~6,800 lines
- **95% less duplication**: Shared core library
- **Composable**: Mix and match modules
- **Testable**: Modular units can be tested independently
- **Maintainable**: One place for fixes

## Key Commands

```bash
# List profiles
(lldb) qjs-list-profiles

# List modules
(lldb) qjs-list-modules

# Start debugging
(lldb) qjs-debug comprehensive

# Add module dynamically
(lldb) qjs-module-add watchpoint_debug

# Check status
(lldb) qjs-status

# Inspect object
(lldb) qjs-dump-obj <address>
```

## Configuration

Set environment variables before running scripts:

```bash
export QJS_APP_PACKAGE="com.bgmdwldr.vulkan"
export QJS_ACTIVITY=".MainActivity"
export QJS_LLDB_SERVER="/data/local/tmp/lldb-server"
export QJS_LIB_NAME="libminimalvulkan.so"
```
