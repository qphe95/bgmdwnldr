"""
Analyze crash state when process stops unexpectedly.
"""

from .base import DebugModule, ModuleConfig
from ..lib.quickjs.types import JSObject
from ..lib.quickjs.inspector import ObjectInspector


class CrashAnalysisModule(DebugModule):
    """Analyze crash state and provide detailed report."""
    
    name = "crash_analyzer"
    description = "Analyze crash state and provide detailed report"
    
    def __init__(self, session, config=None):
        super().__init__(session, config)
        self.inspector = None
        if session.memory_reader:
            self.inspector = ObjectInspector(session.memory_reader)
        self._crash_count = 0
        self._state = {
            'crashes_detected': 0,
            'last_analysis': None,
        }
    
    def setup(self):
        """Setup SIGSEGV handling."""
        if self.session.process:
            # Handle SIGSEGV
            signals = self.session.process.GetUnixSignals()
            signals.SetShouldStop(11, True)   # Stop on SIGSEGV
            signals.SetShouldNotify(11, True)
        self.log("Crash analysis enabled")
    
    def on_stop(self, frame) -> bool:
        """Analyze stop reason."""
        if not self.session.process:
            return False
        
        thread = self.session.process.GetSelectedThread()
        stop_reason = thread.GetStopReason()
        
        if stop_reason == lldb.eStopReasonSignal:
            sig = thread.GetStopReasonDataAtIndex(0)
            if sig == 11:  # SIGSEGV
                self._analyze_crash(frame, "SIGSEGV")
                return True
        
        elif stop_reason == lldb.eStopReasonException:
            self._analyze_crash(frame, "Exception")
            return True
        
        return False
    
    def _analyze_crash(self, frame, crash_type: str):
        """Perform crash analysis."""
        self._crash_count += 1
        thread = frame.GetThread()
        process = thread.GetProcess()
        
        print("\n" + "=" * 70)
        print(f"CRASH ANALYSIS - {crash_type}")
        print("=" * 70)
        
        # Current location
        print(f"\nCrashed in: {frame.GetFunctionName()}")
        print(f"PC: {frame.GetPCAddress()}")
        
        # Registers
        print("\n--- Registers ---")
        for reg_name in ['x0', 'x1', 'x2', 'x3', 'x4', 'lr', 'sp', 'pc']:
            reg = frame.FindRegister(reg_name)
            print(f"  {reg_name}: {reg.GetValue()}")
        
        # If in find_own_property, analyze the object
        func_name = frame.GetFunctionName() or ""
        if 'find_own_property' in func_name:
            self._analyze_find_own_property(frame)
        elif 'JS_SetProperty' in func_name:
            self._analyze_set_property(frame)
        
        # Backtrace
        print("\n--- Backtrace ---")
        for i, f in enumerate(thread.frames[:12]):
            func = f.GetFunctionName() or "???"
            pc = f.GetPCAddress().GetLoadAddress(thread.GetProcess().GetTarget())
            print(f"  #{i}: 0x{pc:x} {func}")
        
        print("=" * 70 + "\n")
        
        self._state['crashes_detected'] = self._crash_count
        self._state['last_analysis'] = {
            'type': crash_type,
            'function': func_name,
        }
    
    def _analyze_find_own_property(self, frame):
        """Analyze find_own_property crash."""
        import struct
        thread = frame.GetThread()
        process = thread.GetProcess()
        
        psh = int(frame.FindRegister("x0").GetValue(), 16)
        print(f"\n--- find_own_property Analysis ---")
        print(f"psh (JSObject**) = 0x{psh:x}")
        
        if psh > 0x1000:
            error = lldb.SBError()
            obj_ptr = process.ReadPointerFromMemory(psh, error)
            if error.Success():
                print(f"JSObject* = 0x{obj_ptr:x}")
                
                # Read shape
                shape_ptr = process.ReadPointerFromMemory(obj_ptr + 8, error)
                if error.Success():
                    print(f"shape = 0x{shape_ptr:x}")
                    
                    if shape_ptr < 0x1000:
                        print("!!! INVALID SHAPE - This is the corruption !!!")
                        
                        # Try to interpret as tagged value
                        tag = shape_ptr & 0xF
                        tags = {
                            1: 'JS_TAG_INT/UNDEFINED',
                            3: 'JS_TAG_EXCEPTION',
                            4: 'JS_TAG_UNDEFINED',
                            5: 'JS_TAG_NULL',
                            6: 'JS_TAG_BOOL',
                            7: 'JS_TAG_EXCEPTION2',
                        }
                        print(f"    -> Looks like tagged value: {tags.get(tag, 'UNKNOWN')}")
    
    def _analyze_set_property(self, frame):
        """Analyze JS_SetProperty crash."""
        obj_val = int(frame.FindRegister("x1").GetValue(), 16)
        
        if obj_val > 0x1000 and self.inspector:
            print(f"\n--- Object Analysis ---")
            result = self.inspector.inspect_object(obj_val, get_context=True)
            if result:
                print(result.summary())
