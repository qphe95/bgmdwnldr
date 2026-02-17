"""
Track register values, especially x28 for 0xc0000000 pattern.
"""

from typing import Dict, List
from .base import DebugModule, ModuleConfig
from ..lib.core.registers import RegisterMonitor, get_register
from ..lib.debug.breakpoints import BreakpointConfig


class RegisterTrackingModule(DebugModule):
    """Track suspicious register values."""
    
    name = "register_tracker"
    description = "Track x28 and other registers for corruption patterns"
    
    SUSPICIOUS_PATTERNS = [
        0xc0000000,
        0xc0000008,
        0xFFFFFFFFFFFFFFFF,
        0xFFFFFFFFFFFFFFFE,
    ]
    
    def __init__(self, session, config=None):
        super().__init__(session, config)
        self.monitor = RegisterMonitor()
        self.track_list: List[str] = ['x28', 'x0', 'x1', 'x2', 'lr']
        self._suspicious_hits: Dict[int, int] = {}  # value -> count
        self._state = {
            'suspicious_hits': 0,
            'registers_tracked': 0,
        }
    
    def setup(self):
        """Set up breakpoints to monitor registers."""
        configs = [
            BreakpointConfig(
                name="JS_DefineProperty",
                on_hit=self._check_registers
            ),
            BreakpointConfig(
                name="JS_SetPropertyStr",
                on_hit=self._check_registers
            ),
            BreakpointConfig(
                name="find_own_property",
                on_hit=self._check_registers
            ),
        ]
        self.session.breakpoint_manager.add_multi(configs)
        self.log(f"Register tracking enabled for: {', '.join(self.track_list)}")
    
    def _check_registers(self, frame, bp_loc) -> bool:
        """Check registers for suspicious values."""
        suspicious = self.monitor.find_suspicious_values(
            patterns=self.SUSPICIOUS_PATTERNS,
            frame=frame
        )
        
        if suspicious:
            func_name = frame.GetFunctionName() or "???"
            print(f"\n[RegTrack] Suspicious values in {func_name}:")
            
            for reg_name, value in suspicious.items():
                print(f"  {reg_name} = 0x{value:016x}")
                
                if value in self._suspicious_hits:
                    self._suspicious_hits[value] += 1
                else:
                    self._suspicious_hits[value] = 1
            
            self._state['suspicious_hits'] = sum(self._suspicious_hits.values())
            
            # Stop if critical pattern found
            if 0xc0000000 in suspicious.values() or 0xc0000008 in suspicious.values():
                print("!!! CRITICAL: x28 corruption pattern detected !!!")
                return self.config.stop_on_event
        
        return False
    
    def get_suspicious_stats(self) -> Dict[int, int]:
        """Get statistics on suspicious values found."""
        return self._suspicious_hits.copy()
