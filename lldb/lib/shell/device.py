#!/usr/bin/env python3
"""
ADB/Device operations for shell scripts.

This module provides Python wrappers for ADB commands
that can be used by both shell scripts and Python code.
"""

import subprocess
import time
from typing import Optional, List, Tuple


class ADBDevice:
    """ADB device operations."""
    
    def __init__(self, serial: str = None):
        self.serial = serial
        self._adb_cmd = ['adb']
        if serial:
            self._adb_cmd.extend(['-s', serial])
    
    def _run(self, cmd: List[str], capture: bool = True,
             timeout: int = 30) -> Tuple[int, str, str]:
        """Run an adb command."""
        full_cmd = self._adb_cmd + cmd
        
        try:
            if capture:
                result = subprocess.run(
                    full_cmd,
                    capture_output=True,
                    text=True,
                    timeout=timeout
                )
                return result.returncode, result.stdout, result.stderr
            else:
                result = subprocess.run(full_cmd, timeout=timeout)
                return result.returncode, "", ""
        except subprocess.TimeoutExpired:
            return -1, "", "Timeout"
        except Exception as e:
            return -1, "", str(e)
    
    def is_connected(self) -> bool:
        """Check if device is connected."""
        returncode, stdout, _ = self._run(['devices'])
        if returncode != 0:
            return False
        # Check for device (not just emulator)
        return 'device' in stdout and 'emulator' not in stdout
    
    def get_devices(self) -> List[str]:
        """Get list of connected devices."""
        _, stdout, _ = self._run(['devices', '-l'])
        devices = []
        for line in stdout.split('\n')[1:]:  # Skip header
            if line.strip() and 'device' in line:
                devices.append(line.split()[0])
        return devices
    
    def shell(self, command: str, capture: bool = True) -> Tuple[int, str, str]:
        """Run shell command on device."""
        return self._run(['shell', command], capture=capture)
    
    def push(self, local: str, remote: str) -> bool:
        """Push file to device."""
        returncode, _, _ = self._run(['push', local, remote])
        return returncode == 0
    
    def pull(self, remote: str, local: str) -> bool:
        """Pull file from device."""
        returncode, _, _ = self._run(['pull', remote, local])
        return returncode == 0
    
    def forward(self, local: str, remote: str) -> bool:
        """Setup port forwarding."""
        returncode, _, _ = self._run(['forward', local, remote])
        return returncode == 0
    
    def remove_forward(self, local: str = None) -> bool:
        """Remove port forwarding."""
        if local:
            returncode, _, _ = self._run(['forward', '--remove', local])
        else:
            returncode, _, _ = self._run(['forward', '--remove-all'])
        return returncode == 0
    
    def logcat(self, args: List[str] = None, clear: bool = False) -> str:
        """Run logcat command."""
        cmd = ['logcat']
        if clear:
            cmd.append('-c')
        if args:
            cmd.extend(args)
        _, stdout, _ = self._run(cmd)
        return stdout
    
    def install(self, apk: str) -> bool:
        """Install APK."""
        returncode, _, _ = self._run(['install', '-r', apk])
        return returncode == 0
    
    def uninstall(self, package: str) -> bool:
        """Uninstall package."""
        returncode, _, _ = self._run(['uninstall', package])
        return returncode == 0


class AppManager:
    """Manage app lifecycle."""
    
    def __init__(self, package: str, device: ADBDevice = None):
        self.package = package
        self.device = device or ADBDevice()
    
    def is_installed(self) -> bool:
        """Check if app is installed."""
        returncode, stdout, _ = self.device.shell(f'pm list packages {self.package}')
        return returncode == 0 and self.package in stdout
    
    def start(self, activity: str = None, debug: bool = False) -> bool:
        """Start the app."""
        if activity:
            component = f"{self.package}/{activity}"
        else:
            component = self.package
        
        cmd = f"am start -n {component}"
        if debug:
            cmd = f"am start -D -n {component}"
        
        returncode, _, _ = self.device.shell(cmd)
        return returncode == 0
    
    def stop(self) -> bool:
        """Force stop the app."""
        returncode, _, _ = self.device.shell(f"am force-stop {self.package}")
        return returncode == 0
    
    def clear_data(self) -> bool:
        """Clear app data."""
        returncode, _, _ = self.device.shell(f"pm clear {self.package}")
        return returncode == 0
    
    def get_pid(self) -> Optional[int]:
        """Get PID of running app."""
        returncode, stdout, _ = self.device.shell(f"pidof {self.package}")
        if returncode != 0:
            return None
        try:
            return int(stdout.strip())
        except ValueError:
            return None
    
    def is_running(self) -> bool:
        """Check if app is running."""
        return self.get_pid() is not None
    
    def wait_for_start(self, timeout: int = 30, poll_interval: float = 0.5) -> bool:
        """Wait for app to start."""
        start = time.time()
        while time.time() - start < timeout:
            if self.is_running():
                return True
            time.sleep(poll_interval)
        return False
    
    def wait_for_exit(self, timeout: int = 30, poll_interval: float = 0.5) -> bool:
        """Wait for app to exit."""
        start = time.time()
        while time.time() - start < timeout:
            if not self.is_running():
                return True
            time.sleep(poll_interval)
        return False


class DeviceInfo:
    """Get device information."""
    
    def __init__(self, device: ADBDevice = None):
        self.device = device or ADBDevice()
    
    def get_prop(self, prop: str) -> str:
        """Get property value."""
        _, stdout, _ = self.device.shell(f"getprop {prop}")
        return stdout.strip()
    
    def get_android_version(self) -> str:
        """Get Android version."""
        return self.get_prop("ro.build.version.release")
    
    def get_sdk_version(self) -> int:
        """Get SDK version."""
        try:
            return int(self.get_prop("ro.build.version.sdk"))
        except ValueError:
            return 0
    
    def get_model(self) -> str:
        """Get device model."""
        return self.get_prop("ro.product.model")
    
    def get_manufacturer(self) -> str:
        """Get device manufacturer."""
        return self.get_prop("ro.product.manufacturer")
    
    def is_emulator(self) -> bool:
        """Check if running on emulator."""
        model = self.get_model().lower()
        return 'emulator' in model or 'sdk' in model
    
    def is_rooted(self) -> bool:
        """Check if device is rooted."""
        returncode, _, _ = self.device.shell("su -c 'id'")
        return returncode == 0


class ProcessMonitor:
    """Monitor process state."""
    
    def __init__(self, pid: int, device: ADBDevice = None):
        self.pid = pid
        self.device = device or ADBDevice()
    
    def is_alive(self) -> bool:
        """Check if process is still alive."""
        returncode, _, _ = self.device.shell(f"kill -0 {self.pid} 2>/dev/null")
        return returncode == 0
    
    def get_maps(self) -> str:
        """Get /proc/pid/maps."""
        _, stdout, _ = self.device.shell(f"cat /proc/{self.pid}/maps")
        return stdout
    
    def get_library_base(self, lib_name: str) -> Optional[int]:
        """Get library base address from maps."""
        _, stdout, _ = self.device.shell(
            f"cat /proc/{self.pid}/maps | grep {lib_name} | head -1 | cut -d'-' -f1"
        )
        if stdout.strip():
            try:
                return int(stdout.strip(), 16)
            except ValueError:
                pass
        return None
    
    def wait_for_library(self, lib_name: str, timeout: int = 30,
                        poll_interval: float = 0.5) -> Optional[int]:
        """Wait for library to load."""
        start = time.time()
        while time.time() - start < timeout:
            base = self.get_library_base(lib_name)
            if base is not None:
                return base
            time.sleep(poll_interval)
        return None


def get_default_device() -> ADBDevice:
    """Get default ADB device."""
    return ADBDevice()
