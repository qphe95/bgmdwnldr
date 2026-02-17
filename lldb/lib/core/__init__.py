"""
Core library for LLDB debugging utilities.
"""

from .memory import MemoryReader, MemoryScanner, MemoryRegion
from .registers import RegisterSet, RegisterMonitor, ARM64Reg
from .process import ProcessControl
from .automation import ExpectScriptBuilder, ExpectInteraction, LLDBServerManager, ADBAutomation
from .events import EventType, DebugEvent, EventHandler, EventDispatcher

__all__ = [
    'MemoryReader',
    'MemoryScanner',
    'MemoryRegion',
    'RegisterSet',
    'RegisterMonitor',
    'ARM64Reg',
    'ProcessControl',
    'ExpectScriptBuilder',
    'ExpectInteraction',
    'LLDBServerManager',
    'ADBAutomation',
    'EventType',
    'DebugEvent',
    'EventHandler',
    'EventDispatcher',
]
