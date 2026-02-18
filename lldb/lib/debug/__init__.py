"""
Debugging framework components.
"""

from lib.debug.breakpoints import BreakpointConfig, BreakpointManager
from lib.debug.watchpoints import WatchpointConfig, WatchpointManager
from lib.debug.commands import CommandSpec, CommandRegistry, command
from lib.debug.session import DebugSession, ModuleConfig
from lib.debug.symbols import StrippedBinaryHelper, FunctionAddress

__all__ = [
    'BreakpointConfig',
    'BreakpointManager',
    'WatchpointConfig',
    'WatchpointManager',
    'CommandSpec',
    'CommandRegistry',
    'command',
    'DebugSession',
    'ModuleConfig',
    'StrippedBinaryHelper',
    'FunctionAddress',
]
