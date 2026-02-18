"""
Base class for debug modules.
"""

from typing import Dict, Any
import lldb


class ModuleConfig:
    """Configuration for a debug module."""
    
    def __init__(self, enabled: bool = True, verbose: bool = False,
                 stop_on_event: bool = True, max_history: int = 1000,
                 async_mode: bool = False):
        self.enabled = enabled
        self.verbose = verbose
        self.stop_on_event = stop_on_event
        self.max_history = max_history
        self.async_mode = async_mode


class DebugModule:
    """Base class for all debug modules."""
    
    name: str = "base"
    description: str = "Base debug module"
    
    def __init__(self, session, config: ModuleConfig = None):
        self.session = session
        self.config = config or ModuleConfig()
        self._state: Dict[str, Any] = {}
        self._enabled = config.enabled if config else True
    
    def setup(self):
        """Set up breakpoints, watchpoints, etc."""
        raise NotImplementedError(f"{self.__class__.__name__}.setup()")
    
    def teardown(self):
        """Clean up when done."""
        pass
    
    def on_stop(self, frame: lldb.SBFrame) -> bool:
        """Handle stop event. Returns True if should stop."""
        return False
    
    def on_continue(self):
        """Handle continue event."""
        pass
    
    def get_state(self) -> Dict[str, Any]:
        """Get module state for inspection."""
        return self._state
    
    def log(self, msg: str):
        """Log with module prefix."""
        if self.config.verbose:
            print(f"[{self.name}] {msg}")
    
    def is_enabled(self) -> bool:
        """Check if module is enabled."""
        return self._enabled
    
    def enable(self):
        """Enable module."""
        self._enabled = True
    
    def disable(self):
        """Disable module."""
        self._enabled = False


class DebugSession:
    """Manages all debug modules and LLDB state."""
    
    def __init__(self, debugger: lldb.SBDebugger, pid: int = None):
        self.debugger = debugger
        self.target = debugger.GetSelectedTarget()
        self.process = self.target.GetProcess() if self.target else None
        self.pid = pid
        
        self.modules: list = []
        
        # Initialize managers if we have a target
        if self.target:
            from lib.debug.breakpoints import BreakpointManager
            from lib.debug.watchpoints import WatchpointManager
            from lib.core.memory import MemoryReader
            
            self.breakpoint_manager = BreakpointManager(self.target)
            self.watchpoint_manager = WatchpointManager(self.target)
            self.memory_reader = MemoryReader(self.process) if self.process else None
    
    def add_module(self, module):
        """Add a debug module."""
        if not module.is_enabled():
            return
        self.modules.append(module)
        module.setup()
    
    def on_stop(self, frame: lldb.SBFrame) -> bool:
        """Dispatch stop event to all modules."""
        should_stop = False
        for module in self.modules:
            if module.is_enabled() and hasattr(module, 'on_stop'):
                try:
                    if module.on_stop(frame):
                        should_stop = True
                except Exception as e:
                    module.log(f"Error in on_stop: {e}")
        return should_stop
    
    def get_module(self, name: str):
        """Get module by name."""
        for module in self.modules:
            if module.name == name:
                return module
        return None
