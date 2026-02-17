"""
Support for expect-based automation.
"""

import os
import subprocess
import tempfile
from typing import List, Tuple, Optional
from dataclasses import dataclass


@dataclass
class ExpectInteraction:
    """Single expect/send interaction."""
    expect: str           # Pattern to wait for
    send: Optional[str]   # Command to send (None for just wait)
    timeout: int = 30     # Timeout in seconds


@dataclass
class ExpectCondition:
    """Condition for expect block."""
    pattern: str
    action: Optional[str]  # Command to send, or None to just continue
    is_timeout: bool = False


class ExpectScriptBuilder:
    """Build expect scripts programmatically."""
    
    def __init__(self, program: str = "lldb"):
        self.program = program
        self.interactions: List[ExpectInteraction] = []
        self.conditional_blocks: List[List[ExpectCondition]] = []
        self.global_timeout = 60
        self.variables: dict = {}
    
    def set_variable(self, name: str, value: str):
        """Set a variable for use in the script."""
        self.variables[name] = value
    
    def add_interaction(self, expect: str, send: Optional[str] = None,
                        timeout: int = 30):
        """Add an expect/send pair."""
        self.interactions.append(ExpectInteraction(
            expect=expect,
            send=send,
            timeout=timeout
        ))
    
    def add_conditional(self, conditions: List[Tuple[str, Optional[str]]],
                        timeout: int = 30):
        """Add expect with multiple conditions.
        
        Args:
            conditions: List of (pattern, action) tuples
            timeout: Timeout for the expect block
        """
        cond_list = []
        for pattern, action in conditions:
            cond_list.append(ExpectCondition(
                pattern=pattern,
                action=action
            ))
        # Add timeout condition
        cond_list.append(ExpectCondition(
            pattern="timeout",
            action=None,
            is_timeout=True
        ))
        self.conditional_blocks.append(cond_list)
    
    def generate(self) -> str:
        """Generate the expect script content."""
        lines = [
            "#!/usr/bin/expect -f",
            f"set timeout {self.global_timeout}",
            "",
        ]
        
        # Set variables
        for name, value in self.variables.items():
            lines.append(f"set {name} {{{value}}}")
        if self.variables:
            lines.append("")
        
        # Spawn program
        lines.append(f"spawn {self.program}")
        lines.append("")
        
        # Add interactions
        for interaction in self.interactions:
            lines.append(f"expect {{")
            lines.append(f"    \"{interaction.expect}\" {{")
            if interaction.send:
                # Escape special characters in send
                send_str = interaction.send.replace("\\", "\\\\").replace("\"", "\\\"")
                lines.append(f"        send \"{send_str}\\r\"")
            lines.append("    }")
            lines.append("    timeout {")
            lines.append(f"        puts \"Timeout waiting for {interaction.expect}\"")
            lines.append("        exit 1")
            lines.append("    }")
            lines.append("}")
            lines.append("")
        
        # Add conditional blocks
        for cond_list in self.conditional_blocks:
            lines.append("expect {")
            for cond in cond_list:
                if cond.is_timeout:
                    lines.append("    timeout {")
                    lines.append("        puts \"Timeout\"")
                    lines.append("        exit 1")
                    lines.append("    }")
                else:
                    lines.append(f"    \"{cond.pattern}\" {{")
                    if cond.action:
                        action_str = cond.action.replace("\\", "\\\\").replace("\"", "\\\"")
                        lines.append(f"        send \"{action_str}\\r\"")
                    lines.append("    }")
            lines.append("}")
            lines.append("")
        
        return "\n".join(lines)
    
    def write_and_execute(self, path: Optional[str] = None,
                          capture_output: bool = True) -> Tuple[int, str, str]:
        """Write script to file, execute it, and return results.
        
        Returns:
            Tuple of (returncode, stdout, stderr)
        """
        if path is None:
            fd, path = tempfile.mkstemp(suffix='.exp', prefix='qjs_expect_')
            os.close(fd)
        
        script_content = self.generate()
        
        with open(path, 'w') as f:
            f.write(script_content)
        
        os.chmod(path, 0o755)
        
        try:
            if capture_output:
                result = subprocess.run(
                    ['expect', path],
                    capture_output=True,
                    text=True,
                    timeout=self.global_timeout
                )
                return result.returncode, result.stdout, result.stderr
            else:
                result = subprocess.run(
                    ['expect', path],
                    timeout=self.global_timeout
                )
                return result.returncode, "", ""
        finally:
            # Clean up temp file
            if path.startswith(tempfile.gettempdir()):
                try:
                    os.unlink(path)
                except OSError:
                    pass


class ADBAutomation:
    """High-level ADB automation helpers."""
    
    @staticmethod
    def tap(x: int, y: int) -> bool:
        """Tap screen at coordinates."""
        result = subprocess.run(
            ['adb', 'shell', 'input', 'tap', str(x), str(y)],
            capture_output=True
        )
        return result.returncode == 0
    
    @staticmethod
    def text_input(text: str) -> bool:
        """Enter text."""
        # Escape special characters for shell
        escaped = text.replace(' ', '%s').replace('&', '\\&')
        result = subprocess.run(
            ['adb', 'shell', 'input', 'text', escaped],
            capture_output=True
        )
        return result.returncode == 0
    
    @staticmethod
    def keyevent(keycode: int) -> bool:
        """Send keyevent."""
        result = subprocess.run(
            ['adb', 'shell', 'input', 'keyevent', str(keycode)],
            capture_output=True
        )
        return result.returncode == 0
    
    @staticmethod
    def trigger_url_entry(url: str = "https://www.youtube.com/watch?v=dQw4w9WgXcQ"):
        """Full sequence to trigger crash via URL entry."""
        # Tap input field
        ADBAutomation.tap(540, 600)
        import time
        time.sleep(0.5)
        
        # Clear existing (long press delete)
        ADBAutomation.keyevent(67)  # KEYCODE_DEL
        time.sleep(0.3)
        
        # Enter URL
        ADBAutomation.text_input(url)
        time.sleep(0.5)
        
        # Submit
        ADBAutomation.keyevent(66)  # KEYCODE_ENTER


class LLDBServerManager:
    """Manage lldb-server lifecycle."""
    
    def __init__(self, lldb_server_path: str = "/data/local/tmp/lldb-server"):
        self.lldb_server_path = lldb_server_path
        self._process = None
    
    def start(self, port: int = 5039) -> bool:
        """Start lldb-server on device."""
        # Kill any existing
        subprocess.run(
            ['adb', 'shell', 'pkill', '-f', 'lldb-server'],
            capture_output=True
        )
        import time
        time.sleep(0.5)
        
        # Start new instance
        cmd = [
            'adb', 'shell',
            f'{self.lldb_server_path} platform --listen "*:{port}" --server'
        ]
        self._process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL)
        time.sleep(2)
        
        # Verify
        result = subprocess.run(
            ['adb', 'shell', 'ps -A | grep lldb-server'],
            capture_output=True, text=True, shell=True
        )
        return 'lldb-server' in result.stdout
    
    def stop(self):
        """Stop lldb-server."""
        if self._process:
            self._process.terminate()
            try:
                self._process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._process.kill()
        
        subprocess.run(
            ['adb', 'shell', 'pkill', '-9', '-f', 'lldb-server'],
            capture_output=True
        )
    
    def setup_port_forward(self, local_port: int = 5039,
                          remote_port: int = 5039) -> bool:
        """Setup adb port forwarding."""
        result = subprocess.run(
            ['adb', 'forward', f'tcp:{local_port}', f'tcp:{remote_port}'],
            capture_output=True
        )
        return result.returncode == 0
    
    def __enter__(self):
        """Context manager entry."""
        self.start()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.stop()
        return False
