"""
Declarative breakpoint management with inline Python support.
"""

import lldb
from typing import Callable, Dict, List, Optional, Union
from dataclasses import dataclass
from enum import Enum


class BreakpointEvent(Enum):
    """Breakpoint event types."""
    ENTRY = "entry"
    RETURN = "return"
    CONDITION = "condition"


@dataclass
class BreakpointConfig:
    """Configuration for a breakpoint."""
    name: str
    on_hit: Optional[Callable] = None
    condition: Optional[str] = None  # LLDB condition expression
    python_condition: Optional[str] = None  # Inline Python code
    auto_continue: bool = False
    hit_count: int = 0
    command_on_hit: Optional[str] = None  # LLDB command to run
    is_address: bool = False  # If True, name is an address


class BreakpointManager:
    """Manage multiple breakpoints with callbacks."""
    
    def __init__(self, target: lldb.SBTarget):
        self.target = target
        self._breakpoints: Dict[str, lldb.SBBreakpoint] = {}
        self._handlers: Dict[str, Callable] = {}
        self._configs: Dict[str, BreakpointConfig] = {}
    
    def add(self, config: BreakpointConfig) -> lldb.SBBreakpoint:
        """Add a breakpoint from configuration."""
        if config.is_address:
            # Set by address
            bp = self.target.BreakpointCreateByAddress(int(config.name, 0))
        else:
            # Set by name
            bp = self.target.BreakpointCreateByName(config.name)
        
        if not bp.IsValid():
            return bp
        
        # Store config
        self._configs[config.name] = config
        self._breakpoints[config.name] = bp
        
        # Set condition
        if config.condition:
            bp.SetCondition(config.condition)
        
        # Set callback
        if config.on_hit:
            bp.SetScriptCallbackFunction(self._get_callback_name(config.on_hit))
            self._handlers[config.name] = config.on_hit
        
        # Set auto-continue
        if config.auto_continue:
            bp.SetAutoContinue(True)
        
        # Add command
        if config.command_on_hit:
            bp.SetCommandLineCommands([config.command_on_hit])
        
        # Set inline Python if provided
        if config.python_condition:
            self._set_inline_python(bp, config.python_condition)
        
        return bp
    
    def add_multi(self, configs: List[BreakpointConfig]) -> List[lldb.SBBreakpoint]:
        """Add multiple breakpoints."""
        return [self.add(c) for c in configs]
    
    def add_with_inline_python(self, name: str, python_code: str,
                               condition: str = None,
                               is_address: bool = False) -> lldb.SBBreakpoint:
        """Add breakpoint with inline Python handler.
        
        Equivalent to:
        breakpoint command add -s python
        <python_code>
        DONE
        """
        if is_address:
            bp = self.target.BreakpointCreateByAddress(int(name, 0))
        else:
            bp = self.target.BreakpointCreateByName(name)
        
        if not bp.IsValid():
            return bp
        
        if condition:
            bp.SetCondition(condition)
        
        self._set_inline_python(bp, python_code)
        
        self._breakpoints[name] = bp
        return bp
    
    def _set_inline_python(self, bp: lldb.SBBreakpoint, python_code: str):
        """Set inline Python code for breakpoint."""
        # In LLDB, we use the command line interface for this
        # The python_code should be the body of the callback
        # This is a simplified version - full implementation would
        # need to use LLDB's Python API properly
        
        # For now, we create a wrapper function and set it as callback
        import hashlib
        func_name = f"_inline_bp_{hashlib.md5(python_code.encode()).hexdigest()[:8]}"
        
        # Create the function dynamically
        exec_globals = {}
        exec(f"""
def {func_name}(frame, bp_loc, dict):
    from __main__ import lldb
    process = frame.GetThread().GetProcess()
    thread = frame.GetThread()
{self._indent_code(python_code)}
    return False
""", exec_globals)
        
        # Store in globals so LLDB can find it
        import __main__
        setattr(__main__, func_name, exec_globals[func_name])
        
        bp.SetScriptCallbackFunction(f"__main__.{func_name}")
    
    def _indent_code(self, code: str, indent: int = 4) -> str:
        """Indent code block."""
        lines = code.strip().split('\n')
        spaces = ' ' * indent
        return '\n'.join(spaces + line for line in lines)
    
    def _get_callback_name(self, func: Callable) -> str:
        """Get fully qualified name for callback function."""
        module = func.__module__
        name = func.__name__
        return f"{module}.{name}"
    
    def remove(self, name: str):
        """Remove a breakpoint."""
        if name in self._breakpoints:
            self.target.BreakpointDelete(self._breakpoints[name].GetID())
            del self._breakpoints[name]
            del self._handlers[name]
            del self._configs[name]
    
    def enable(self, name: str):
        """Enable a breakpoint."""
        if name in self._breakpoints:
            self._breakpoints[name].SetEnabled(True)
    
    def disable(self, name: str):
        """Disable a breakpoint."""
        if name in self._breakpoints[name]:
            self._breakpoints[name].SetEnabled(False)
    
    def get(self, name: str) -> Optional[lldb.SBBreakpoint]:
        """Get breakpoint by name."""
        return self._breakpoints.get(name)
    
    def list_all(self) -> Dict[str, lldb.SBBreakpoint]:
        """Get all breakpoints."""
        return self._breakpoints.copy()
    
    def on_stop(self, frame: lldb.SBFrame) -> bool:
        """Handle stop event, returns True if should stop."""
        thread = frame.GetThread()
        process = thread.GetProcess()
        
        # Find which breakpoint was hit
        for bp in self._breakpoints.values():
            if bp.IsValid():
                for loc in bp:
                    if loc.IsValid() and loc.GetAddress() == frame.GetPCAddress():
                        # This breakpoint was hit
                        config = self._configs.get(bp.GetName())
                        if config and config.on_hit:
                            return config.on_hit(frame, loc, {})
        
        return False
    
    def set_pending(self, name: str, pending: bool = True):
        """Set breakpoint as pending (wait for symbol to load)."""
        if name in self._breakpoints:
            # In LLDB, pending breakpoints are created differently
            # This is a placeholder
            pass
