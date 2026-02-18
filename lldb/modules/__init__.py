"""
Composable debug modules.
"""

from modules.base import DebugModule, ModuleConfig, DebugSession
from modules.shape_tracking import ShapeTrackingModule
from modules.object_tracking import ObjectTrackingModule
from modules.memory_corruption import MemoryCorruptionModule
from modules.watchpoint_debug import WatchpointDebugModule
from modules.register_tracking import RegisterTrackingModule
from modules.crash_analysis import CrashAnalysisModule
from modules.automation import ExpectAutomationModule

AVAILABLE_MODULES = {
    'shape_tracking': ShapeTrackingModule,
    'object_tracking': ObjectTrackingModule,
    'memory_corruption': MemoryCorruptionModule,
    'watchpoint_debug': WatchpointDebugModule,
    'register_tracking': RegisterTrackingModule,
    'crash_analysis': CrashAnalysisModule,
    'expect_automation': ExpectAutomationModule,
    'automation': ExpectAutomationModule,
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
