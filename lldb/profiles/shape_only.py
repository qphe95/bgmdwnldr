"""
Shape tracking focused profile.
"""

from .base import DebugProfile
from ..modules import (
    ShapeTrackingModule,
    ObjectTrackingModule,
    MemoryCorruptionModule,
)


class ShapeOnlyProfile(DebugProfile):
    """Focus on shape tracking and lifecycle."""
    
    name = "shape_only"
    description = "Shape and object lifecycle tracking"
    
    def configure(self, session):
        """Add shape tracking modules."""
        session.add_module(ShapeTrackingModule(session))
        session.add_module(ObjectTrackingModule(session))
        session.add_module(MemoryCorruptionModule(session))
