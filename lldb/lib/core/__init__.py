"""
Core library for LLDB debugging utilities.
"""

from lib.core.memory import MemoryReader, MemoryScanner, MemoryRegion
from lib.core.registers import RegisterSet, RegisterMonitor, ARM64Reg
from lib.core.process import ProcessControl
from lib.core.automation import ExpectScriptBuilder, ExpectInteraction, LLDBServerManager, ADBAutomation
from lib.core.events import EventType, DebugEvent, EventHandler, EventDispatcher

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
