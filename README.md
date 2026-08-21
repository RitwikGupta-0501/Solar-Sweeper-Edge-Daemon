# Solar Sweeper: Edge Daemon 🤖⚡

## 🧠 What is the Edge Daemon?

The C++ Edge Daemon is the "Central Brain" of the Automatic Solar Panel Cleaning Machine. It is a highly optimized, multithreaded application designed to run on a Raspberry Pi. 

**Why does it exist?**
Current automated cleaning robots often rely on monolithic, blocking hardware loops (like a simple Arduino `loop()`). When trying to introduce heavy AI vision models and high-frequency cloud telemetry into those systems, the hardware suffers from race conditions, massive latency, and safety failures (like missing a limit switch hit because the CPU was busy processing an image). The Edge Daemon exists to decouple these systems—ensuring the robot is both "smart" (AI-driven) and "safe" (real-time hardware responsiveness).

**How does it work?**
It acts as the deterministic bridge between the Cloud (FastAPI), the AI (ONNX), and the physical Actuators (Arduino). It runs a blazingly fast Finite State Machine (`TinyFSM`) fed by a **Lock-Free Event Queue**. This means the network, vision, and hardware threads can operate entirely asynchronously without ever blocking one another.

## 🚀 Key Architectural Features
- **Lock-Free Concurrency:** Eliminates mutex bottlenecks.
- **TinyFSM State Machine:** Guarantees safe transitions between `Boot_Init`, `Idle`, `Offline_Idle`, `Inspection`, `DryClean`, `WetClean`, `Error`, and `Hardware_Fault` states without spaghetti code.
- **Atomic Hardware Safety:** Mechanical emergency stops bypass the software queue using an `std::atomic<bool>` flag to halt actuators in sub-milliseconds.
- **Offline Resilience:** Persists MQTT telemetry to a local disk cache if Wi-Fi drops.
- **True Zero-Copy Vision:** Uses DMA-BUFs to pass hardware memory directly to the inference engine, saving critical CPU cycles.

## 🏗️ Phased Execution Plan
To ensure a stable build, the daemon is being constructed in four progressive phases. Each phase produces a standalone, testable binary.

* **Phase 1: The Skeleton (FSM & Event Loop)** -- done
  * Core C++17 application, lock-free event queue, and the full TinyFSM state machine, all driven by mock hardware/network/vision implementations. Boots, self-tests, runs a complete cleaning cycle, and reacts to E-Stops and network drops -- all without any real hardware, broker, or camera attached.
* **Phase 2: Hardware Abstraction Layer (Serial/UART) & Malleable Contracts** -- done
  * Config-driven opcode mapping and RFC 6901 tolerant JSON decoders.
  * Real POSIX serial driver (`SerialHardwareDriver`) for UART/USB Arduino communication, background reader thread, sub-millisecond atomic E-Stop latching, systemd unit, and offline disk telemetry cache.
* **Phase 3: Network & Telemetry Layer (MQTT Integration)**
  * Integrating Eclipse Paho MQTT client for live cloud communication and cache synchronization.
* **Phase 4: AI Vision Integration**
  * Hooking up the camera pipeline and ONNX Runtime to parse bounding boxes and feed physical cleaning decisions back into the state machine.

## 🛠️ Build Instructions

**Prerequisites (Phase 1):**
* Modern C++ compiler (C++17), CMake >= 3.16, and network access on first configure (dependencies are fetched via CMake `FetchContent`: nlohmann/json, moodycamel::ConcurrentQueue, tinyfsm, Catch2 -- no manual submodule setup needed)

Phase 2-4 add real hardware/network/vision dependencies (a serial port, Eclipse Paho MQTT C++, OpenCV/ONNX Runtime) as each phase's mock is swapped for a real implementation.

**Build and test:**
```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

**Run the daemon (mocks only, Phase 1):**
```sh
./build/solar_edge_daemon config/daemon.config.json
```
This boots through self-test into `Idle`/`Offline_Idle` and drops into an interactive console -- type `help` for the list of commands (`start`, `debris <type>`, `estop`, `network-lost`, etc.) to drive the FSM through a full cleaning cycle without any physical hardware attached.
