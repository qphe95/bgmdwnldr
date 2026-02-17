"""
Comprehensive debug profile - all modules enabled.
"""

from .base import DebugProfile
from ..modules import (
    ShapeTrackingModule,
    ObjectTrackingModule,
    MemoryCorruptionModule,
    WatchpointDebugModule,
    RegisterTrackingModule,
    CrashAnalysisModule,
)


class ComprehensiveProfile(DebugProfile):
    """Full debugging with all modules enabled."""
    
    name = "comprehensive"
    description = "Full debugging with all features enabled"
    
    def configure(self, session):
        """Add all available modules."""
        session.add_module(ShapeTrackingModule(session))
        session.add_module(ObjectTrackingModule(session))
        session.add_module(MemoryCorruptionModule(session))
        session.add_module(WatchpointDebugModule(session))
        session.add_module(RegisterTrackingModule(session))
        session.add_module(CrashAnalysisModule(session))
