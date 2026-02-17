"""
Core library for LLDB debugging utilities.
"""

from .memory import MemoryReader, MemoryScanner, MemoryRegion
from .registers import RegisterSet, RegisterMonitor, ARM64Reg
from .process import ProcessControl
from .automation import ExpectScriptBuilder, ExpectInteraction

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
]
