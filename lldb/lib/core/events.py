"""
Event handling framework for LLDB debugging.
"""

import lldb
from typing import Callable, Dict, List, Any
from enum import Enum, auto
from dataclasses import dataclass


class EventType(Enum):
    """Types of debugging events."""
    PROCESS_START = auto()
    PROCESS_STOP = auto()
    PROCESS_CONTINUE = auto()
    BREAKPOINT_HIT = auto()
    WATCHPOINT_HIT = auto()
    SIGNAL_RECEIVED = auto()
    EXCEPTION_OCCURRED = auto()
    STEP_COMPLETED = auto()
    MODULE_LOADED = auto()
    THREAD_CREATED = auto()
    THREAD_EXITED = auto()


@dataclass
class DebugEvent:
    """A debugging event."""
    type: EventType
    timestamp: float
    process: lldb.SBProcess
    thread: lldb.SBThread
    frame: lldb.SBFrame
    data: Dict[str, Any]


class EventHandler:
    """Base class for event handlers."""
    
    def __init__(self, event_type: EventType = None):
        self.event_type = event_type
        self._enabled = True
    
    def can_handle(self, event: DebugEvent) -> bool:
        """Check if this handler can handle the event."""
        if not self._enabled:
            return False
        if self.event_type is None:
            return True
        return event.type == self.event_type
    
    def handle(self, event: DebugEvent) -> bool:
        """Handle the event. Returns True if handled."""
        raise NotImplementedError
    
    def enable(self):
        """Enable handler."""
        self._enabled = True
    
    def disable(self):
        """Disable handler."""
        self._enabled = False
    
    def is_enabled(self) -> bool:
        """Check if handler is enabled."""
        return self._enabled


class EventDispatcher:
    """Dispatches debugging events to registered handlers."""
    
    def __init__(self):
        self._handlers: Dict[EventType, List[EventHandler]] = {}
        self._global_handlers: List[EventHandler] = []
        self._event_history: List[DebugEvent] = []
        self._max_history = 1000
    
    def register(self, handler: EventHandler):
        """Register an event handler."""
        if handler.event_type is None:
            self._global_handlers.append(handler)
        else:
            if handler.event_type not in self._handlers:
                self._handlers[handler.event_type] = []
            self._handlers[handler.event_type].append(handler)
    
    def unregister(self, handler: EventHandler):
        """Unregister an event handler."""
        if handler.event_type is None:
            if handler in self._global_handlers:
                self._global_handlers.remove(handler)
        else:
            if handler.event_type in self._handlers:
                if handler in self._handlers[handler.event_type]:
                    self._handlers[handler.event_type].remove(handler)
    
    def dispatch(self, event: DebugEvent) -> bool:
        """Dispatch event to handlers. Returns True if handled."""
        # Add to history
        self._event_history.append(event)
        if len(self._event_history) > self._max_history:
            self._event_history.pop(0)
        
        handled = False
        
        # Dispatch to type-specific handlers
        if event.type in self._handlers:
            for handler in self._handlers[event.type]:
                if handler.can_handle(event):
                    try:
                        if handler.handle(event):
                            handled = True
                    except Exception as e:
                        print(f"Error in event handler: {e}")
        
        # Dispatch to global handlers
        for handler in self._global_handlers:
            if handler.can_handle(event):
                try:
                    if handler.handle(event):
                        handled = True
                except Exception as e:
                    print(f"Error in global event handler: {e}")
        
        return handled
    
    def create_event(self, event_type: EventType, process: lldb.SBProcess,
                    data: Dict[str, Any] = None) -> DebugEvent:
        """Create a debug event."""
        import time
        thread = process.GetSelectedThread()
        frame = thread.GetSelectedFrame()
        
        return DebugEvent(
            type=event_type,
            timestamp=time.time(),
            process=process,
            thread=thread,
            frame=frame,
            data=data or {}
        )
    
    def get_history(self, event_type: EventType = None,
                   limit: int = 100) -> List[DebugEvent]:
        """Get event history."""
        if event_type is None:
            return self._event_history[-limit:]
        
        return [e for e in self._event_history if e.type == event_type][-limit:]
    
    def clear_history(self):
        """Clear event history."""
        self._event_history.clear()


class CorruptionEventHandler(EventHandler):
    """Handler for corruption detection events."""
    
    def __init__(self, callback: Callable[[DebugEvent], None] = None):
        super().__init__(EventType.BREAKPOINT_HIT)
        self.callback = callback
        self.corruption_count = 0
    
    def handle(self, event: DebugEvent) -> bool:
        """Check for corruption at breakpoint."""
        # Check if we're at a corruption-prone location
        frame = event.frame
        func_name = frame.GetFunctionName() or ""
        
        if 'find_own_property' in func_name or 'JS_SetProperty' in func_name:
            # This would integrate with corruption detector
            if self.callback:
                self.callback(event)
        
        return False


class LoggingEventHandler(EventHandler):
    """Handler that logs all events."""
    
    def __init__(self, verbose: bool = False):
        super().__init__(None)  # Handle all events
        self.verbose = verbose
    
    def handle(self, event: DebugEvent) -> bool:
        """Log the event."""
        if self.verbose:
            print(f"[{event.type.name}] {event.frame.GetFunctionName()}")
        return False


class BreakpointEventFilter(EventHandler):
    """Filter events by breakpoint location."""
    
    def __init__(self, function_names: List[str]):
        super().__init__(EventType.BREAKPOINT_HIT)
        self.function_names = function_names
    
    def can_handle(self, event: DebugEvent) -> bool:
        """Check if breakpoint is at one of our target functions."""
        if not super().can_handle(event):
            return False
        
        func_name = event.frame.GetFunctionName() or ""
        return any(name in func_name for name in self.function_names)
    
    def handle(self, event: DebugEvent) -> bool:
        """Handle the filtered event."""
        # Override in subclass
        return False
