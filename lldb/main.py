#!/usr/bin/env python3
"""
Main entry point for the QuickJS Debug System.

Usage:
    (lldb) command script import main.py
    (lldb) qjs-debug comprehensive
    (lldb) qjs-debug minimal
    (lldb) qjs-debug shape_only
"""

import lldb
import sys
import os

# Add lib directory to path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)

from lib.debug.commands import CommandRegistry, CommandSpec
from lib.debug.session import DebugSession
from profiles import AVAILABLE_PROFILES
from modules import AVAILABLE_MODULES

# Global session
_session: 'DebugSession' = None


def qjs_debug(debugger, command, result, internal_dict):
    """
    Start debugging with specified profile.
    
    Usage: qjs-debug [profile_name]
    
    Available profiles:
        comprehensive    - Full debugging with all modules
        minimal          - Essential corruption detection only
        shape_only       - Shape and object lifecycle tracking
        register_focus   - x28 register tracking
        stripped_binary  - For stripped binaries
    """
    global _session
    
    args = command.strip().split()
    profile_name = args[0] if args else "comprehensive"
    
    if profile_name not in AVAILABLE_PROFILES:
        result.write(f"Unknown profile: {profile_name}\n")
        result.write(f"Available profiles: {', '.join(AVAILABLE_PROFILES.keys())}\n")
        return
    
    # Get PID if available
    pid = None
    target = debugger.GetSelectedTarget()
    if target:
        process = target.GetProcess()
        if process:
            pid = process.GetProcessID()
    
    _session = DebugSession(debugger, pid=pid)
    
    # Load profile
    profile_class = AVAILABLE_PROFILES[profile_name]
    profile = profile_class()
    profile.configure(_session)
    
    result.write(f"\n{'='*70}\n")
    result.write(f"QuickJS Debug System - Profile: {profile_name}\n")
    result.write(f"{'='*70}\n")
    result.write(f"Loaded {len(_session.modules)} module(s):\n")
    for module in _session.modules:
        result.write(f"  - {module.name}: {module.description}\n")
    result.write(f"{'='*70}\n\n")
    result.write("Commands available:\n")
    result.write("  qjs-status       - Show debug session status\n")
    result.write("  qjs-module-add   - Add a module dynamically\n")
    result.write("  qjs-dump-obj <addr> - Inspect object at address\n")
    result.write("  continue         - Continue execution\n\n")


def qjs_module_add(debugger, command, result, internal_dict):
    """
    Add a debug module dynamically.
    
    Usage: qjs-module-add <module_name>
    
    Available modules:
        shape_tracking    - Track shape allocation/freeing
        object_tracking   - Track object lifecycle
        memory_corruption - Detect corruption
        watchpoint_debug  - Use hardware watchpoints
        register_tracking - Track x28 register
        crash_analysis    - Analyze crashes
    """
    global _session
    
    if not _session:
        result.write("No active debug session. Run 'qjs-debug' first.\n")
        return
    
    module_name = command.strip()
    if not module_name:
        result.write("Usage: qjs-module-add <module_name>\n")
        result.write(f"Available: {', '.join(AVAILABLE_MODULES.keys())}\n")
        return
    
    if module_name not in AVAILABLE_MODULES:
        result.write(f"Unknown module: {module_name}\n")
        result.write(f"Available: {', '.join(AVAILABLE_MODULES.keys())}\n")
        return
    
    module_class = AVAILABLE_MODULES[module_name]
    module = module_class(_session)
    _session.add_module(module)
    
    result.write(f"Added module: {module.name}\n")
    result.write(f"  {module.description}\n")


def qjs_status(debugger, command, result, internal_dict):
    """
    Show debug session status.
    """
    global _session
    
    if not _session:
        result.write("No active debug session.\n")
        return
    
    _session.print_status()


def qjs_dump_obj(debugger, command, result, internal_dict):
    """
    Inspect object at address.
    
    Usage: qjs-dump-obj <address>
    """
    global _session
    
    if not _session or not _session.memory_reader:
        result.write("No active debug session with memory access.\n")
        return
    
    addr_str = command.strip()
    if not addr_str:
        result.write("Usage: qjs-dump-obj <address>\n")
        return
    
    try:
        addr = int(addr_str, 0)  # Handles 0x prefix
    except ValueError:
        result.write(f"Invalid address: {addr_str}\n")
        return
    
    from lib.quickjs.inspector import ObjectInspector
    inspector = ObjectInspector(_session.memory_reader)
    dump = inspector.dump_object(addr)
    result.write(dump + "\n")


def qjs_list_profiles(debugger, command, result, internal_dict):
    """
    List available debug profiles.
    """
    result.write("Available profiles:\n")
    for name, profile_class in AVAILABLE_PROFILES.items():
        profile = profile_class()
        result.write(f"  {name:15} - {profile.description}\n")


def qjs_list_modules(debugger, command, result, internal_dict):
    """
    List available debug modules.
    """
    result.write("Available modules:\n")
    for name, module_class in AVAILABLE_MODULES.items():
        # Create dummy instance to get description
        dummy_config = type('Config', (), {'enabled': True, 'verbose': False})()
        dummy_session = type('Session', (), {})()
        try:
            module = module_class(dummy_session, dummy_config)
            result.write(f"  {name:15} - {module.description}\n")
        except:
            result.write(f"  {name}\n")


def __lldb_init_module(debugger, internal_dict):
    """Initialize module and register commands."""
    
    # Register commands
    registry = CommandRegistry(debugger)
    
    registry.register(CommandSpec(
        name="qjs-debug",
        handler=qjs_debug,
        help_short="Start debugging with specified profile",
        help_long="Usage: qjs-debug [profile_name]"
    ))
    
    registry.register(CommandSpec(
        name="qjs-module-add",
        handler=qjs_module_add,
        help_short="Add a debug module dynamically",
        help_long="Usage: qjs-module-add <module_name>"
    ))
    
    registry.register(CommandSpec(
        name="qjs-status",
        handler=qjs_status,
        help_short="Show debug session status",
    ))
    
    registry.register(CommandSpec(
        name="qjs-dump-obj",
        handler=qjs_dump_obj,
        help_short="Inspect object at address",
        help_long="Usage: qjs-dump-obj <address>"
    ))
    
    registry.register(CommandSpec(
        name="qjs-list-profiles",
        handler=qjs_list_profiles,
        help_short="List available debug profiles",
    ))
    
    registry.register(CommandSpec(
        name="qjs-list-modules",
        handler=qjs_list_modules,
        help_short="List available debug modules",
    ))
    
    # Print banner
    print("=" * 70)
    print("QuickJS Shape Corruption Debug System")
    print("=" * 70)
    print()
    print("Commands available:")
    print("  qjs-debug [profile]    - Start debugging")
    print("  qjs-list-profiles      - List available profiles")
    print("  qjs-list-modules       - List available modules")
    print("  qjs-module-add <name>  - Add module dynamically")
    print("  qjs-status             - Show session status")
    print("  qjs-dump-obj <addr>    - Inspect object")
    print()
    print("Quick start:")
    print("  (lldb) qjs-debug comprehensive")
    print("  (lldb) continue")
    print("=" * 70)


if __name__ == "__main__":
    print("This script is meant to be run within LLDB")
    print("Load it with: command script import main.py")
