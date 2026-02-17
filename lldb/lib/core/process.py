"""
Process control utilities.
"""

import lldb
import time
from typing import Optional, List, Callable
from enum import Enum


class ProcessState(Enum):
    """LLDB process states."""
    INVALID = 0
    UNLOADED = 1
    CONNECTED = 2
    ATTACHING = 3
    LAUNCHING = 4
    STOPPED = 5
    RUNNING = 6
    STEPPING = 7
    CRASHED = 8
    DETACHED = 9
    EXITED = 10
    SUSPENDED = 11


class ProcessControl:
    """High-level process control operations."""
    
    def __init__(self, process: lldb.SBProcess):
        self.process = process
    
    def get_state(self) -> ProcessState:
        """Get current process state."""
        return ProcessState(self.process.GetState())
    
    def is_running(self) -> bool:
        """Check if process is running."""
        return self.process.GetState() == lldb.eStateRunning
    
    def is_stopped(self) -> bool:
        """Check if process is stopped."""
        return self.process.GetState() == lldb.eStateStopped
    
    def is_crashed(self) -> bool:
        """Check if process has crashed."""
        return self.process.GetState() == lldb.eStateCrashed
    
    def is_exited(self) -> bool:
        """Check if process has exited."""
        return self.process.GetState() == lldb.eStateExited
    
    def continue_execution(self) -> bool:
        """Continue process execution."""
        error = self.process.Continue()
        return error.Success()
    
    def stop(self) -> bool:
        """Stop process execution."""
        error = self.process.Stop()
        return error.Success()
    
    def step_instruction(self, into: bool = True) -> bool:
        """Step one instruction."""
        thread = self.process.GetSelectedThread()
        if into:
            error = thread.StepInstruction(True)
        else:
            error = thread.StepOver()
        return error.Success()
    
    def step_out(self) -> bool:
        """Step out of current function."""
        thread = self.process.GetSelectedThread()
        error = thread.StepOut()
        return error.Success()
    
    def get_selected_thread(self) -> lldb.SBThread:
        """Get the currently selected thread."""
        return self.process.GetSelectedThread()
    
    def get_all_threads(self) -> List[lldb.SBThread]:
        """Get all threads."""
        return list(self.process.threads)
    
    def get_thread_by_id(self, tid: int) -> Optional[lldb.SBThread]:
        """Get thread by thread ID."""
        for thread in self.process.threads:
            if thread.GetThreadID() == tid:
                return thread
        return None
    
    def select_thread(self, tid: int) -> bool:
        """Select a thread by ID."""
        thread = self.get_thread_by_id(tid)
        if thread:
            self.process.SetSelectedThread(thread)
            return True
        return False
    
    def wait_for_stop(self, timeout: float = 30.0,
                      poll_interval: float = 0.1) -> bool:
        """Wait for process to stop."""
        start = time.time()
        while time.time() - start < timeout:
            if self.is_stopped() or self.is_crashed() or self.is_exited():
                return True
            time.sleep(poll_interval)
        return False
    
    def wait_for_state(self, target_states: List[ProcessState],
                       timeout: float = 30.0,
                       poll_interval: float = 0.1) -> Optional[ProcessState]:
        """Wait for process to reach one of target states."""
        start = time.time()
        while time.time() - start < timeout:
            state = self.get_state()
            if state in target_states:
                return state
            time.sleep(poll_interval)
        return None
    
    def get_stop_reason(self) -> str:
        """Get human-readable stop reason."""
        if not self.is_stopped():
            return "Not stopped"
        
        thread = self.process.GetSelectedThread()
        reason = thread.GetStopReason()
        
        reason_map = {
            lldb.eStopReasonInvalid: "Invalid",
            lldb.eStopReasonNone: "None",
            lldb.eStopReasonTrace: "Trace",
            lldb.eStopReasonBreakpoint: "Breakpoint",
            lldb.eStopReasonWatchpoint: "Watchpoint",
            lldb.eStopReasonSignal: "Signal",
            lldb.eStopReasonException: "Exception",
            lldb.eStopReasonExec: "Exec",
            lldb.eStopReasonPlanComplete: "Plan Complete",
            lldb.eStopReasonThreadExiting: "Thread Exiting",
            lldb.eStopReasonInstrumentation: "Instrumentation",
        }
        
        return reason_map.get(reason, f"Unknown ({reason})")
    
    def get_stop_description(self, max_len: int = 256) -> str:
        """Get detailed stop description."""
        if not self.is_stopped():
            return "Not stopped"
        
        thread = self.process.GetSelectedThread()
        return thread.GetStopDescription(max_len)
    
    def handle_signals(self, signals: dict):
        """Configure signal handling.
        
        Args:
            signals: Dict mapping signal number to (should_stop, should_notify)
        """
        signals_obj = self.process.GetUnixSignals()
        for signo, (stop, notify) in signals.items():
            signals_obj.SetShouldStop(signo, stop)
            signals_obj.SetShouldNotify(signo, notify)
    
    def kill(self) -> bool:
        """Kill the process."""
        error = self.process.Kill()
        return error.Success()
    
    def detach(self) -> bool:
        """Detach from process."""
        error = self.process.Detach()
        return error.Success()
    
    def read_memory(self, addr: int, size: int) -> Optional[bytes]:
        """Read process memory."""
        error = lldb.SBError()
        data = self.process.ReadMemory(addr, size, error)
        return data if error.Success() else None
    
    def write_memory(self, addr: int, data: bytes) -> bool:
        """Write to process memory."""
        error = lldb.SBError()
        self.process.WriteMemory(addr, data, error)
        return error.Success()


class AsyncProcessController:
    """Async process controller to prevent ANR when attaching."""
    
    def __init__(self, debugger: lldb.SBDebugger, process: lldb.SBProcess):
        self.debugger = debugger
        self.process = process
        self.original_async = debugger.GetAsync()
    
    def enable_async(self):
        """Enable async mode to prevent ANR."""
        self.debugger.SetAsync(True)
    
    def disable_async(self):
        """Disable async mode (blocking)."""
        self.debugger.SetAsync(False)
    
    def __enter__(self):
        """Context manager entry."""
        self.enable_async()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.debugger.SetAsync(self.original_async)
        return False
