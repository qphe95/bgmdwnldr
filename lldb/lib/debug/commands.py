"""
Simplified command registration.
"""

import lldb
from typing import Callable, Dict, List
from dataclasses import dataclass


@dataclass
class CommandSpec:
    """Specification for a command."""
    name: str
    handler: Callable
    help_short: str
    help_long: str = ""


class CommandRegistry:
    """Register LLDB commands with less boilerplate."""
    
    def __init__(self, debugger: lldb.SBDebugger):
        self.debugger = debugger
        self._commands: Dict[str, CommandSpec] = {}
    
    def register(self, spec: CommandSpec):
        """Register a single command."""
        self._commands[spec.name] = spec
        
        # Create the LLDB command
        cmd = f"command script add -f {self._get_func_path(spec.handler)} {spec.name}"
        self.debugger.HandleCommand(cmd)
    
    def register_multi(self, specs: List[CommandSpec]):
        """Register multiple commands."""
        for spec in specs:
            self.register(spec)
    
    def create_alias(self, alias: str, command: str):
        """Create command alias."""
        self.debugger.HandleCommand(f"command alias {alias} {command}")
    
    def get(self, name: str) -> Optional[CommandSpec]:
        """Get command spec by name."""
        return self._commands.get(name)
    
    def list_all(self) -> List[CommandSpec]:
        """List all registered commands."""
        return list(self._commands.values())
    
    def unregister(self, name: str):
        """Unregister a command."""
        if name in self._commands:
            self.debugger.HandleCommand(f"command script delete {name}")
            del self._commands[name]
    
    def _get_func_path(self, func: Callable) -> str:
        """Get fully qualified function path."""
        module = func.__module__
        name = func.__name__
        return f"{module}.{name}"
    
    def print_help(self):
        """Print help for all commands."""
        print("Available commands:")
        for spec in self._commands.values():
            print(f"  {spec.name:20} - {spec.help_short}")


# Decorator for easy command registration
_commands_to_register = []


def command(name: str, help_short: str = "", help_long: str = ""):
    """Decorator to mark a function as an LLDB command."""
    def decorator(func):
        func._lldb_command = True
        func._cmd_name = name
        func._cmd_help_short = help_short
        func._cmd_help_long = help_long
        _commands_to_register.append(CommandSpec(
            name=name,
            handler=func,
            help_short=help_short,
            help_long=help_long
        ))
        return func
    return decorator


def auto_register(debugger: lldb.SBDebugger):
    """Auto-register all @command decorated functions."""
    registry = CommandRegistry(debugger)
    
    # Get all functions that have been decorated
    for spec in _commands_to_register:
        registry.register(spec)
    
    return registry
