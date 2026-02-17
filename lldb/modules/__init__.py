"""
Composable debug modules.
"""

from .base import DebugModule, ModuleConfig, DebugSession
from .shape_tracking import ShapeTrackingModule
from .object_tracking import ObjectTrackingModule
from .memory_corruption import MemoryCorruptionModule
from .watchpoint_debug import WatchpointDebugModule
from .register_tracking import RegisterTrackingModule
from .crash_analysis import CrashAnalysisModule

AVAILABLE_MODULES = {
    'shape_tracking': ShapeTrackingModule,
    'object_tracking': ObjectTrackingModule,
    'memory_corruption': MemoryCorruptionModule,
    'watchpoint_debug': WatchpointDebugModule,
    'register_tracking': RegisterTrackingModule,
    'crash_analysis': CrashAnalysisModule,
}

__all__ = [
    'DebugModule',
    'ModuleConfig',
    'DebugSession',
    'ShapeTrackingModule',
    'ObjectTrackingModule',
    'MemoryCorruptionModule',
    'WatchpointDebugModule',
    'RegisterTrackingModule',
    'CrashAnalysisModule',
    'AVAILABLE_MODULES',
]
