"""
Register tracking focused profile.
"""

from profiles.base import DebugProfile
from modules import (
    RegisterTrackingModule,
    MemoryCorruptionModule,
    CrashAnalysisModule,
)


class RegisterFocusProfile(DebugProfile):
    """Focus on register corruption detection (x28 tracking)."""
    
    name = "register_focus"
    description = "x28 register and corruption tracking"
    
    def configure(self, session):
        """Add register tracking modules."""
        session.add_module(RegisterTrackingModule(session))
        session.add_module(MemoryCorruptionModule(session))
        session.add_module(CrashAnalysisModule(session))
