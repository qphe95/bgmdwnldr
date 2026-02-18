"""
Detect memory corruption patterns.
"""

from modules.base import DebugModule, ModuleConfig
from lib.quickjs.corruption import CorruptionDetector, CorruptionType
from lib.quickjs.types import JSObject
from lib.debug.breakpoints import BreakpointConfig


class MemoryCorruptionModule(DebugModule):
    """Detect and analyze memory corruption."""
    
    name = "corruption_detector"
    description = "Detect shape corruption and other memory issues"
    
    def __init__(self, session, config=None):
        super().__init__(session, config)
        self.detector = CorruptionDetector()
        self.corruption_count = 0
        self._stopped_on_corruption = False
        self._state = {
            'corruption_count': 0,
            'last_corruption': None,
            'stopped': False,
        }
    
    def setup(self):
        """Set up corruption detection at critical points."""
        configs = [
            BreakpointConfig(
                name="find_own_property",
                on_hit=self._check_at_find_own_property
            ),
            BreakpointConfig(
                name="JS_SetPropertyStr",
                on_hit=self._check_at_set_property
            ),
            BreakpointConfig(
                name="JS_SetPropertyInternal",
                on_hit=self._check_at_set_property_internal
            ),
        ]
        self.session.breakpoint_manager.add_multi(configs)
        self.log("Corruption detection enabled")
    
    def _check_at_find_own_property(self, frame, bp_loc) -> bool:
        """Check for corruption at find_own_property entry."""
        thread = frame.GetThread()
        process = thread.GetProcess()
        
        psh = int(frame.FindRegister("x0").GetValue(), 16)
        
        if psh < 0x1000:
            self._report_corruption("find_own_property", "psh is NULL", psh)
            return self.config.stop_on_event
        
        error = process.ReadPointerFromMemory(psh, error)
        if not error.Success():
            self._report_corruption("find_own_property", "Cannot read obj pointer", psh)
            return self.config.stop_on_event
        
        obj_ptr = error
        
        if obj_ptr < 0x1000:
            self._report_corruption("find_own_property", "obj is NULL", obj_ptr)
            return self.config.stop_on_event
        
        # Read and check object
        obj = JSObject.from_memory(self.session.memory_reader, obj_ptr)
        if obj:
            corruption = self.detector.check_object(obj)
            if corruption:
                self._handle_corruption(corruption, frame)
                return self.config.stop_on_event
        
        return False
    
    def _check_at_set_property(self, frame, bp_loc) -> bool:
        """Check for corruption when setting properties."""
        thread = frame.GetThread()
        process = thread.GetProcess()
        
        obj_val = int(frame.FindRegister("x1").GetValue(), 16)
        prop_ptr = int(frame.FindRegister("x2").GetValue(), 16)
        
        # Read property name
        error = process.ReadMemory(prop_ptr, 64, error)
        prop_name = "?"
        if error.Success() and error:
            try:
                prop_name = error.split(b'\x00')[0].decode('utf-8')
            except:
                pass
        
        if obj_val < 0x1000:
            return False
        
        obj = JSObject.from_memory(self.session.memory_reader, obj_val)
        if obj:
            corruption = self.detector.check_object(obj)
            if corruption:
                corruption.description = f"In JS_SetPropertyStr('{prop_name}'): {corruption.description}"
                self._handle_corruption(corruption, frame)
                return self.config.stop_on_event
        
        return False
    
    def _check_at_set_property_internal(self, frame, bp_loc) -> bool:
        """Check for corruption in SetPropertyInternal."""
        obj_val = int(frame.FindRegister("x1").GetValue(), 16)
        
        if obj_val < 0x1000:
            return False
        
        obj = JSObject.from_memory(self.session.memory_reader, obj_val)
        if obj:
            corruption = self.detector.check_object(obj)
            if corruption:
                self._handle_corruption(corruption, frame)
                return self.config.stop_on_event
        
        return False
    
    def _handle_corruption(self, corruption, frame):
        """Handle detected corruption."""
        self.corruption_count += 1
        self._stopped_on_corruption = True
        
        thread = frame.GetThread()
        
        print("\n" + "=" * 70)
        print("!!! SHAPE CORRUPTION DETECTED !!!")
        print("=" * 70)
        print(f"Type: {corruption.type.value}")
        print(f"Object: 0x{corruption.object_addr:x}")
        print(f"Shape value: 0x{corruption.shape_value:x}")
        print(f"Description: {corruption.description}")
        print("\nBacktrace:")
        for i, f in enumerate(thread.frames[:10]):
            func = f.GetFunctionName() or "???"
            print(f"  #{i}: {func}")
        print("=" * 70 + "\n")
        
        self._state['corruption_count'] = self.corruption_count
        self._state['last_corruption'] = corruption
        self._state['stopped'] = True
    
    def _report_corruption(self, location: str, reason: str, value: int):
        """Report corruption details."""
        print(f"\n[{location}] Corruption detected: {reason}")
        print(f"  Value: 0x{value:x}")
