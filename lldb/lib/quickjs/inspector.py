"""
High-level object inspection utilities.
"""

from typing import List, Dict, Optional, Iterator, Tuple
from dataclasses import dataclass, field
from lib.quickjs.types import JSObject, JSShape
from lib.quickjs.corruption import CorruptionDetector, CorruptionReport
from lib.quickjs.constants import JSObjectOffset, MIN_VALID_PTR, MAX_VALID_PTR, is_valid_pointer


@dataclass
class InspectionResult:
    """Result of object inspection."""
    object: JSObject
    shape: Optional[JSShape]
    corruption: Optional[CorruptionReport]
    memory_context: Dict[int, bytes] = field(default_factory=dict)
    nearby_objects: List[JSObject] = field(default_factory=list)
    backtrace: Optional[List[str]] = None
    
    def is_corrupted(self) -> bool:
        """Check if object is corrupted."""
        return self.corruption is not None
    
    def summary(self) -> str:
        """Get human-readable summary."""
        lines = [
            f"JSObject at 0x{self.object.addr:x}:",
            f"  class_id: {self.object.class_id}",
            f"  shape: 0x{self.object.shape:x} ({self.object.get_shape_status()})",
            f"  prop: 0x{self.object.prop:x}",
        ]
        
        if self.shape:
            lines.append(f"  JSShape: 0x{self.shape.addr:x}")
            lines.append(f"    prop_count: {self.shape.prop_count}")
            lines.append(f"    prop_size: {self.shape.prop_size}")
        
        if self.corruption:
            lines.append(f"  CORRUPTION: {self.corruption.type.value}")
            lines.append(f"    {self.corruption.description}")
        
        return "\n".join(lines)


class ObjectInspector:
    """Inspect QuickJS objects in memory."""
    
    def __init__(self, reader):
        from lib.core.memory import MemoryReader
        self.reader: MemoryReader = reader
        self.corruption = CorruptionDetector()
    
    def inspect_object(self, addr: int, depth: int = 1,
                       get_context: bool = True) -> Optional[InspectionResult]:
        """Deep inspection of an object."""
        obj = JSObject.from_memory(self.reader, addr)
        if not obj:
            return None
        
        # Get shape if valid
        shape = None
        if not obj.has_corrupted_shape():
            shape = obj.get_shape(self.reader)
        
        # Check corruption
        corruption = self.corruption.check_object(obj)
        
        # Memory context
        memory_context = {}
        if get_context:
            for offset in [-64, -32, -16, 0, 16, 32, 64]:
                ctx_addr = addr + offset
                if ctx_addr >= MIN_VALID_PTR:
                    data = self.reader.read(ctx_addr, 16, use_cache=False)
                    if data:
                        memory_context[offset] = data
        
        # Find nearby objects
        nearby = []
        if get_context:
            nearby = self.find_nearby_objects(addr, radius=1024, max_count=5)
        
        return InspectionResult(
            object=obj,
            shape=shape,
            corruption=corruption,
            memory_context=memory_context,
            nearby_objects=nearby
        )
    
    def find_objects_with_shape(self, shape_addr: int,
                                 search_start: int = None,
                                 search_size: int = 100 * 1024 * 1024,
                                 max_count: int = 100) -> List[int]:
        """Find all objects pointing to a given shape."""
        # This is expensive - scan memory for pointer values
        from lib.core.memory import MemoryScanner
        scanner = MemoryScanner(self.reader)
        
        # Search in heap regions
        regions = self.reader.read_memory_regions()
        found = []
        
        for region in regions:
            if not region.readable:
                continue
            if region.executable and not region.writable:
                continue  # Skip code sections
            
            # Look for the shape pointer at JSObject shape offset
            # This means finding pointers at offset +8 that match
            ptrs = scanner.scan_for_pointers(region.start, region.end, shape_addr)
            
            for ptr_addr in ptrs:
                # Check if this looks like a JSObject (shape at offset 8)
                obj_addr = ptr_addr - JSObjectOffset.SHAPE
                if obj_addr >= MIN_VALID_PTR:
                    obj = JSObject.from_memory(self.reader, obj_addr)
                    if obj and obj.is_valid():
                        found.append(obj_addr)
                        if len(found) >= max_count:
                            return found
        
        return found
    
    def find_nearby_objects(self, addr: int, radius: int = 4096,
                            max_count: int = 10) -> List[JSObject]:
        """Find valid objects near an address."""
        found = []
        
        for offset in range(-radius, radius + 1, 8):
            test_addr = addr + offset
            if test_addr < MIN_VALID_PTR:
                continue
            
            obj = JSObject.from_memory(self.reader, test_addr)
            if obj and obj.is_valid():
                found.append(obj)
                if len(found) >= max_count:
                    break
        
        return found
    
    def scan_heap_for_objects(self, max_count: int = 100) -> Iterator[JSObject]:
        """Scan heap for all objects."""
        regions = self.reader.read_memory_regions()
        count = 0
        
        for region in regions:
            if not region.readable or region.executable:
                continue
            
            # Scan in chunks
            chunk_size = 64 * 1024
            for chunk_start in range(region.start, region.end, chunk_size):
                chunk_end = min(chunk_start + chunk_size, region.end)
                
                for addr in range(chunk_start, chunk_end - JSObjectOffset.SIZE, 8):
                    obj = JSObject.from_memory(self.reader, addr)
                    if obj and obj.is_valid():
                        yield obj
                        count += 1
                        if count >= max_count:
                            return
    
    def analyze_corruption(self, obj: JSObject) -> Optional[CorruptionReport]:
        """Analyze corruption details."""
        return self.corruption.check_object(obj)
    
    def get_object_backtrace(self, thread) -> List[str]:
        """Get backtrace for current thread."""
        bt = []
        for i, frame in enumerate(thread.frames):
            func_name = frame.GetFunctionName() or "???"
            file_spec = frame.GetLineEntry().GetFileSpec()
            file_name = file_spec.GetFilename() or "???"
            line = frame.GetLineEntry().GetLine()
            bt.append(f"#{i}: {func_name} at {file_name}:{line}")
        return bt
    
    def hex_dump_memory(self, addr: int, size: int = 64) -> str:
        """Create hex dump of memory."""
        data = self.reader.read(addr, size, use_cache=False)
        if not data:
            return f"Cannot read memory at 0x{addr:x}"
        
        lines = []
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_part = ' '.join(f'{b:02x}' for b in chunk)
            hex_part = hex_part.ljust(48)
            ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            lines.append(f"  0x{addr+i:08x}: {hex_part} {ascii_part}")
        
        return "\n".join(lines)
    
    def dump_object(self, addr: int) -> str:
        """Full object dump including shape."""
        result = self.inspect_object(addr, get_context=True)
        if not result:
            return f"Cannot read object at 0x{addr:x}"
        
        lines = [
            "=" * 70,
            f"Object Dump: 0x{addr:x}",
            "=" * 70,
            "",
            result.summary(),
            "",
        ]
        
        if result.memory_context:
            lines.append("Memory Context:")
            for offset, data in sorted(result.memory_context.items()):
                hex_str = ' '.join(f'{b:02x}' for b in data[:8])
                marker = " <-- OBJECT" if offset == 0 else ""
                lines.append(f"  {offset:+4d}: 0x{addr+offset:08x}: {hex_str}{marker}")
            lines.append("")
        
        if result.nearby_objects:
            lines.append(f"Nearby Objects ({len(result.nearby_objects)} found):")
            for obj in result.nearby_objects[:5]:
                offset = obj.addr - addr
                lines.append(f"  {offset:+6d}: 0x{obj.addr:x} class={obj.class_id} shape=0x{obj.shape:x}")
            lines.append("")
        
        if result.corruption:
            lines.append("=" * 70)
            lines.append("CORRUPTION DETECTED!")
            lines.append("=" * 70)
            lines.append(f"Type: {result.corruption.type.value}")
            lines.append(f"Severity: {result.corruption.severity}")
            lines.append(f"Description: {result.corruption.description}")
            if result.corruption.expected_range:
                lines.append(f"Expected range: {result.corruption.expected_range}")
        
        return "\n".join(lines)
