"""
Use hardware watchpoints to catch corruption.
"""

from typing import Dict
from .base import DebugModule, ModuleConfig
from ..lib.quickjs.types import JSObject
from ..lib.debug.breakpoints import BreakpointConfig


class WatchpointDebugModule(DebugModule):
    """Set watchpoints on object shapes to catch corruption."""
    
    name = "watchpoint_debug"
    description = "Use hardware watchpoints to detect corruption"
    
    MAX_WATCHPOINTS = 4  # Hardware limit on most ARM64
    
    def __init__(self, session, config=None):
        super().__init__(session, config)
        self.watched_objects: Dict[int, Dict] = {}
        self._watch_count = 0
        self._object_counter = 0
        self._state = {
            'watchpoints_active': 0,
            'objects_watched': 0,
            'corruption_caught': 0,
        }
    
    def setup(self):
        """Set up breakpoint to watch new objects."""
        # Break on object creation
        config = BreakpointConfig(
            name="JS_NewObjectFromShape",
            on_hit=self._on_object_created,
            auto_continue=True
        )
        self.session.breakpoint_manager.add(config)
        self.log("Watchpoint debugging enabled (max 4 watchpoints)")
    
    def _on_object_created(self, frame, bp_loc):
        """Called when JS_NewObjectFromShape returns."""
        thread = frame.GetThread()
        
        # Get return value
        ret_val = int(frame.FindRegister('x0').GetValue(), 0)
        
        if ret_val < 0x1000 or ret_val > 0x0000FFFFFFFFFFFF:
            return False
        
        # Read object
        obj = JSObject.from_memory(self.session.memory_reader, ret_val)
        if not obj or not obj.is_valid():
            return False
        
        # Skip if shape is already invalid
        if obj.has_corrupted_shape():
            print(f"[WatchDebug] Object 0x{ret_val:x} created with INVALID shape!")
            return False
        
        self._object_counter += 1
        obj_id = self._object_counter
        
        self.watched_objects[ret_val] = {
            'id': obj_id,
            'addr': ret_val,
            'original_shape': obj.shape,
            'watchpoint': None,
        }
        
        print(f"[WatchDebug] #{obj_id}: Watching object 0x{ret_val:x} shape=0x{obj.shape:x}")
        
        # Try to set watchpoint
        self._try_set_watchpoint(ret_val, obj_id)
        
        self._state['objects_watched'] = len(self.watched_objects)
        return False
    
    def _try_set_watchpoint(self, obj_addr: int, obj_id: int) -> bool:
        """Try to set watchpoint on an object's shape field."""
        if self._watch_count >= self.MAX_WATCHPOINTS:
            print(f"[WatchDebug] #{obj_id}: No watchpoint slots available")
            return False
        
        from ..lib.quickjs.constants import JSObjectOffset
        shape_field_addr = obj_addr + JSObjectOffset.SHAPE
        
        wp = self.session.watchpoint_manager.add_on_object_shape(
            obj_addr,
            on_hit=self._on_shape_changed
        )
        
        if wp:
            self.watched_objects[obj_addr]['watchpoint'] = wp
            self._watch_count += 1
            self._state['watchpoints_active'] = self._watch_count
            print(f"[WatchDebug] #{obj_id}: Watchpoint set on shape field")
            return True
        else:
            print(f"[WatchDebug] #{obj_id}: Failed to set watchpoint")
            return False
    
    def _on_shape_changed(self, frame, wp_loc):
        """Called when a watched shape field changes."""
        import struct
        
        thread = frame.GetThread()
        process = thread.GetProcess()
        
        # Get watchpoint address
        wp = wp_loc.GetWatchpoint()
        if not wp:
            return False
        
        wp_addr = wp.GetWatchAddress()
        obj_addr = wp_addr - 8  # Shape is at offset 8
        
        # Read new shape value
        error = process.ReadPointerFromMemory(wp_addr, error)
        if not error.Success():
            return False
        
        new_shape = error
        
        # Get object info
        obj_info = self.watched_objects.get(obj_addr)
        if not obj_info:
            return False
        
        original_shape = obj_info['original_shape']
        obj_id = obj_info['id']
        
        print("\n" + "=" * 70)
        print(f"[WatchDebug] #{obj_id}: SHAPE FIELD MODIFIED!")
        print("=" * 70)
        print(f"Object: 0x{obj_addr:x}")
        print(f"Original shape: 0x{original_shape:x}")
        print(f"New shape: 0x{new_shape:x}")
        
        # Check if this is corruption
        if new_shape < 0x1000 or new_shape > 0x0000FFFFFFFFFFFF:
            print("!!! CORRUPTION DETECTED - Invalid shape value !!!")
            self._state['corruption_caught'] += 1
            
            # Print backtrace
            print("\nBacktrace:")
            for i, f in enumerate(thread.frames[:10]):
                func = f.GetFunctionName() or "???"
                pc = f.GetPCAddress().GetLoadAddress(thread.GetProcess().GetTarget())
                print(f"  #{i}: 0x{pc:x} {func}")
            
            print("=" * 70 + "\n")
            return self.config.stop_on_event
        else:
            # Legitimate shape change
            print(f"[WatchDebug] Legitimate shape change")
            obj_info['original_shape'] = new_shape
        
        print("=" * 70 + "\n")
        return False
