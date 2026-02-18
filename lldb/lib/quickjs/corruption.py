"""
Corruption detection logic.
"""

from dataclasses import dataclass
from typing import List, Optional, Tuple, Set
from enum import Enum

from lib.quickjs.types import JSObject
from lib.quickjs.constants import (
    MIN_VALID_PTR,
    MAX_VALID_PTR,
    SUSPICIOUS_PATTERNS,
    is_valid_pointer,
    looks_like_tagged_value,
    decode_js_tag,
)


class CorruptionType(Enum):
    """Types of corruption."""
    NULL_SHAPE = "null_shape"
    INVALID_POINTER = "invalid_pointer"
    TAGGED_VALUE = "tagged_value_as_pointer"
    FREED_SHAPE = "freed_shape"
    SHAPE_CHANGED_UNEXPECTEDLY = "shape_changed"
    USE_AFTER_FREE = "use_after_free"
    OUT_OF_RANGE = "out_of_range"


@dataclass
class CorruptionReport:
    """Detailed corruption report."""
    type: CorruptionType
    object_addr: int
    shape_value: int
    expected_range: Optional[Tuple[int, int]]
    description: str
    severity: str  # 'warning', 'critical'
    recommendation: Optional[str] = None


class CorruptionDetector:
    """Detect various forms of corruption."""
    
    def __init__(self):
        self._freed_shapes: Set[int] = set()
        self._known_good_shapes: Set[int] = set()
        self._object_history: dict = {}  # addr -> {shape, timestamp}
    
    def check_shape(self, shape_ptr: int, obj_addr: int = 0) -> Optional[CorruptionReport]:
        """Check if shape pointer indicates corruption."""
        # NULL shape
        if shape_ptr == 0:
            return CorruptionReport(
                type=CorruptionType.NULL_SHAPE,
                object_addr=obj_addr,
                shape_value=shape_ptr,
                expected_range=(MIN_VALID_PTR, MAX_VALID_PTR),
                description="Shape pointer is NULL (object not initialized or freed)",
                severity="critical",
                recommendation="Check object initialization path"
            )
        
        # -1 / JS_TAG_OBJECT
        if shape_ptr == 0xFFFFFFFFFFFFFFFF:
            return CorruptionReport(
                type=CorruptionType.TAGGED_VALUE,
                object_addr=obj_addr,
                shape_value=shape_ptr,
                expected_range=(MIN_VALID_PTR, MAX_VALID_PTR),
                description="Shape is -1 (looks like JS_TAG_OBJECT), memory corruption likely",
                severity="critical",
                recommendation="Check for buffer overflow or use-after-free"
            )
        
        # Looks like a tagged value (small value with low bits set)
        if looks_like_tagged_value(shape_ptr):
            return CorruptionReport(
                type=CorruptionType.TAGGED_VALUE,
                object_addr=obj_addr,
                shape_value=shape_ptr,
                expected_range=(MIN_VALID_PTR, MAX_VALID_PTR),
                description=f"Shape looks like tagged JSValue: {decode_js_tag(shape_ptr)}",
                severity="critical",
                recommendation="JSValue was written to shape field - check property assignment"
            )
        
        # Small value (likely tagged)
        if shape_ptr < MIN_VALID_PTR:
            return CorruptionReport(
                type=CorruptionType.TAGGED_VALUE,
                object_addr=obj_addr,
                shape_value=shape_ptr,
                expected_range=(MIN_VALID_PTR, MAX_VALID_PTR),
                description=f"Shape is small value (0x{shape_ptr:x}), likely tagged pointer",
                severity="critical"
            )
        
        # Out of range
        if shape_ptr > MAX_VALID_PTR:
            return CorruptionReport(
                type=CorruptionType.OUT_OF_RANGE,
                object_addr=obj_addr,
                shape_value=shape_ptr,
                expected_range=(MIN_VALID_PTR, MAX_VALID_PTR),
                description=f"Shape pointer out of valid range (kernel space?)",
                severity="critical"
            )
        
        # Known suspicious patterns
        if shape_ptr in SUSPICIOUS_PATTERNS:
            return CorruptionReport(
                type=CorruptionType.INVALID_POINTER,
                object_addr=obj_addr,
                shape_value=shape_ptr,
                expected_range=(MIN_VALID_PTR, MAX_VALID_PTR),
                description=f"Shape matches known corruption pattern: 0x{shape_ptr:x}",
                severity="critical"
            )
        
        # Freed shape
        if shape_ptr in self._freed_shapes:
            return CorruptionReport(
                type=CorruptionType.FREED_SHAPE,
                object_addr=obj_addr,
                shape_value=shape_ptr,
                expected_range=None,
                description=f"Shape was freed but is still referenced (use-after-free)",
                severity="critical",
                recommendation="Set watchpoint on shape field to catch when it changes"
            )
        
        return None
    
    def check_object(self, obj: JSObject) -> Optional[CorruptionReport]:
        """Check object for corruption."""
        if not obj:
            return None
        
        # First check if object itself is valid
        if not obj.is_valid():
            return CorruptionReport(
                type=CorruptionType.INVALID_POINTER,
                object_addr=obj.addr,
                shape_value=obj.shape,
                expected_range=(1, 255),
                description=f"Object has invalid class_id: {obj.class_id}",
                severity="warning"
            )
        
        # Check shape
        return self.check_shape(obj.shape, obj.addr)
    
    def check_shape_change(self, obj_addr: int, old_shape: int,
                          new_shape: int) -> Optional[CorruptionReport]:
        """Check if a shape change is suspicious."""
        # Legitimate shape changes happen when properties are added
        # Suspicious if new shape is invalid or freed
        
        report = self.check_shape(new_shape, obj_addr)
        if report:
            report.type = CorruptionType.SHAPE_CHANGED_UNEXPECTEDLY
            report.description = f"Shape changed from 0x{old_shape:x} to invalid: {report.description}"
            return report
        
        return None
    
    def register_freed_shape(self, shape_addr: int):
        """Track freed shapes for use-after-free detection."""
        self._freed_shapes.add(shape_addr)
        if shape_addr in self._known_good_shapes:
            self._known_good_shapes.discard(shape_addr)
    
    def register_good_shape(self, shape_addr: int):
        """Track known-good shapes."""
        self._known_good_shapes.add(shape_addr)
        if shape_addr in self._freed_shapes:
            self._freed_shapes.discard(shape_addr)
    
    def is_shape_freed(self, shape_addr: int) -> bool:
        """Check if a shape has been freed."""
        return shape_addr in self._freed_shapes
    
    def record_object_state(self, obj: JSObject):
        """Record object state for change tracking."""
        import time
        self._object_history[obj.addr] = {
            'shape': obj.shape,
            'timestamp': time.time(),
            'class_id': obj.class_id
        }
    
    def get_object_history(self, addr: int) -> Optional[dict]:
        """Get recorded state for an object."""
        return self._object_history.get(addr)
    
    def get_stats(self) -> dict:
        """Get detector statistics."""
        return {
            'freed_shapes_tracked': len(self._freed_shapes),
            'good_shapes_tracked': len(self._known_good_shapes),
            'objects_tracked': len(self._object_history),
        }
