# Developer, Testing & Configuration Guide

## 1. Prerequisites & Build Instructions

### Prerequisites
* **Compiler:** Modern C++ compiler with C++17 support (`g++ >= 9.0` or `clang++ >= 10.0`)
* **Build System:** CMake `>= 3.16`
* **Dependencies:** Automatic via CMake `FetchContent` (No manual submodule or library installation required):
  * `nlohmann/json` v3.11.3
  * `moodycamel::ConcurrentQueue` v1.0.4
  * `tinyfsm` v0.3.3
  * `Catch2` v3.5.4

---

### Building the Project

```bash
# Clone and enter directory
cd codebase

# Configure CMake in build directory
cmake -S . -B build

# Compile with all CPU cores
cmake --build build -j
```

---

## 2. Running the Daemon

### 1. Mock Mode (Default for Development / Laptops)
Runs the daemon with mock hardware, mock network, and mock vision engines. No physical Arduino or camera required:

```bash
./build/solar_edge_daemon config/daemon.config.json
```

### 2. Interactive CLI Console
When launched in console mode, you can type live commands into stdin to drive the State Machine:

| Command | Action |
|---|---|
| `start` | Initiates an automated cleaning mission from sector 0. |
| `debris <type>` | Simulates AI detecting debris (`dust`, `leaf`, `bird`, `unknown`). |
| `clear` | Simulates AI inspecting a sector and finding no debris. |
| `estop [reason]` | Triggers a synthetic hardware emergency stop (e.g. `estop limit_switch`). |
| `estop-clear` | Clears the E-Stop latch and triggers a boot self-test reboot. |
| `network-lost` | Simulates an MQTT / Wi-Fi network drop mid-operation. |
| `network-restored` | Simulates network reconnection. |
| `help` | Displays the command menu. |
| `quit` / `exit` | Requests clean, graceful daemon shutdown. |

---

## 3. Automated Test Suite (Catch2 v3)

Run all 40 automated unit and integration tests via `ctest`:

```bash
ctest --test-dir build --output-on-failure
```

### Test Suite Directory Structure

| Test File | Tests Covered |
|---|---|
| `test_config_loader.cpp` | Validates config parsing, comment handling, missing field rejection, and type checks. |
| `test_config_malleability.cpp` | Tests RFC 6901 JSON pointer fallback resolution, hex byte parsing, and custom opcode tables. |
| `test_event_queue.cpp` | Multi-threaded lock-free queue stress test; FIFO preservation and concurrency safety. |
| `test_fsm_boot.cpp` | Boot self-test sequences, all-ready transitions, and retry limit escalations to `Hardware_Fault`. |
| `test_fsm_cleaning_cycle.cpp` | Full cleaning pass lifecycle: `Idle` $\to$ `Inspection` $\to$ `DryClean` $\to$ `WetClean` $\to$ `Idle`. |
| `test_fsm_estop.cpp` | Multi-threaded race condition tests; verifies stale events are discarded when E-Stop is active. |
| `test_fsm_network_resilience.cpp` | Mid-cycle Wi-Fi drops, degraded `Offline_Idle` transitions, and reconnection restoration. |
| `test_mock_hardware_driver.cpp` | Self-test simulation and fault injection verification. |
| `test_mock_vision_engine.cpp` | Deterministic canned inference cycling and warm-up checks. |
| `test_serial_driver.cpp` | **Hardware-in-the-Loop Loopback:** Opens Linux Virtual PTY (`/dev/pts/X`), validates serial byte serialization, and verifies sub-millisecond atomic E-Stop flag latching on `0xE1`. |
| `test_telemetry_cache.cpp` | Tests `.jsonl` disk caching, `fdatasync()` durability, sequence continuity, and restart persistence. |
| `test_application_boot.cpp` | Composition root lifecycle, signal trapping, and clean shutdown. |

---

## 4. Complete Configuration Reference (`daemon.config.json`)

```json
{
  "system": {
    "log_level": "info",             // Log level: trace | debug | info | warn | error
    "total_sectors": 6,               // Number of solar panel sectors to clean
    "queue_capacity_hint": 1024,      // Initial ring capacity for lock-free event queue
    "boot_timeout_ms": 5000,          // Self-test timeout per peripheral (ms)
    "boot_max_retries": 3             // Max retry attempts before entering Hardware_Fault
  },
  "mock": {
    "enabled": true,                  // Global mock default
    "hardware_driver": true,          // true: use MockHardwareDriver, false: use SerialHardwareDriver
    "network_client": true,           // true: use MockNetworkClient, false: use PahoNetworkClient
    "vision_engine": true,            // true: use MockVisionEngine, false: use OnnxVisionEngine
    "hardware_fault_injection": false,// Simulate Arduino failure during self-test
    "network_start_offline": false,   // Simulate starting daemon without cloud connectivity
    "vision_cycle_ms": 800,           // Mock inference latency (ms)
    "cleaning_duration_ms": 500       // Mock sector clean travel time (ms)
  },
  "serial": {
    "device_path": "/dev/ttyACM0",    // POSIX serial port path (or /dev/ttyUSB0)
    "baud_rate": 115200,              // Baud rate: 9600, 19200, 38400, 57600, 115200, 230400
    "timeout_ms": 500,                // Serial read timeout (ms)
    "protocol_mode": "single_byte",   // Protocol framing mode: single_byte | framed_packet | ascii_line
    "commands": {
      "dry_clean": "0x01",            // Outbound opcode: Dry clean start
      "wet_clean": "0x02",            // Outbound opcode: Wet clean start
      "emergency_halt": "0xFF",       // Outbound opcode: E-Stop / kill power
      "ping": "0x00"                  // Outbound opcode: Self-test ping
    },
    "interrupts": {
      "0xE1": { "event": "estop", "reason": "limit_switch_hit" },
      "0xE2": { "event": "estop", "reason": "motor_stall" },
      "0xD0": { "event": "clean_complete" },
      "0xA0": { "event": "ack_ping" },
      "0xA1": { "event": "ack" },
      "0xA2": { "event": "ack" },
      "0xAF": { "event": "ack" }
    }
  },
  "mqtt": {
    "broker_host": "localhost",       // IP or hostname of FastAPI Mosquitto broker
    "broker_port": 1883,              // MQTT port (1883 TCP / 8883 TLS)
    "client_id": "edge-daemon-01",    // Unique MQTT client ID
    "topic_prefix": "solarsweeper/robot01", // Topic namespace root
    "keepalive_sec": 10,              // MQTT keepalive ping interval (seconds)
    "cache_path": "/var/lib/edge-daemon/cache.json", // Non-volatile offline telemetry path
    "routes": [
      { "topic": "cmd/start_clean", "action": "START" },
      { "topic": "cmd/abort",       "action": "ABORT" },
      { "topic": "cmd/reset",       "action": "RESET" }
    ],
    "telemetry_topics": {
      "status": "telemetry/status",
      "clean_result": "telemetry/clean_result",
      "alert": "telemetry/alert"
    }
  },
  "vision": {
    "model_path": "models/debris_detector.onnx", // Path to trained ONNX neural net
    "camera_index": 0,                // V4L2 camera device index (/dev/video0)
    "confidence_threshold": 0.65,     // Detection threshold (0.0 to 1.0)
    "class_map": {
      "0": { "name": "dust",          "action": "dry_clean" },
      "1": { "name": "leaf",          "action": "dry_clean" },
      "2": { "name": "bird_dropping", "action": "wet_clean" }
    }
  }
}
```
