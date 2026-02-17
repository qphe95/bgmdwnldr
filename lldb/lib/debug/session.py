"""
Debug session management.
"""

import lldb
from typing import Dict, Any, List, Optional, Callable
from dataclasses import dataclass


@dataclass
class ModuleConfig:
    """Configuration for a debug module."""
    enabled: bool = True
    verbose: bool = False
    stop_on_event: bool = True
    max_history: int = 1000
    async_mode: bool = False  # For ANR prevention


class DebugModule:
    """Base class for all debug modules."""
    
    name: str = "base"
    description: str = "Base debug module"
    
    def __init__(self, session: 'DebugSession', config: ModuleConfig = None):
        self.session = session
        self.config = config or ModuleConfig()
        self._state: Dict[str, Any] = {}
        self._enabled = config.enabled if config else True
    
    def setup(self):
        """Set up breakpoints, watchpoints, etc."""
        raise NotImplementedError
    
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
        
        self.modules: List[DebugModule] = []
        self._event_handlers: List[Callable] = []
        
        # Initialize managers if we have a target
        if self.target:
            from .breakpoints import BreakpointManager
            from .watchpoints import WatchpointManager
            from ..core.memory import MemoryReader
            
            self.breakpoint_manager = BreakpointManager(self.target)
            self.watchpoint_manager = WatchpointManager(self.target)
            self.memory_reader = MemoryReader(self.process) if self.process else None
    
    def add_module(self, module: DebugModule):
        """Add a debug module."""
        if not module.is_enabled():
            return
        
        self.modules.append(module)
        module.setup()
    
    def remove_module(self, module: DebugModule):
        """Remove a debug module."""
        if module in self.modules:
            module.teardown()
            self.modules.remove(module)
    
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
    
    def on_continue(self):
        """Dispatch continue event to all modules."""
        for module in self.modules:
            if module.is_enabled() and hasattr(module, 'on_continue'):
                try:
                    module.on_continue()
                except Exception as e:
                    module.log(f"Error in on_continue: {e}")
    
    def continue_execution(self):
        """Continue execution."""
        self.on_continue()
        if self.process:
            self.process.Continue()
    
    def get_module(self, name: str) -> Optional[DebugModule]:
        """Get module by name."""
        for module in self.modules:
            if module.name == name:
                return module
        return None
    
    def get_all_state(self) -> Dict[str, Dict[str, Any]]:
        """Get state from all modules."""
        return {
            module.name: module.get_state()
            for module in self.modules
        }
    
    def print_status(self):
        """Print status of all modules."""
        print("=" * 70)
        print("Debug Session Status")
        print("=" * 70)
        print(f"PID: {self.pid}")
        print(f"Modules: {len(self.modules)}")
        
        for module in self.modules:
            status = "enabled" if module.is_enabled() else "disabled"
            print(f"\n[{module.name}] ({status})")
            print(f"  {module.description}")
            state = module.get_state()
            if state:
                for key, val in state.items():
                    print(f"  {key}: {val}")
    
    def setup_async(self, async_mode: bool = True):
        """Setup async mode to prevent ANR."""
        self.debugger.SetAsync(async_mode)
