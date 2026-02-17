"""
Debugging framework components.
"""

from .breakpoints import BreakpointConfig, BreakpointManager
from .watchpoints import WatchpointConfig, WatchpointManager
from .commands import CommandSpec, CommandRegistry, command
from .session import DebugSession, ModuleConfig
from .symbols import StrippedBinaryHelper, FunctionAddress

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
