# Solar Sweeper: Edge Daemon 🤖⚡

The **C++ Edge Daemon** is the high-performance, real-time "Central Brain" of the Automatic Solar Panel Cleaning Gantry, engineered to run on a Raspberry Pi / embedded Linux SBC.

It acts as the deterministic bridge between the **Cloud Backend (FastAPI)**, the **Vision AI Pipeline (ONNX/OpenCV)**, and the **Physical Actuators (Arduino Microcontroller)**.

---

## 📚 Comprehensive Documentation Suite

For in-depth explanations, architecture diagrams, decision records, and integration protocols, explore our documentation guides:

| Document | Description |
|---|---|
| 🏛️ **[System Architecture & Concurrency Model](file:///mnt/data/Backups/Documents/Capstone/codebase/docs/ARCHITECTURE.md)** | Multi-threaded swimlanes, lock-free queue architecture, and sub-millisecond safety model. |
| ⚖️ **[Architectural Decisions & Rationale (ADRs)](file:///mnt/data/Backups/Documents/Capstone/codebase/docs/DECISIONS_AND_RATIONALE.md)** | Why C++17, why TinyFSM, why lock-free MPSC queues, why malleable contracts, and rejected alternatives. |
| 🔄 **[State Machine & Lifecycle Guide](file:///mnt/data/Backups/Documents/Capstone/codebase/docs/STATE_MACHINE_GUIDE.md)** | Detailed breakdown of all 8 TinyFSM states, event catalogs, edge cases, and fail-safes. |
| 🔌 **[Hardware HAL & Safety Guide](file:///mnt/data/Backups/Documents/Capstone/codebase/docs/HARDWARE_AND_SAFETY_GUIDE.md)** | POSIX UART/Serial driver, opcode lookup tables, limit switch interrupts, and systemd service. |
| 📡 **[Offline Resilience & Telemetry Guide](file:///mnt/data/Backups/Documents/Capstone/codebase/docs/OFFLINE_AND_TELEMETRY_GUIDE.md)** | MQTT schemas, RFC 6901 tolerant JSON fallbacks, `fdatasync` power-cut durability, and cloud deduplication. |
| 🛠️ **[Developer, Testing & Config Guide](file:///mnt/data/Backups/Documents/Capstone/codebase/docs/DEVELOPER_AND_TESTING_GUIDE.md)** | Quickstart, full `daemon.config.json` reference, interactive CLI console, and test suite breakdown. |
| 🚜 **[Physical, Optical & Operational Model](file:///mnt/data/Backups/Documents/Capstone/codebase/docs/PHYSICAL_AND_OPERATIONAL_MODEL.md)** | Continuous travel kinematics, gantry tool layout, sector vs bounding box hierarchy, and optical FOV math. |
| 📄 **[Serial Interface Contract](file:///mnt/data/Backups/Documents/Capstone/codebase/contracts/serial_protocol.md)** | Byte-level hardware protocol contract for Arduino microcontroller firmware. |
| 📄 **[MQTT Interface Contract](file:///mnt/data/Backups/Documents/Capstone/codebase/contracts/mqtt_schema.md)** | JSON schemas and topic routes contract for FastAPI cloud engineers. |

---

## 🚀 Key Architectural Highlights

- **Lock-Free Concurrency:** Uses `moodycamel::BlockingConcurrentQueue` to eliminate mutex bottlenecks and priority inversions.
- **TinyFSM Deterministic Core:** Zero-dynamic-allocation state engine guaranteeing safe transitions between `Boot_Init`, `Idle`, `Offline_Idle`, `Inspection`, `DryClean`, `WetClean`, `Error`, and `Hardware_Fault`.
- **Sub-Millisecond Atomic Safety:** Hardware emergency stops bypass the software queue using a release-fenced `std::atomic<bool>` flag, killing motor actuation within microseconds.
- **Zero-Recompile Malleability:** All serial byte opcodes, interrupt triggers, MQTT routes, and YOLO class IDs are driven by JSON configuration.
- **Power-Cut Durability:** Offline telemetry disk cache uses POSIX `fdatasync()` to commit data to physical flash immediately, guaranteeing zero data loss during sudden power disconnects.

---

## 🏗️ Phased Execution Plan

* **Phase 0: Integration Contracts** -- 🟢 **Done**
  * Formal serial byte and MQTT JSON contracts documented and version-controlled.
* **Phase 1: The Skeleton (FSM & Event Loop)** -- 🟢 **Done**
  * Core C++17 daemon, lock-free MPSC queue, TinyFSM engine, mock modules, interactive CLI, and Catch2 test harness.
* **Phase 2: Hardware Abstraction Layer & Malleability** -- 🟢 **Done**
  * POSIX UART/Serial driver, background reader thread, configurable opcode lookup, systemd unit, and offline disk cache.
* **Phase 3: Network & Telemetry Layer (MQTT Integration)** -- 🟢 **Done**
  * Integrated Eclipse Paho MQTT client for live cloud telemetry synchronization, remote command dispatch, and offline disk cache spooling/draining.
* **Phase 4: AI Vision Integration** -- ⏳ **Upcoming**
  * Hooking up the camera pipeline and ONNX Runtime for debris classification and automated cleaning decisions.

---

## 🛠️ Quickstart

### 1. Build and Test
```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```
*(40 / 40 Catch2 unit tests pass 100%).*

### 2. Run with Interactive Console (Mock Mode)
```bash
./build/solar_edge_daemon config/daemon.config.json
```
Type `help` in the interactive console for live commands (`start`, `debris <type>`, `estop`, `network-lost`, etc.).
