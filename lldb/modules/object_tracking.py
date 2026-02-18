"""
Track JSObject lifecycle and shape changes.
"""

from typing import Dict, List, Optional
from dataclasses import dataclass, field
import time
from modules.base import DebugModule, ModuleConfig
from lib.quickjs.types import JSObject
from lib.debug.breakpoints import BreakpointConfig


@dataclass
class ObjectHistory:
    """History of an object's shape changes."""
    alloc_addr: int
    alloc_shape: int
    alloc_time: float
    events: List[Dict] = field(default_factory=list)
    freed: bool = False
    freed_time: Optional[float] = None


class ObjectTrackingModule(DebugModule):
    """Track object creation and shape evolution."""
    
    name = "object_tracker"
    description = "Track JSObject lifecycle"
    
    def __init__(self, session, config=None):
        super().__init__(session, config)
        self.objects: Dict[int, ObjectHistory] = {}
        self._pending_allocs: Dict[int, Dict] = {}
        self._alloc_count = 0
        self._free_count = 0
        self._state = {
            'objects_tracked': 0,
            'alloc_count': 0,
            'free_count': 0,
        }
    
    def setup(self):
        """Set up object tracking breakpoints."""
        configs = [
            BreakpointConfig("JS_NewObjectFromShape", self._on_object_create, auto_continue=True),
            BreakpointConfig("JS_NewObject", self._on_object_create_simple, auto_continue=True),
            BreakpointConfig("js_free_object", self._on_object_free, auto_continue=True),
            BreakpointConfig("add_property", self._on_shape_change, auto_continue=True),
        ]
        self.session.breakpoint_manager.add_multi(configs)
        self.log("Object tracking enabled")
    
    def _on_object_create(self, frame, bp_loc):
        """Handle object creation."""
        thread = frame.GetThread()
        process = thread.GetProcess()
        
        # Get return value (x0 on ARM64)
        ret_val = int(frame.FindRegister('x0').GetValue(), 0)
        
        if ret_val < 0x1000:
            return False
        
        # Read object
        obj = JSObject.from_memory(self.session.memory_reader, ret_val)
        if obj and obj.is_valid():
            self.objects[ret_val] = ObjectHistory(
                alloc_addr=ret_val,
                alloc_shape=obj.shape,
                alloc_time=time.time(),
                events=[{'type': 'create', 'shape': obj.shape, 'time': time.time()}]
            )
            self._alloc_count += 1
            self._update_state()
            self.log(f"Object created: 0x{ret_val:x} class={obj.class_id} shape=0x{obj.shape:x}")
        
        return False
    
    def _on_object_create_simple(self, frame, bp_loc):
        """Handle simple object creation."""
        return self._on_object_create(frame, bp_loc)
    
    def _on_object_free(self, frame, bp_loc):
        """Handle object free."""
        obj_addr = int(frame.FindRegister('x1').GetValue(), 16)
        
        if obj_addr in self.objects:
            self.objects[obj_addr].freed = True
            self.objects[obj_addr].freed_time = time.time()
            self._free_count += 1
            self._update_state()
            self.log(f"Object freed: 0x{obj_addr:x}")
        
        return False
    
    def _on_shape_change(self, frame, bp_loc):
        """Handle potential shape change."""
        obj_addr = int(frame.FindRegister("x1").GetValue(), 16)
        
        if obj_addr in self.objects:
            # Re-read object to get new shape
            obj = JSObject.from_memory(self.session.memory_reader, obj_addr)
            if obj:
                self.objects[obj_addr].events.append({
                    'type': 'shape_change',
                    'shape': obj.shape,
                    'time': time.time()
                })
                self.log(f"Object 0x{obj_addr:x} shape change")
        
        return False
    
    def _update_state(self):
        """Update state dict."""
        self._state['objects_tracked'] = len(self.objects)
        self._state['alloc_count'] = self._alloc_count
        self._state['free_count'] = self._free_count
    
    def get_object_history(self, addr: int) -> Optional[ObjectHistory]:
        """Get history for an object."""
        return self.objects.get(addr)
    
    def find_objects_by_shape(self, shape_addr: int) -> List[int]:
        """Find all objects using a given shape."""
        return [
            addr for addr, hist in self.objects.items()
            if hist.alloc_shape == shape_addr
        ]
    
    def get_corrupted_objects(self) -> List[int]:
        """Get objects with corrupted shapes."""
        corrupted = []
        for addr, hist in self.objects.items():
            obj = JSObject.from_memory(self.session.memory_reader, addr)
            if obj and obj.has_corrupted_shape():
                corrupted.append(addr)
        return corrupted
