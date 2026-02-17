#!/usr/bin/env python3
"""
LLDB Server management for shell scripts.

This module provides Python wrappers for lldb-server operations
that can be used by both shell scripts and Python code.
"""

import subprocess
import time
import os
import signal
from typing import Optional, List
from dataclasses import dataclass


@dataclass
class ServerConfig:
    """LLDB server configuration."""
    path: str = "/data/local/tmp/lldb-server"
    port: int = 5039
    listen_addr: str = "*"
    platform: bool = True


class LLDBServer:
    """Manage lldb-server on device."""
    
    def __init__(self, device_serial: str = None, config: ServerConfig = None):
        self.config = config or ServerConfig()
        self.device_serial = device_serial
        self._process: Optional[subprocess.Popen] = None
        self._adb_cmd = ['adb']
        if device_serial:
            self._adb_cmd.extend(['-s', device_serial])
    
    def _adb_shell(self, cmd: str, background: bool = False) -> int:
        """Run adb shell command."""
        full_cmd = self._adb_cmd + ['shell', cmd]
        
        if background:
            # Run in background
            self._process = subprocess.Popen(
                full_cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            return 0
        else:
            result = subprocess.run(full_cmd, capture_output=True)
            return result.returncode
    
    def is_running(self) -> bool:
        """Check if lldb-server is running."""
        result = subprocess.run(
            self._adb_cmd + ['shell', 'ps -A | grep lldb-server'],
            capture_output=True,
            text=True
        )
        return 'lldb-server' in result.stdout
    
    def kill_existing(self) -> bool:
        """Kill any existing lldb-server processes."""
        self._adb_shell("pkill -9 -f lldb-server 2>/dev/null")
        time.sleep(0.5)
        return not self.is_running()
    
    def start(self, background: bool = True, wait: bool = True) -> bool:
        """Start lldb-server."""
        # Kill existing first
        self.kill_existing()
        
        # Build command
        if self.config.platform:
            cmd = (f"{self.config.path} platform "
                   f"--listen '*:{self.config.port}' --server")
        else:
            cmd = (f"{self.config.path} gdbserver "
                   f"--listen '*:{self.config.port}'")
        
        # Start server
        ret = self._adb_shell(cmd, background=background)
        
        if background and wait:
            # Wait for server to be ready
            for _ in range(10):
                if self.is_running():
                    time.sleep(0.5)  # Extra time to initialize
                    return True
                time.sleep(0.5)
            return False
        
        return ret == 0
    
    def stop(self) -> bool:
        """Stop lldb-server."""
        if self._process:
            self._process.terminate()
            try:
                self._process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._process.kill()
            self._process = None
        
        return self.kill_existing()
    
    def setup_port_forward(self, local_port: int = None,
                          remote_port: int = None) -> bool:
        """Setup adb port forwarding."""
        local = local_port or self.config.port
        remote = remote_port or self.config.port
        
        result = subprocess.run(
            self._adb_cmd + ['forward', f'tcp:{local}', f'tcp:{remote}'],
            capture_output=True
        )
        return result.returncode == 0
    
    def remove_port_forward(self, local_port: int = None) -> bool:
        """Remove port forwarding."""
        if local_port:
            result = subprocess.run(
                self._adb_cmd + ['forward', '--remove', f'tcp:{local_port}'],
                capture_output=True
            )
        else:
            result = subprocess.run(
                self._adb_cmd + ['forward', '--remove-all'],
                capture_output=True
            )
        return result.returncode == 0
    
    def push_to_device(self, local_path: str) -> bool:
        """Push lldb-server binary to device."""
        if not os.path.exists(local_path):
            return False
        
        result = subprocess.run(
            self._adb_cmd + ['push', local_path, self.config.path],
            capture_output=True
        )
        
        if result.returncode == 0:
            # Make executable
            self._adb_shell(f"chmod +x {self.config.path}")
            return True
        
        return False
    
    def check_binary(self) -> bool:
        """Check if lldb-server binary exists on device."""
        result = subprocess.run(
            self._adb_cmd + ['shell', f'test -f {self.config.path} && echo yes'],
            capture_output=True,
            text=True
        )
        return 'yes' in result.stdout
    
    def find_ndk_lldb_server(self, ndk_path: str = None) -> Optional[str]:
        """Find lldb-server in NDK installation."""
        import glob
        
        # Common NDK paths
        search_paths = []
        if ndk_path:
            search_paths.append(ndk_path)
        
        search_paths.extend([
            os.path.expanduser("~/Library/Android/sdk/ndk"),
            os.path.expanduser("~/Android/Sdk/ndk"),
            "/usr/local/lib/android/sdk/ndk",
        ])
        
        for base_path in search_paths:
            if not os.path.exists(base_path):
                continue
            
            # Search for arm64 lldb-server
            pattern = os.path.join(base_path, "*/toolchains/llvm/prebuilt/*/lib/clang/*/lib/linux/arm64/lldb-server")
            matches = glob.glob(pattern)
            
            if matches:
                return matches[0]
        
        return None
    
    def ensure_binary(self, ndk_path: str = None) -> bool:
        """Ensure lldb-server binary is on device."""
        if self.check_binary():
            return True
        
        # Try to find and push from NDK
        local_path = self.find_ndk_lldb_server(ndk_path)
        if local_path:
            return self.push_to_device(local_path)
        
        return False
    
    def get_connection_url(self) -> str:
        """Get LLDB connection URL."""
        return f"connect://localhost:{self.config.port}"
    
    def __enter__(self):
        """Context manager entry."""
        self.start()
        self.setup_port_forward()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.stop()
        self.remove_port_forward()
        return False


class LLDBMultiServer:
    """Manage multiple lldb-server instances for multiple devices."""
    
    def __init__(self):
        self.servers: List[LLDBServer] = []
    
    def add_server(self, device_serial: str = None, port: int = None) -> LLDBServer:
        """Add a server for a device."""
        config = ServerConfig()
        if port:
            config.port = port
        
        server = LLDBServer(device_serial, config)
        self.servers.append(server)
        return server
    
    def start_all(self) -> bool:
        """Start all servers."""
        for server in self.servers:
            if not server.start():
                return False
            if not server.setup_port_forward():
                return False
        return True
    
    def stop_all(self):
        """Stop all servers."""
        for server in self.servers:
            server.stop()
            server.remove_port_forward()


def get_lldb_server(device_serial: str = None,
                   auto_start: bool = True) -> LLDBServer:
    """Get LLDB server instance, optionally starting it."""
    server = LLDBServer(device_serial)
    
    if auto_start and not server.is_running():
        server.ensure_binary()
        server.start()
        server.setup_port_forward()
    
    return server
