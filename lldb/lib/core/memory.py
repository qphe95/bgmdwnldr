"""
Memory reading/writing utilities with caching and error handling.
"""

import lldb
import struct
from typing import Optional, List, Dict, Tuple
from dataclasses import dataclass
from collections import OrderedDict


@dataclass
class MemoryRegion:
    """Represents a memory region."""
    start: int
    end: int
    readable: bool
    writable: bool
    executable: bool


class MemoryCache:
    """LRU cache for memory reads."""
    
    def __init__(self, max_size: int = 1024 * 1024):  # 1MB default
        self.max_size = max_size
        self.cache: OrderedDict[int, bytes] = OrderedDict()
        self.current_size = 0
    
    def get(self, addr: int, size: int) -> Optional[bytes]:
        """Get from cache if available."""
        # Check if entire range is cached
        result = bytearray()
        for offset in range(0, size, 64):  # Check in 64-byte chunks
            chunk_addr = addr + offset
            if chunk_addr not in self.cache:
                return None
            result.extend(self.cache[chunk_addr])
        return bytes(result[:size])
    
    def put(self, addr: int, data: bytes):
        """Add to cache with LRU eviction."""
        # Store in 64-byte chunks
        for offset in range(0, len(data), 64):
            chunk_addr = addr + offset
            chunk = data[offset:offset + 64]
            
            if chunk_addr in self.cache:
                self.current_size -= len(self.cache[chunk_addr])
            
            self.cache[chunk_addr] = chunk
            self.current_size += len(chunk)
            
            # LRU eviction
            while self.current_size > self.max_size and self.cache:
                oldest = next(iter(self.cache))
                self.current_size -= len(self.cache[oldest])
                del self.cache[oldest]
    
    def invalidate(self, addr: int = None):
        """Invalidate cache entry or entire cache."""
        if addr is None:
            self.cache.clear()
            self.current_size = 0
        else:
            if addr in self.cache:
                self.current_size -= len(self.cache[addr])
                del self.cache[addr]


class MemoryReader:
    """Efficient memory reading with caching."""
    
    def __init__(self, process: lldb.SBProcess, cache_size: int = 1024 * 1024):
        self.process = process
        self.cache = MemoryCache(cache_size)
    
    def read(self, addr: int, size: int, use_cache: bool = True) -> Optional[bytes]:
        """Read memory with optional caching."""
        if addr == 0 or size == 0:
            return None
        
        # Check cache first
        if use_cache:
            cached = self.cache.get(addr, size)
            if cached is not None:
                return cached
        
        # Read from process
        error = lldb.SBError()
        data = self.process.ReadMemory(addr, size, error)
        
        if error.Success() and data:
            if use_cache:
                self.cache.put(addr, data)
            return data
        return None
    
    def read_u64(self, addr: int) -> Optional[int]:
        """Read 8 bytes as uint64."""
        data = self.read(addr, 8)
        if data and len(data) == 8:
            return struct.unpack('<Q', data)[0]
        return None
    
    def read_u32(self, addr: int) -> Optional[int]:
        """Read 4 bytes as uint32."""
        data = self.read(addr, 4)
        if data and len(data) == 4:
            return struct.unpack('<I', data)[0]
        return None
    
    def read_u16(self, addr: int) -> Optional[int]:
        """Read 2 bytes as uint16."""
        data = self.read(addr, 2)
        if data and len(data) == 2:
            return struct.unpack('<H', data)[0]
        return None
    
    def read_u8(self, addr: int) -> Optional[int]:
        """Read 1 byte as uint8."""
        data = self.read(addr, 1)
        if data and len(data) == 1:
            return data[0]
        return None
    
    def read_pointer(self, addr: int) -> Optional[int]:
        """Read pointer-sized value (8 bytes on ARM64)."""
        return self.read_u64(addr)
    
    def read_c_string(self, addr: int, max_len: int = 256) -> Optional[str]:
        """Read null-terminated C string."""
        if addr == 0:
            return None
        
        error = lldb.SBError()
        # Read up to max_len bytes
        data = self.process.ReadMemory(addr, max_len, error)
        if not error.Success() or not data:
            return None
        
        # Find null terminator
        null_pos = data.find(b'\x00')
        if null_pos != -1:
            data = data[:null_pos]
        
        try:
            return data.decode('utf-8')
        except UnicodeDecodeError:
            return data.decode('latin-1', errors='replace')
    
    def read_memory_regions(self) -> List[MemoryRegion]:
        """Get list of memory regions."""
        regions = []
        region = lldb.SBMemoryRegionInfo()
        addr = 0
        
        while self.process.GetMemoryRegionInfo(addr, region):
            start = region.GetRegionBase()
            end = region.GetRegionEnd()
            
            regions.append(MemoryRegion(
                start=start,
                end=end,
                readable=region.IsReadable(),
                writable=region.IsWritable(),
                executable=region.IsExecutable()
            ))
            
            addr = end
            if addr == 0:
                break
        
        return regions
    
    def clear_cache(self):
        """Clear the memory cache."""
        self.cache.invalidate()


class MemoryScanner:
    """Scan memory for patterns."""
    
    def __init__(self, reader: MemoryReader):
        self.reader = reader
    
    def scan_for_pointers(self, start: int, end: int, target: int) -> List[int]:
        """Find all occurrences of a pointer value."""
        found = []
        target_bytes = struct.pack('<Q', target)
        
        # Read in chunks to avoid large allocations
        chunk_size = 64 * 1024  # 64KB chunks
        for chunk_start in range(start, end, chunk_size):
            chunk_end = min(chunk_start + chunk_size, end)
            data = self.reader.read(chunk_start, chunk_end - chunk_start, use_cache=False)
            
            if data:
                offset = 0
                while True:
                    idx = data.find(target_bytes, offset)
                    if idx == -1:
                        break
                    found.append(chunk_start + idx)
                    offset = idx + 1
        
        return found
    
    def scan_for_pattern(self, start: int, end: int, pattern: bytes) -> List[int]:
        """Find all occurrences of byte pattern."""
        found = []
        chunk_size = 64 * 1024
        
        for chunk_start in range(start, end, chunk_size):
            chunk_end = min(chunk_start + chunk_size, end)
            data = self.reader.read(chunk_start, chunk_end - chunk_start, use_cache=False)
            
            if data:
                offset = 0
                while True:
                    idx = data.find(pattern, offset)
                    if idx == -1:
                        break
                    found.append(chunk_start + idx)
                    offset = idx + 1
        
        return found
    
    def scan_for_valid_pointers(self, start: int, end: int,
                                 min_val: int = 0x1000,
                                 max_val: int = 0x0000FFFFFFFFFFFF) -> List[Tuple[int, int]]:
        """Scan for values that look like valid pointers."""
        found = []
        chunk_size = 64 * 1024
        
        for chunk_start in range(start, end, chunk_size):
            chunk_end = min(chunk_start + chunk_size, end)
            data = self.reader.read(chunk_start, chunk_end - chunk_start, use_cache=False)
            
            if data and len(data) >= 8:
                # Unpack as uint64 array
                num_qwords = len(data) // 8
                for i in range(num_qwords):
                    val = struct.unpack_from('<Q', data, i * 8)[0]
                    if min_val <= val <= max_val:
                        found.append((chunk_start + i * 8, val))
        
        return found
