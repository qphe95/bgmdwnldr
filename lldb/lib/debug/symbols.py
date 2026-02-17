"""
Support for debugging stripped binaries via address calculation.
"""

import subprocess
import time
from typing import Optional, Dict, List
from dataclasses import dataclass


@dataclass
class FunctionAddress:
    """Resolved function address."""
    name: str
    offset: int  # Offset from library base
    absolute: int  # Absolute address


class StrippedBinaryHelper:
    """Helper for debugging stripped binaries."""
    
    # Known offsets from tombstone analysis
    # These would be populated from actual tombstone files
    KNOWN_OFFSETS = {
        'init_browser_stubs': 0xa1f30,
        'JS_SetPropertyStr': 0xb775c,
        'JS_SetPropertyInternal': 0xb51d4,
        'JS_DefineProperty': 0xb7700,
        'find_own_property': 0xa5000,
        'js_new_shape_nohash': 0x90000,
        'js_free_shape0': 0x91000,
        'JS_NewObjectFromShape': 0x92000,
    }
    
    def __init__(self, pid: int, lib_name: str = "libminimalvulkan.so"):
        self.pid = pid
        self.lib_name = lib_name
        self.base_addr: Optional[int] = None
        self._cached_offsets: Dict[str, int] = {}
    
    def get_library_base(self) -> Optional[int]:
        """Get library base address from /proc/pid/maps."""
        try:
            result = subprocess.run(
                ['adb', 'shell', f'cat /proc/{self.pid}/maps | grep {self.lib_name} | head -1'],
                capture_output=True,
                text=True,
                shell=False
            )
            
            if result.returncode != 0 or not result.stdout:
                return None
            
            line = result.stdout.strip()
            # Format: b4000077b2e8d000-b4000077b2f5c000 r--p 00000000 ...
            base_str = line.split('-')[0]
            self.base_addr = int(base_str, 16)
            return self.base_addr
            
        except (ValueError, subprocess.SubprocessError) as e:
            print(f"Error getting library base: {e}")
            return None
    
    def wait_for_library_load(self, timeout: int = 30,
                              poll_interval: float = 0.5) -> bool:
        """Poll /proc/pid/maps until library appears."""
        start = time.time()
        while time.time() - start < timeout:
            base = self.get_library_base()
            if base is not None:
                return True
            time.sleep(poll_interval)
        return False
    
    def calculate_address(self, function_name: str) -> Optional[FunctionAddress]:
        """Calculate absolute address for function."""
        if function_name not in self.KNOWN_OFFSETS:
            return None
        
        if self.base_addr is None:
            self.base_addr = self.get_library_base()
        
        if self.base_addr is None:
            return None
        
        offset = self.KNOWN_OFFSETS[function_name]
        absolute = self.base_addr + offset
        
        return FunctionAddress(
            name=function_name,
            offset=offset,
            absolute=absolute
        )
    
    def set_address_breakpoint(self, target, function_name: str) -> bool:
        """Set breakpoint by calculated address."""
        addr_info = self.calculate_address(function_name)
        if not addr_info:
            return False
        
        import lldb
        bp = target.BreakpointCreateByAddress(addr_info.absolute)
        return bp.IsValid()
    
    def setup_all_breakpoints(self, target) -> List[str]:
        """Setup breakpoints for all known functions."""
        successful = []
        
        for func_name in self.KNOWN_OFFSETS.keys():
            if self.set_address_breakpoint(target, func_name):
                successful.append(func_name)
        
        return successful
    
    def get_loaded_libraries(self) -> List[str]:
        """Get list of loaded libraries from /proc/pid/maps."""
        try:
            result = subprocess.run(
                ['adb', 'shell', f'cat /proc/{self.pid}/maps'],
                capture_output=True,
                text=True
            )
            
            if result.returncode != 0:
                return []
            
            libs = set()
            for line in result.stdout.split('\n'):
                if '/' in line:
                    path = line.split('/')[-1].strip()
                    if path and path.endswith('.so'):
                        libs.add(path)
            
            return sorted(libs)
            
        except subprocess.SubprocessError:
            return []
    
    def find_function_in_library(self, function_pattern: str) -> Optional[str]:
        """Try to find a function that matches pattern in loaded library."""
        # This is for when we don't know exact offsets
        # Could use nm/objdump on host side symbols
        return None
