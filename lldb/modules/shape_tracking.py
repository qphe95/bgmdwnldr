"""
Track shape allocation, freeing, and usage.
"""

from typing import Dict, List
from dataclasses import dataclass, field
from datetime import datetime
from .base import DebugModule, ModuleConfig
from ..lib.quickjs.types import JSShape
from ..lib.debug.breakpoints import BreakpointConfig


@dataclass
class ShapeEvent:
    """Record of a shape-related event."""
    timestamp: datetime
    event_type: str  # 'alloc', 'free', 'use'
    shape_addr: int
    backtrace: List[str]
    details: Dict = field(default_factory=dict)


class ShapeTrackingModule(DebugModule):
    """Track shape lifecycle."""
    
    name = "shape_tracker"
    description = "Track shape allocation and freeing"
    
    def __init__(self, session, config=None):
        super().__init__(session, config)
        self.shapes: Dict[int, JSShape] = {}
        self.events: List[ShapeEvent] = []
        self._freed_shapes: set = set()
        self._alloc_count = 0
        self._free_count = 0
        self._state = {
            'shapes_tracked': 0,
            'alloc_count': 0,
            'free_count': 0,
            'freed_shapes': 0,
        }
    
    def setup(self):
        """Set up shape tracking breakpoints."""
        configs = [
            BreakpointConfig(
                name="js_new_shape_nohash",
                on_hit=self._on_shape_alloc,
                auto_continue=True
            ),
            BreakpointConfig(
                name="js_free_shape0",
                on_hit=self._on_shape_free,
                auto_continue=True
            ),
        ]
        self.session.breakpoint_manager.add_multi(configs)
        self.log("Shape tracking enabled")
    
    def _on_shape_alloc(self, frame, bp_loc):
        """Handle shape allocation."""
        import struct
        import time
        
        # Get return value (x0 on ARM64)
        thread = frame.GetThread()
        ret_val = int(frame.FindRegister('x0').GetValue(), 0)
        
        if ret_val < 0x1000:
            return False
        
        # Read shape
        shape = JSShape.from_memory(self.session.memory_reader, ret_val)
        if shape:
            self.shapes[ret_val] = shape
            self._alloc_count += 1
            
            # Record event
            bt = [f.GetFunctionName() or "???" for f in thread.frames[:5]]
            self.events.append(ShapeEvent(
                timestamp=datetime.now(),
                event_type='alloc',
                shape_addr=ret_val,
                backtrace=bt,
                details={'prop_size': shape.prop_size}
            ))
            
            self._update_state()
            self.log(f"Shape allocated: 0x{ret_val:x}")
        
        return False
    
    def _on_shape_free(self, frame, bp_loc):
        """Handle shape free."""
        thread = frame.GetThread()
        shape_addr = int(frame.FindRegister('x1').GetValue(), 0)
        
        self._freed_shapes.add(shape_addr)
        if shape_addr in self.shapes:
            del self.shapes[shape_addr]
        
        self._free_count += 1
        
        # Record event
        bt = [f.GetFunctionName() or "???" for f in thread.frames[:5]]
        self.events.append(ShapeEvent(
            timestamp=datetime.now(),
            event_type='free',
            shape_addr=shape_addr,
            backtrace=bt
        ))
        
        self._update_state()
        self.log(f"Shape freed: 0x{shape_addr:x}")
        
        return False
    
    def _update_state(self):
        """Update state dict."""
        self._state['shapes_tracked'] = len(self.shapes)
        self._state['alloc_count'] = self._alloc_count
        self._state['free_count'] = self._free_count
        self._state['freed_shapes'] = len(self._freed_shapes)
    
    def is_shape_freed(self, shape_addr: int) -> bool:
        """Check if a shape has been freed."""
        return shape_addr in self._freed_shapes
    
    def get_events_for_shape(self, shape_addr: int) -> List[ShapeEvent]:
        """Get all events for a specific shape."""
        return [e for e in self.events if e.shape_addr == shape_addr]
    
    def get_stats(self) -> Dict:
        """Get tracking statistics."""
        return self._state.copy()
