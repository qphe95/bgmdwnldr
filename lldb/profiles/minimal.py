"""
Minimal debug profile - essential detection only.
"""

from profiles.base import DebugProfile
from modules import MemoryCorruptionModule


class MinimalProfile(DebugProfile):
    """Minimal debugging with corruption detection only."""
    
    name = "minimal"
    description = "Essential corruption detection only"
    
    def configure(self, session):
        """Add minimal modules."""
        session.add_module(MemoryCorruptionModule(session))
