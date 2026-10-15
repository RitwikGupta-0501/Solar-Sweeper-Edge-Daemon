import os
import pty
import subprocess
import time
import json
import tempfile
import select

def test_serial_integration():
    print("Starting Integration Test...")
    
    # 1. Native OS Mocking: Create a pseudo-terminal pair (PTY)
    # The 'master' acts as our python test script (Arduino emulator).
    # The 'slave' acts as a real /dev/tty file that the C++ daemon connects to.
    master_fd, slave_fd = pty.openpty()
    slave_name = os.ttyname(slave_fd)
    print(f"Created virtual serial port: {slave_name}")

    # 2. Create a temporary config file that points to the virtual port
    # and has mock.enabled = False, forcing the REAL SerialHardwareDriver to boot.
    config = {
        "system": {
            "log_level": "info",
            "total_sectors": 2,
            "queue_capacity_hint": 1024,
            "boot_timeout_ms": 5000,
            "boot_max_retries": 3
        },
        "mock": {
            "enabled": False, 
            "hardware_fault_injection": False,
            "network_start_offline": False,
            "vision_cycle_ms": 50,
            "cleaning_duration_ms": 50
        },
        "mqtt": { "broker_host": "localhost", "broker_port": 1883, "client_id": "test", "topic_prefix": "test" },
        "serial": {
            "device_path": slave_name,
            "baud_rate": 115200
        },
        "vision": { "model_path": "", "camera_index": 0 }
    }

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.json') as f:
        json.dump(config, f)
        config_path = f.name

    daemon = None
    try:
        # 3. Launch the compiled C++ binary
        print("Launching C++ Edge Daemon...")
        daemon = subprocess.Popen(
            ['./build/solar_edge_daemon', config_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True
        )

        time.sleep(0.5)

        # 4. Arduino Emulator: Send "Ready" event to C++
        print("Arduino Emulator -> Sending 'R\\n' (Ready)...")
        os.write(master_fd, b"R\n")
        time.sleep(0.5)

        # Helper to read bytes coming out of the C++ daemon
        def read_from_daemon(timeout=2.0):
            r, _, _ = select.select([master_fd], [], [], timeout)
            if r:
                return os.read(master_fd, 1024).decode('utf-8')
            return ""

        # Drain any initial SafePark() halt commands ('H\n') emitted during boot
        boot_output = ""
        while True:
            r, _, _ = select.select([master_fd], [], [], 0.2)
            if r:
                boot_output += os.read(master_fd, 1024).decode('utf-8')
            else:
                break

        # 5. Tell the FSM to start cleaning via the daemon's stdin console
        print("Test Runner -> Triggering 'start' via console...")
        daemon.stdin.write("start\n")
        daemon.stdin.flush()

        # 6. Verify C++ daemon sends Dry Clean Sector 0
        output = ""
        start_t = time.time()
        while time.time() - start_t < 3.0:
            chunk = read_from_daemon(timeout=0.5)
            output += chunk
            if "D0\n" in output:
                break
        assert "D0\n" in output, f"Expected D0\\n, got '{output}'"
        print("Assertion Passed: Daemon correctly commanded 'D0\\n'")
        
        # 7. Arduino Emulator: Reply that Sector 0 is clean
        print("Arduino Emulator -> Sending 'C0\\n' (Clean Complete)...")
        os.write(master_fd, b"C0\n")
        
        # 8. Verify C++ daemon advances to Sector 1 (WetClean for bird dropping or DryClean)
        output = ""
        start_t = time.time()
        while time.time() - start_t < 3.0:
            chunk = read_from_daemon(timeout=0.5)
            output += chunk
            if "D1\n" in output or "W1\n" in output:
                break
        assert ("D1\n" in output or "W1\n" in output), f"Expected D1\\n or W1\\n, got '{output}'"
        print("Assertion Passed: Daemon correctly commanded sector 1 cleaning ('W1\\n' or 'D1\\n')")
        
        # 9. Arduino Emulator: Reply that Sector 1 is clean
        print("Arduino Emulator -> Sending 'C1\\n' (Clean Complete)...")
        os.write(master_fd, b"C1\n")
        
        # 10. Cleanly shut down
        print("Test Runner -> Shutting down...")
        daemon.stdin.write("quit\n")
        daemon.stdin.flush()
        daemon.wait(timeout=2.0)
        
        print("\nSUCCESS! Integration Test Passed.")
        print("The real SerialHardwareDriver successfully communicated with our Python Arduino emulator!")
    
    except AssertionError as e:
        print(f"\nTEST FAILED: {e}")
        if daemon:
            daemon.kill()
    finally:
        os.close(master_fd)
        os.close(slave_fd)
        os.remove(config_path)

if __name__ == "__main__":
    test_serial_integration()
