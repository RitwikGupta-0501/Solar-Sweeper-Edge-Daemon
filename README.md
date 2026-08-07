# Solar Sweeper: Edge Daemon 🤖⚡

## 🧠 What is the Edge Daemon?

The C++ Edge Daemon is the "Central Brain" of the Automatic Solar Panel Cleaning Machine. It is a highly optimized, multithreaded application designed to run on a Raspberry Pi. 

**Why does it exist?**
Current automated cleaning robots often rely on monolithic, blocking hardware loops (like a simple Arduino `loop()`). When trying to introduce heavy AI vision models and high-frequency cloud telemetry into those systems, the hardware suffers from race conditions, massive latency, and safety failures (like missing a limit switch hit because the CPU was busy processing an image). The Edge Daemon exists to decouple these systems—ensuring the robot is both "smart" (AI-driven) and "safe" (real-time hardware responsiveness).

**How does it work?**
It acts as the deterministic bridge between the Cloud (FastAPI), the AI (ONNX), and the physical Actuators (Arduino). It runs a blazingly fast Finite State Machine (`TinyFSM`) fed by a **Lock-Free Event Queue**. This means the network, vision, and hardware threads can operate entirely asynchronously without ever blocking one another.

## 🚀 Key Architectural Features
- **Lock-Free Concurrency:** Eliminates mutex bottlenecks.
- **TinyFSM State Machine:** Guarantees safe transitions between `Idle`, `Inspection`, `Dry_Clean`, `Wet_Clean`, and `Error` states without spaghetti code.
- **Atomic Hardware Safety:** Mechanical emergency stops bypass the software queue using an `std::atomic<bool>` flag to halt actuators in sub-milliseconds.
- **Offline Resilience:** Persists MQTT telemetry to a local disk cache if Wi-Fi drops.
- **True Zero-Copy Vision:** Uses DMA-BUFs to pass hardware memory directly to the inference engine, saving critical CPU cycles.

## 🏗️ Phased Execution Plan
To ensure a stable build, the daemon is being constructed in four progressive phases. Each phase produces a standalone, testable binary.

* **Phase 1: The Skeleton (FSM & Event Loop)**
  * Building the core C++17 application, implementing the lock-free event queue, and wiring the TinyFSM state transitions using mocked events.
* **Phase 2: Hardware Abstraction Layer**
  * Establishing UART serial communication with the Arduino to dispatch physical commands and listen for hardware interrupts (E-Stops).
* **Phase 3: Network & Telemetry Layer**
  * Integrating Eclipse Paho MQTT for asynchronous cloud communication, including offline disk-caching to survive network drops.
* **Phase 4: AI Vision Integration**
  * Hooking up the camera pipeline and ONNX Runtime to parse bounding boxes and feed physical cleaning decisions back into the state machine.

## 🛠️ Build Instructions
*(Detailed CMake build instructions will be populated here as Phase 1 commences.)*

**Prerequisites:**
* Modern C++ Compiler (C++17 or C++20)
* CMake >= 3.15
* Eclipse Paho MQTT C++
* OpenCV 4.x / ONNX Runtime
