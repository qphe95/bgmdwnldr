"""
ARM64 register access utilities.
"""

import lldb
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from enum import IntEnum


class ARM64Reg(IntEnum):
    """ARM64 register enumeration."""
    X0 = 0
    X1 = 1
    X2 = 2
    X3 = 3
    X4 = 4
    X5 = 5
    X6 = 6
    X7 = 7
    X8 = 8
    X9 = 9
    X10 = 10
    X11 = 11
    X12 = 12
    X13 = 13
    X14 = 14
    X15 = 15
    X16 = 16
    X17 = 17
    X18 = 18
    X19 = 19
    X20 = 20
    X21 = 21
    X22 = 22
    X23 = 23
    X24 = 24
    X25 = 25
    X26 = 26
    X27 = 27
    X28 = 28
    FP = 29  # Frame pointer (x29)
    LR = 30  # Link register (x30)
    SP = 31  # Stack pointer
    PC = 32  # Program counter


@dataclass
class RegisterSet:
    """Complete ARM64 register set."""
    x: List[int]  # x0-x28
    fp: int
    lr: int
    sp: int
    pc: int
    
    @classmethod
    def from_frame(cls, frame: lldb.SBFrame) -> 'RegisterSet':
        """Capture all registers from frame."""
        x = []
        for i in range(29):  # x0-x28
            reg = frame.FindRegister(f'x{i}')
            try:
                val = int(reg.GetValue(), 0)
            except (ValueError, TypeError):
                val = 0
            x.append(val)
        
        fp = cls._read_reg(frame, 'fp')
        lr = cls._read_reg(frame, 'lr')
        sp = cls._read_reg(frame, 'sp')
        pc = cls._read_reg(frame, 'pc')
        
        return cls(x=x, fp=fp, lr=lr, sp=sp, pc=pc)
    
    @staticmethod
    def _read_reg(frame: lldb.SBFrame, name: str) -> int:
        """Read a register value safely."""
        try:
            reg = frame.FindRegister(name)
            return int(reg.GetValue(), 0)
        except (ValueError, TypeError):
            return 0
    
    def get(self, reg: ARM64Reg) -> int:
        """Get register value by enum."""
        if reg <= ARM64Reg.X28:
            return self.x[reg]
        elif reg == ARM64Reg.FP:
            return self.fp
        elif reg == ARM64Reg.LR:
            return self.lr
        elif reg == ARM64Reg.SP:
            return self.sp
        elif reg == ARM64Reg.PC:
            return self.pc
        return 0
    
    def get_by_name(self, name: str) -> int:
        """Get register value by name (e.g., 'x0', 'lr')."""
        name = name.lower()
        
        if name.startswith('x') and name[1:].isdigit():
            idx = int(name[1:])
            if 0 <= idx <= 28:
                return self.x[idx]
        
        if name in ('fp', 'x29'):
            return self.fp
        if name in ('lr', 'x30'):
            return self.lr
        if name == 'sp':
            return self.sp
        if name == 'pc':
            return self.pc
        
        return 0
    
    def to_dict(self) -> Dict[str, int]:
        """Convert to dictionary."""
        result = {f'x{i}': v for i, v in enumerate(self.x)}
        result['fp'] = self.fp
        result['lr'] = self.lr
        result['sp'] = self.sp
        result['pc'] = self.pc
        return result


class RegisterMonitor:
    """Monitor register changes across execution."""
    
    def __init__(self, track_registers: List[str] = None):
        self.track_registers = track_registers or [f'x{i}' for i in range(29)] + ['fp', 'lr', 'sp', 'pc']
        self._history: List[Tuple[int, RegisterSet]] = []
        self._checkpoints: Dict[str, int] = {}
    
    def capture(self, frame: lldb.SBFrame) -> RegisterSet:
        """Capture current register state."""
        reg_set = RegisterSet.from_frame(frame)
        self._history.append((len(self._history), reg_set))
        return reg_set
    
    def checkpoint(self, name: str = "default"):
        """Mark current position as checkpoint."""
        self._checkpoints[name] = len(self._history)
    
    def get_changes(self, since: str = "default") -> Dict[str, Tuple[int, int]]:
        """Get registers that changed since checkpoint."""
        if since not in self._checkpoints:
            return {}
        
        idx = self._checkpoints[since]
        if idx >= len(self._history):
            return {}
        
        old = self._history[idx][1]
        new = self._history[-1][1]
        
        changes = {}
        for i in range(29):
            if old.x[i] != new.x[i]:
                changes[f'x{i}'] = (old.x[i], new.x[i])
        
        if old.fp != new.fp:
            changes['fp'] = (old.fp, new.fp)
        if old.lr != new.lr:
            changes['lr'] = (old.lr, new.lr)
        if old.sp != new.sp:
            changes['sp'] = (old.sp, new.sp)
        if old.pc != new.pc:
            changes['pc'] = (old.pc, new.pc)
        
        return changes
    
    def find_suspicious_values(self,
                               patterns: List[int] = None,
                               frame: lldb.SBFrame = None) -> Dict[str, int]:
        """Find registers containing suspicious values."""
        if patterns is None:
            patterns = [
                0xc0000000,
                0xc0000008,
                0xFFFFFFFFFFFFFFFF,
                0xFFFFFFFFFFFFFFFE,
            ]
        
        if frame is not None:
            reg_set = RegisterSet.from_frame(frame)
        elif self._history:
            reg_set = self._history[-1][1]
        else:
            return {}
        
        suspicious = {}
        reg_dict = reg_set.to_dict()
        
        for name, val in reg_dict.items():
            for pattern in patterns:
                if val == pattern:
                    suspicious[name] = val
                    break
            # Also check for tagged pointer patterns (low bits set, high bits zero)
            if val & 0xF and (val >> 4) == 0:
                if name not in suspicious:
                    suspicious[name] = val
        
        return suspicious
    
    def get_history(self) -> List[Tuple[int, RegisterSet]]:
        """Get full register history."""
        return self._history.copy()
    
    def get_register_history(self, reg_name: str) -> List[int]:
        """Get history of a specific register."""
        history = []
        for _, reg_set in self._history:
            history.append(reg_set.get_by_name(reg_name))
        return history
    
    def clear(self):
        """Clear history."""
        self._history.clear()
        self._checkpoints.clear()


def get_register(frame: lldb.SBFrame, name: str) -> int:
    """Get a single register value safely."""
    try:
        reg = frame.FindRegister(name)
        return int(reg.GetValue(), 0)
    except (ValueError, TypeError, AttributeError):
        return 0


def get_registers(frame: lldb.SBFrame, names: List[str]) -> Dict[str, int]:
    """Get multiple register values."""
    return {name: get_register(frame, name) for name in names}


def get_argument_registers(frame: lldb.SBFrame, count: int = 8) -> Dict[str, int]:
    """Get ARM64 argument registers (x0-x7)."""
    return get_registers(frame, [f'x{i}' for i in range(count)])
