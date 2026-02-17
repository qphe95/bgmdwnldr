"""
Watchpoint management utilities.
"""

import lldb
from typing import Callable, Dict, List, Optional
from dataclasses import dataclass


@dataclass
class WatchpointConfig:
    """Configuration for a watchpoint."""
    address: int
    size: int = 8
    read: bool = False
    write: bool = True
    on_hit: Optional[Callable] = None
    condition: Optional[str] = None


class WatchpointManager:
    """Manage hardware watchpoints."""
    
    # Hardware limit on most ARM64 devices
    MAX_WATCHPOINTS = 4
    
    def __init__(self, target: lldb.SBTarget):
        self.target = target
        self._watchpoints: Dict[int, lldb.SBWatchpoint] = {}
        self._handlers: Dict[int, Callable] = {}
        self._watch_count = 0
    
    def can_add_watchpoint(self) -> bool:
        """Check if we can add another watchpoint."""
        return len(self._watchpoints) < self.MAX_WATCHPOINTS
    
    def add(self, config: WatchpointConfig) -> Optional[lldb.SBWatchpoint]:
        """Add a watchpoint."""
        if not self.can_add_watchpoint():
            return None
        
        error = lldb.SBError()
        wp = self.target.WatchAddress(
            config.address,
            config.size,
            config.read,
            config.write,
            error
        )
        
        if error.Fail() or not wp.IsValid():
            return None
        
        self._watchpoints[wp.GetID()] = wp
        
        if config.on_hit:
            wp.SetScriptCallbackFunction(self._get_callback_name(config.on_hit))
            self._handlers[wp.GetID()] = config.on_hit
        
        if config.condition:
            wp.SetCondition(config.condition)
        
        return wp
    
    def add_on_object_shape(self, obj_addr: int,
                           on_hit: Callable = None) -> Optional[lldb.SBWatchpoint]:
        """Add watchpoint on an object's shape field."""
        from ..quickjs.constants import JSObjectOffset
        
        shape_field_addr = obj_addr + JSObjectOffset.SHAPE
        
        config = WatchpointConfig(
            address=shape_field_addr,
            size=8,
            read=False,
            write=True,
            on_hit=on_hit
        )
        
        return self.add(config)
    
    def remove(self, wp_id: int):
        """Remove a watchpoint by ID."""
        if wp_id in self._watchpoints:
            self.target.DeleteWatchpoint(wp_id)
            del self._watchpoints[wp_id]
            if wp_id in self._handlers:
                del self._handlers[wp_id]
    
    def remove_all(self):
        """Remove all watchpoints."""
        for wp_id in list(self._watchpoints.keys()):
            self.remove(wp_id)
    
    def get(self, wp_id: int) -> Optional[lldb.SBWatchpoint]:
        """Get watchpoint by ID."""
        return self._watchpoints.get(wp_id)
    
    def list_all(self) -> Dict[int, lldb.SBWatchpoint]:
        """Get all watchpoints."""
        return self._watchpoints.copy()
    
    def count(self) -> int:
        """Get number of active watchpoints."""
        return len(self._watchpoints)
    
    def is_full(self) -> bool:
        """Check if at watchpoint limit."""
        return len(self._watchpoints) >= self.MAX_WATCHPOINTS
    
    def on_stop(self, frame: lldb.SBFrame, wp_loc) -> bool:
        """Handle watchpoint stop event."""
        wp = wp_loc.GetWatchpoint()
        if not wp:
            return False
        
        wp_id = wp.GetID()
        if wp_id in self._handlers:
            return self._handlers[wp_id](frame, wp_loc, {})
        
        return True  # Stop by default
    
    def _get_callback_name(self, func: Callable) -> str:
        """Get fully qualified name for callback function."""
        module = func.__module__
        name = func.__name__
        return f"{module}.{name}"
