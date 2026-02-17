"""
Profile for debugging stripped binaries.
"""

from .base import DebugProfile
from ..modules import (
    ShapeTrackingModule,
    ObjectTrackingModule,
    MemoryCorruptionModule,
)
from ..lib.debug.symbols import StrippedBinaryHelper


class StrippedBinaryProfile(DebugProfile):
    """Debugging stripped binaries with address-based breakpoints."""
    
    name = "stripped_binary"
    description = "For debugging stripped binaries via address calculation"
    
    def configure(self, session):
        """Add modules for stripped binary debugging."""
        # Add symbol helper if we have a PID
        if session.pid:
            helper = StrippedBinaryHelper(session.pid)
            if helper.wait_for_library_load():
                # Setup address-based breakpoints
                helper.setup_all_breakpoints(session.target)
                print(f"[StrippedBinary] Library base: 0x{helper.base_addr:x}")
        
        session.add_module(ShapeTrackingModule(session))
        session.add_module(ObjectTrackingModule(session))
        session.add_module(MemoryCorruptionModule(session))
