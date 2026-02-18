"""
QuickJS data structure representations.
"""

from dataclasses import dataclass
from typing import Optional, TYPE_CHECKING
from enum import IntEnum

from lib.quickjs.constants import (
    JSObjectOffset,
    JSShapeOffset,
    JSValueTag,
    MIN_VALID_PTR,
    MAX_VALID_PTR,
    is_valid_pointer,
    looks_like_tagged_value,
    decode_js_tag,
)

if TYPE_CHECKING:
    from lib.core.memory import MemoryReader


@dataclass
class JSObject:
    """Parsed JSObject structure."""
    addr: int
    class_id: int
    flags: int
    weakref_count: int
    shape: int
    prop: int
    
    @classmethod
    def from_memory(cls, reader: 'MemoryReader', addr: int) -> Optional['JSObject']:
        """Parse JSObject from memory."""
        if addr < MIN_VALID_PTR:
            return None
        
        # Read object header (24 bytes minimum)
        data = reader.read(addr, JSObjectOffset.SIZE, use_cache=False)
        if not data or len(data) < JSObjectOffset.SIZE:
            return None
        
        import struct
        class_id = struct.unpack_from('<H', data, JSObjectOffset.CLASS_ID)[0]
        flags = struct.unpack_from('<B', data, JSObjectOffset.FLAGS)[0]
        weakref_count = struct.unpack_from('<I', data, JSObjectOffset.WEAKREF_COUNT)[0]
        shape = struct.unpack_from('<Q', data, JSObjectOffset.SHAPE)[0]
        prop = struct.unpack_from('<Q', data, JSObjectOffset.PROP)[0]
        
        return cls(
            addr=addr,
            class_id=class_id,
            flags=flags,
            weakref_count=weakref_count,
            shape=shape,
            prop=prop
        )
    
    def is_valid(self) -> bool:
        """Check if object looks valid."""
        # class_id should be in reasonable range (1-255 typically)
        if self.class_id < 1 or self.class_id > 255:
            return False
        return True
    
    def has_corrupted_shape(self) -> bool:
        """Check if shape pointer is corrupted."""
        if self.shape == 0:
            return True  # NULL shape
        if self.shape == 0xFFFFFFFFFFFFFFFF:
            return True  # -1 / JS_TAG_OBJECT
        if self.shape < MIN_VALID_PTR:
            return True  # Looks like tagged value
        if self.shape > MAX_VALID_PTR:
            return True  # Out of range
        return False
    
    def get_shape(self, reader: 'MemoryReader') -> Optional['JSShape']:
        """Get associated shape object."""
        if self.has_corrupted_shape():
            return None
        return JSShape.from_memory(reader, self.shape)
    
    def get_shape_status(self) -> str:
        """Get human-readable shape status."""
        if self.shape == 0:
            return "NULL"
        if self.shape == 0xFFFFFFFFFFFFFFFF:
            return "CORRUPTED (-1)"
        if self.shape < MIN_VALID_PTR:
            return f"CORRUPTED (tagged: {decode_js_tag(self.shape)})"
        if self.shape > MAX_VALID_PTR:
            return "CORRUPTED (out of range)"
        return f"VALID (0x{self.shape:x})"
    
    def __repr__(self) -> str:
        return (f"JSObject(0x{self.addr:x}, class={self.class_id}, "
                f"shape=0x{self.shape:x}, prop=0x{self.prop:x})")


@dataclass
class JSShape:
    """Parsed JSShape structure."""
    addr: int
    is_hashed: bool
    hash_val: int
    prop_hash_mask: int
    prop_size: int
    prop_count: int
    deleted_prop_count: int
    shape_hash_next: int
    proto: int
    
    @classmethod
    def from_memory(cls, reader: 'MemoryReader', addr: int) -> Optional['JSShape']:
        """Parse JSShape from memory."""
        if not is_valid_pointer(addr):
            return None
        
        # Read shape header (40 bytes)
        data = reader.read(addr, JSShapeOffset.SIZE, use_cache=False)
        if not data or len(data) < JSShapeOffset.SIZE:
            return None
        
        import struct
        is_hashed = bool(data[JSShapeOffset.IS_HASHED])
        hash_val = struct.unpack_from('<I', data, JSShapeOffset.HASH)[0]
        prop_hash_mask = struct.unpack_from('<I', data, JSShapeOffset.PROP_HASH_MASK)[0]
        prop_size = struct.unpack_from('<i', data, JSShapeOffset.PROP_SIZE)[0]
        prop_count = struct.unpack_from('<i', data, JSShapeOffset.PROP_COUNT)[0]
        deleted_prop_count = struct.unpack_from('<i', data, JSShapeOffset.DELETED_PROP_COUNT)[0]
        shape_hash_next = struct.unpack_from('<Q', data, JSShapeOffset.SHAPE_HASH_NEXT)[0]
        proto = struct.unpack_from('<Q', data, JSShapeOffset.PROTO)[0]
        
        return cls(
            addr=addr,
            is_hashed=is_hashed,
            hash_val=hash_val,
            prop_hash_mask=prop_hash_mask,
            prop_size=prop_size,
            prop_count=prop_count,
            deleted_prop_count=deleted_prop_count,
            shape_hash_next=shape_hash_next,
            proto=proto
        )
    
    def is_valid(self) -> bool:
        """Check if shape looks valid."""
        # prop_count should be <= prop_size
        if self.prop_count < 0 or self.prop_size < 0:
            return False
        if self.prop_count > self.prop_size:
            return False
        # prop_hash_mask should be power of 2 minus 1, or 0
        if self.prop_hash_mask != 0 and (self.prop_hash_mask & (self.prop_hash_mask + 1)) != 0:
            return False
        return True
    
    def __repr__(self) -> str:
        return (f"JSShape(0x{self.addr:x}, hash=0x{self.hash_val:x}, "
                f"props={self.prop_count}/{self.prop_size})")


@dataclass
class JSValue:
    """Parsed JSValue structure."""
    addr: int  # Address of JSValue, not the pointer value
    u: int     # Union value (ptr or number)
    tag: int   # Tag (int64)
    
    @classmethod
    def from_memory(cls, reader: 'MemoryReader', addr: int) -> Optional['JSValue']:
        """Parse JSValue from memory."""
        if addr < MIN_VALID_PTR:
            return None
        
        # JSValue is 16 bytes: 8 bytes union + 8 bytes tag
        data = reader.read(addr, 16, use_cache=False)
        if not data or len(data) < 16:
            return None
        
        import struct
        u = struct.unpack_from('<Q', data, 0)[0]
        tag = struct.unpack_from('<q', data, 8)[0]  # Signed int64
        
        return cls(addr=addr, u=u, tag=tag)
    
    def get_type(self) -> str:
        """Get human-readable type."""
        if self.tag < 0:
            return "JS_TAG_OBJECT (reference)"
        
        tag_names = {
            JSValueTag.INT: "JS_TAG_INT",
            JSValueTag.UNDEFINED: "JS_TAG_UNDEFINED",
            JSValueTag.NULL: "JS_TAG_NULL",
            JSValueTag.BOOL: "JS_TAG_BOOL",
            JSValueTag.EXCEPTION: "JS_TAG_EXCEPTION",
            JSValueTag.FLOAT64: "JS_TAG_FLOAT64",
        }
        return tag_names.get(self.tag, f"JS_TAG_UNKNOWN({self.tag})")
    
    def is_object(self) -> bool:
        """Check if this is an object reference."""
        return self.tag < 0  # Negative tags are object references
    
    def is_int(self) -> bool:
        """Check if this is an integer."""
        return self.tag == JSValueTag.INT
    
    def get_object_ptr(self) -> Optional[int]:
        """Get object pointer if this is an object reference."""
        if self.is_object():
            return self.u
        return None
    
    def __repr__(self) -> str:
        return f"JSValue({self.get_type()}, u=0x{self.u:x})"


@dataclass
class JSContext:
    """Minimal JSContext representation."""
    addr: int
    
    @classmethod
    def from_register(cls, frame) -> Optional['JSContext']:
        """Get JSContext from x0 register (first argument)."""
        try:
            import lldb
            reg = frame.FindRegister('x0')
            addr = int(reg.GetValue(), 0)
            if addr < MIN_VALID_PTR:
                return None
            return cls(addr=addr)
        except (ValueError, TypeError):
            return None
