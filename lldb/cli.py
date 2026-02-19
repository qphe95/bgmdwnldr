#!/usr/bin/env python3
"""
Command-line interface for non-interactive debugging.

This script can be run standalone to automate debugging without
needing to manually interact with LLDB.

Usage:
    python3 cli.py --profile comprehensive --launch
    python3 cli.py --attach --pid 1234 --profile minimal
    python3 cli.py --profile stripped --wait-for-crash
"""

import argparse
import subprocess
import sys
import time
import os

# Add parent directory to path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)

from lib.core.automation import LLDBServerManager, ADBAutomation


def check_device() -> bool:
    """Check if Android device is connected."""
    result = subprocess.run(
        ['adb', 'devices'],
        capture_output=True,
        text=True
    )
    # Check for any device (including emulators)
    # Format: "emulator-5554\tdevice" or "ABC123\tdevice"
    for line in result.stdout.strip().split('\n')[1:]:  # Skip header
        if '\tdevice' in line:
            return True
    return False


def get_app_pid(package: str = "com.bgmdwldr.vulkan") -> int:
    """Get PID of running app."""
    result = subprocess.run(
        ['adb', 'shell', 'pidof', package],
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        return None
    try:
        return int(result.stdout.strip())
    except ValueError:
        return None


def start_app(package: str = "com.bgmdwldr.vulkan",
              activity: str = ".MainActivity") -> bool:
    """Start the app."""
    result = subprocess.run(
        ['adb', 'shell', 'am', 'start', '-n', f'{package}/{activity}'],
        capture_output=True
    )
    return result.returncode == 0


def stop_app(package: str = "com.bgmdwldr.vulkan") -> bool:
    """Stop the app."""
    result = subprocess.run(
        ['adb', 'shell', 'am', 'force-stop', package],
        capture_output=True
    )
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(
        description="QuickJS Shape Corruption Debugger CLI",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Launch app and debug with comprehensive profile
  %(prog)s --launch --profile comprehensive

  # Attach to running app with minimal profile
  %(prog)s --attach --profile minimal

  # Debug stripped binary
  %(prog)s --profile stripped --launch --wait-for-crash

  # Use expect scripting for automation
  %(prog)s --launch --profile comprehensive --expect

  # Run until crash and save output
  %(prog)s --launch --profile comprehensive --wait-for-crash -o results.log
        """
    )
    
    parser.add_argument(
        "--profile", "-p",
        default="comprehensive",
        choices=["comprehensive", "minimal", "shape_only", "shape",
                "register_focus", "register", "stripped", "stripped_binary"],
        help="Debug profile to use (default: comprehensive)"
    )
    
    parser.add_argument(
        "--attach", "-a",
        action="store_true",
        help="Attach to running process"
    )
    
    parser.add_argument(
        "--pid", "-P",
        type=int,
        help="PID to attach to (if not specified, will auto-detect)"
    )
    
    parser.add_argument(
        "--launch", "-l",
        action="store_true",
        help="Launch app before debugging"
    )
    
    parser.add_argument(
        "--wait-for-crash", "-w",
        action="store_true",
        help="Run until crash detected or timeout"
    )
    
    parser.add_argument(
        "--output", "-o",
        help="Output file for results"
    )
    
    parser.add_argument(
        "--stripped", "-s",
        action="store_true",
        help="Use address-based breakpoints for stripped binaries"
    )
    
    parser.add_argument(
        "--expect", "-e",
        action="store_true",
        help="Use expect scripting for automation"
    )
    
    parser.add_argument(
        "--async-mode",
        dest="async_mode",
        action="store_true",
        help="Use async mode to prevent ANR"
    )
    
    parser.add_argument(
        "--timeout", "-t",
        type=int,
        default=300,
        help="Timeout in seconds (default: 300)"
    )
    
    parser.add_argument(
        "--trigger-crash",
        action="store_true",
        help="Automatically trigger crash via UI interaction"
    )
    
    parser.add_argument(
        "--package",
        default="com.bgmdwldr.vulkan",
        help="App package name (default: com.bgmdwldr.vulkan)"
    )
    
    parser.add_argument(
        "--port",
        type=int,
        default=5039,
        help="LLDB server port (default: 5039)"
    )
    
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Verbose output"
    )
    
    args = parser.parse_args()
    
    # Check device
    if not check_device():
        print("Error: No Android device connected")
        return 1
    
    # Get or determine PID
    pid = args.pid
    
    if args.launch:
        print(f"[CLI] Stopping any existing app instances...")
        stop_app(args.package)
        time.sleep(1)
        
        print(f"[CLI] Starting app...")
        if not start_app(args.package):
            print("Error: Failed to start app")
            return 1
        time.sleep(2)
        
        pid = get_app_pid(args.package)
        if not pid:
            print("Error: Failed to get app PID after launch")
            return 1
        print(f"[CLI] App started, PID: {pid}")
    
    elif args.attach:
        if not pid:
            pid = get_app_pid(args.package)
        if not pid:
            print(f"Error: App not running. Use --launch to start it.")
            return 1
        print(f"[CLI] Attaching to PID: {pid}")
    
    else:
        # Default: try to attach, launch if not running
        pid = get_app_pid(args.package)
        if not pid:
            print(f"[CLI] App not running, launching...")
            start_app(args.package)
            time.sleep(2)
            pid = get_app_pid(args.package)
        if not pid:
            print("Error: Failed to get app PID")
            return 1
    
    # Start lldb-server
    print(f"[CLI] Starting lldb-server...")
    server = LLDBServerManager()
    if not server.start(args.port):
        print("Error: Failed to start lldb-server")
        return 1
    
    if not server.setup_port_forward(args.port, args.port):
        print("Error: Failed to setup port forwarding")
        return 1
    
    # Build LLDB init commands for remote Android debugging
    # Use proper remote platform connection instead of -p (local attach)
    init_cmds = [
        "platform select remote-android",
        f"platform connect connect://localhost:{args.port}",
        f"process attach -p {pid}",
    ]
    
    # Configure crash signals to stop and notify
    init_cmds.extend([
        "process handle SIGSEGV --stop=true --notify=true --pass=false",
        "process handle SIGBUS --stop=true --notify=true --pass=false",
        "process handle SIGILL --stop=true --notify=true --pass=false",
        "process handle SIGABRT --stop=true --notify=true --pass=false",
    ])
    
    if args.async_mode:
        init_cmds.append("settings set target.async true")
    
    # Load main.py and set profile
    init_cmds.extend([
        f"command script import {SCRIPT_DIR}/main.py",
        f"qjs-debug {args.profile}",
    ])
    
    # Add stop-hook for crash detection
    init_cmds.append(f'target stop-hook add -o "script import sys; sys.path.insert(0, \\"{SCRIPT_DIR}\\"); from main import qjs_handle_stop_event; qjs_handle_stop_event(frame, None, {{}})"')
    
    # Build final LLDB command
    lldb_cmd = ["lldb"]
    for cmd in init_cmds:
        lldb_cmd.extend(["-o", cmd])
    
    # Continue execution
    lldb_cmd.extend(["-o", "continue"])
    
    # Setup output redirection
    output_file = None
    if args.output:
        output_file = open(args.output, 'w')
    
    try:
        print(f"[CLI] Starting LLDB with profile: {args.profile}")
        print(f"[CLI] Commands: {' '.join(lldb_cmd)}")
        
        # Run LLDB
        if args.expect:
            # Use expect for automation
            from lib.core.automation import ExpectScriptBuilder
            
            builder = ExpectScriptBuilder("lldb")
            
            # Add initial commands
            for cmd in init_cmds:
                builder.add_interaction("(lldb) ", cmd)
            
            builder.add_interaction("(lldb) ", "continue")
            
            # Add conditional for crash detection
            builder.add_conditional([
                ("stop reason", "bt\r\nregister read\r"),
                ("Process", None),  # Just continue monitoring
                ("timeout", "quit\r"),
            ])
            
            if args.trigger_crash:
                # Trigger crash after short delay
                time.sleep(3)
                print("[CLI] Triggering crash via UI...")
                ADBAutomation.trigger_url_entry()
            
            # Run with expect
            returncode, stdout, stderr = builder.write_and_execute()
            
            if output_file:
                output_file.write(stdout)
                output_file.write(stderr)
            
            print(f"[CLI] LLDB exited with code: {returncode}")
            
        else:
            # Run LLDB directly
            if args.trigger_crash:
                # Start LLDB in background
                import threading
                
                def trigger():
                    time.sleep(5)
                    print("[CLI] Triggering crash via UI...")
                    ADBAutomation.trigger_url_entry()
                
                trigger_thread = threading.Thread(target=trigger)
                trigger_thread.daemon = True
                trigger_thread.start()
            
            # Run LLDB
            if output_file:
                result = subprocess.run(lldb_cmd, stdout=output_file, stderr=subprocess.STDOUT)
            else:
                result = subprocess.run(lldb_cmd)
            
            print(f"[CLI] LLDB exited with code: {result.returncode}")
    
    finally:
        if output_file:
            output_file.close()
        server.stop()
        print("[CLI] Cleanup complete")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
