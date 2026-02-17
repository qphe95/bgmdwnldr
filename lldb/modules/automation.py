"""
Expect-based automation for non-interactive debugging.
"""

from .base import DebugModule, ModuleConfig
from ..lib.core.automation import ExpectScriptBuilder
from ..lib.debug.breakpoints import BreakpointConfig


class ExpectAutomationModule(DebugModule):
    """Use expect scripting for automated LLDB sessions."""
    
    name = "expect_automation"
    description = "Automate LLDB via expect scripting"
    
    def __init__(self, session, config=None):
        super().__init__(session, config)
        self.builder = ExpectScriptBuilder("lldb")
        self._expect_script_path = "/tmp/qjs_expect_auto.exp"
        self._state = {
            'interactions_defined': 0,
            'automation_active': False,
        }
    
    def setup(self):
        """Build and configure expect script."""
        # Add common LLDB interactions
        self._build_lldb_script()
        self.log("Expect automation enabled")
    
    def _build_lldb_script(self):
        """Build the expect script for LLDB automation."""
        # Platform connection
        self.builder.add_interaction(
            expect="(lldb) ",
            send="platform select remote-android",
            timeout=10
        )
        
        self.builder.add_interaction(
            expect="(lldb) ",
            send="platform connect connect://localhost:5039",
            timeout=10
        )
        
        # Attach or target
        if self.session.pid:
            self.builder.add_interaction(
                expect="(lldb) ",
                send=f"attach -p {self.session.pid}",
                timeout=15
            )
        
        # Load debug script
        self.builder.add_interaction(
            expect="(lldb) ",
            send="command script import main.py",
            timeout=10
        )
        
        # Start debugging
        self.builder.add_interaction(
            expect="(lldb) ",
            send="qjs-debug comprehensive",
            timeout=10
        )
        
        # Continue execution
        self.builder.add_interaction(
            expect="(lldb) ",
            send="continue",
            timeout=5
        )
        
        # Add conditional handling for various stop conditions
        self._add_stop_conditions()
        
        self._state['interactions_defined'] = len(self.builder.interactions)
    
    def _add_stop_conditions(self):
        """Add expect conditions for different stop scenarios."""
        self.builder.add_conditional([
            ("stop reason = breakpoint", "bt\r\nframe info\r\ncontinue\r"),
            ("stop reason = watchpoint", "bt\r\nregister read\r\ncontinue\r"),
            ("stop reason = signal", "bt all\r\nregister read\r"),
            ("Process exited", "quit\r"),
            ("timeout", "quit\r"),
        ])
    
    def add_conditional_stop(self, pattern: str, action: str = None):
        """Add a conditional stop pattern."""
        self.builder.add_conditional([
            (pattern, action if action else ""),
            ("timeout", None),
        ])
    
    def run_automation(self, capture_output: bool = True) -> tuple:
        """Execute the expect script."""
        self.log("Starting expect automation...")
        
        try:
            returncode, stdout, stderr = self.builder.write_and_execute(
                path=self._expect_script_path,
                capture_output=capture_output
            )
            
            self._state['automation_active'] = False
            
            return returncode, stdout, stderr
            
        except Exception as e:
            self.log(f"Automation failed: {e}")
            return -1, "", str(e)
    
    def generate_script(self, output_path: str = None) -> str:
        """Generate the expect script to a file."""
        if output_path:
            self._expect_script_path = output_path
        
        script_content = self.builder.generate()
        
        with open(self._expect_script_path, 'w') as f:
            f.write(script_content)
        
        import os
        os.chmod(self._expect_script_path, 0o755)
        
        self.log(f"Expect script written to: {self._expect_script_path}")
        return self._expect_script_path
