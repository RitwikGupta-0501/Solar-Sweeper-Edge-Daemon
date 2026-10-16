# Phase 4 Implementation Plan: AI Vision Integration (Edge Inference & Optical Gantry Kinematics)

**Milestone:** Phase 4 (AI Vision Integration)  
**Phase:** 4 of 4  
**Status:** Ready for Execution / Architectural Review  
**Target Branch:** `feat/phase-4-ai-vision` $\rightarrow$ `master`  

---

## 1. Executive Summary, Theoretical Grounding & Pre-Empted Bug Mitigations

### 1.1 Executive Scope
Phase 4 represents the culminating milestone of the **Solar Sweeper Edge Daemon**: replacing the deterministic canned responses of [`MockVisionEngine`](file:///home/ritwikg/Repository/capstone/src/Mocks/MockVisionEngine.cpp) with an operational, high-throughput, edge-accelerated AI vision pipeline. The Edge Daemon acts as the autonomous central brain on a Raspberry Pi 4/5 SBC. While Phase 2 established asynchronous UART motor/solenoid actuation and Phase 3 established robust MQTT telemetry and offline spooling, Phase 4 provides the perceptual intelligence that drives cleaning decisions.

The vision pipeline ingests real-time video frames from a leading-edge wide-angle look-ahead camera, executes tensor preprocessing, performs deep learning object detection using **ONNX Runtime (C++ API)**, executes Non-Maximum Suppression (NMS), projects optical pixel coordinates to physical panel coordinates, and injects actionable events ([`EvDebrisDetected`](file:///home/ritwikg/Repository/capstone/include/solaredge/Core/Events.h) and [`EvSectorClear`](file:///home/ritwikg/Repository/capstone/include/solaredge/Core/Events.h)) into the lock-free [`EventQueue`](file:///home/ritwikg/Repository/capstone/include/solaredge/Core/EventQueue.h). This unlocks continuous gantry motion where dry sweeping and targeted high-pressure water pulse spray actuation occur dynamically without stopping the machine.

### 1.2 Governing Research & Standards
The architecture of this vision system is governed by foundational computer vision and embedded real-time systems literature:
1. **Real-Time Object Detection:** Redmon et al., *You Only Look Once: Unified, Real-Time Object Detection* (CVPR 2016, arXiv:1506.02640); Jocher et al., *Ultralytics YOLO Architecture & Anchor-Free Decoupled Head* (2023).
2. **Efficient Non-Maximum Suppression:** Neubeck & Van Gool, *Efficient Non-Maximum Suppression* (ICPR 2006). Vectorized greedy IoU box clustering.
3. **Linux Kernel Zero-Copy Streaming:** *Linux Video4Linux2 (V4L2) API Specification*, Kernel Memory-Mapped Buffers (`V4L2_MEMORY_MMAP`) and Direct Memory Access (`DMA-BUF`) streaming pipelines (POSIX/Linux Kernel 5.x+).
4. **Edge Tensor Inference Acceleration:** Microsoft ONNX Runtime Team, *ONNX Runtime: Cross-Platform, High Performance ML Inferencing and Training Accelerator* (2021); ARM Compute Library & NEON SIMD acceleration.

---

### 1.3 End-to-End Architectural Dataflow

```mermaid
flowchart TD
    subgraph Hardware Layer ["Hardware & Sensor Layer"]
        CAM["Leading-Edge Camera (/dev/video0)"] -->|V4L2 MMAP Ring Buffer| V4L2["V4L2CameraPipeline"]
    end

    subgraph Preprocessing Layer ["In-Place Tensor Preprocessing"]
        V4L2 -->|Raw YUYV/RGB Buffer| PP["Letterbox & Normalization Engine"]
        PP -->|NCHW float32 Tensor [1, 3, 640, 640]| ORT_IN["ONNX Input Memory"]
    end

    subgraph Inference Layer ["Edge Neural Inference Engine"]
        ORT_IN --> SESS["OnnxVisionEngine (Ort::Session)"]
        MODEL[("debris_detector.onnx\n(YOLOv8s Decoupled Head)")] -.-> SESS
        SESS -->|Raw Output Tensor [1, 84, 8400]| POST["Post-Processing & NMS"]
    end

    subgraph Spatial & Decision Layer ["Spatial Kinematics & Arbitration"]
        POST -->|Detections {class_id, conf, bbox}| MAP["SpatialCoordinateMapper"]
        MAP -->|Pixel to Panel Projection| KIN["Continuous Kinematics Compensator"]
        KIN -->|Debris Classified & Prioritized| ARB{"Decision Fork"}
        ARB -->|Bird Dropping Found| WET["EvDebrisDetected (BirdDropping, WetClean)"]
        ARB -->|Dust / Leaf Found| DRY["EvDebrisDetected (Dust/Leaf, DryClean)"]
        ARB -->|No Obstructions| CLR["EvSectorClear"]
    end

    subgraph Core System ["Deterministic Edge Daemon Core"]
        WET -->|moodycamel Queue| Q["Lock-Free EventQueue"]
        DRY -->|moodycamel Queue| Q
        CLR -->|moodycamel Queue| Q
        Q --> FSM["TinyFSM State Machine (Inspection State)"]
        FSM -->|Serial UART 115200| ARDUINO["Arduino Actuators (Brush / Solenoids)"]
        FSM -->|MQTT Telemetry| CLOUD["FastAPI Cloud Backend"]
    end
```

---

### 1.4 The Pre-Mortem Bug Hunt: Pre-Empted Architectural Traps

| # | Architectural Trap | Naïve Failure Mode | Root Cause Analysis | Production Mitigation in this Plan |
|---|---|---|---|---|
| **T.1** | **Memory Bandwidth & CPU Thrashing via OpenCV `cv::Mat` Copies** | High CPU consumption ($> 85\%$), thermal throttling, and frame drop. | Naïve `cv::VideoCapture::read()` allocates heap buffers and performs 3 distinct memory copies (Kernel $\to$ Userland $\to$ BGR Mat $\to$ RGB float Mat). | **V4L2 Memory-Mapped (`v4l2_buffer`) Ring Buffers:** Zero-copy mapped kernel pages directly addressed by a dedicated NEON SIMD strided in-place normalizer writing directly into pre-allocated ONNX input tensors. |
| **T.2** | **Letterbox Aspect Ratio Distortion Skewing Spatial Coordinates** | Misalignment between detected debris coordinates and physical panel coordinates ($> 5\text{ cm}$ error). | Directly resizing a 16:9 or 4:3 camera frame to $640 \times 640$ distorts spatial coordinates non-linearly. | **Strict Letterbox Preserving Transform & Inverse Affine Matrix:** Image aspect ratio is preserved with centered padding; inverse scale and translation offsets ($\Delta x, \Delta y$) are strictly applied before spatial sector mapping. |
| **T.3** | **Continuous Travel Kinematic Latency Race (Capture vs. Actuation Drift)** | Water spray missing the bird dropping because the gantry moved during inference. | Gantry travels at $v = 0.15\text{ m/s}$. If frame capture timestamp is neglected and inference takes $80\text{ ms}$, robot moves $1.2\text{ cm}$; under heavy load ($200\text{ ms}$), it moves $3.0\text{ cm}$. | **Timestamped Odometry Delta Compensation:** Every frame is stamped with monotonic `capture_time_us`. The nozzle actuation trigger applies velocity delta $\Delta x = v \cdot (t_{\text{act}} - t_{\text{cap}})$ to guarantee sub-millimeter nozzle pulse precision. |
| **T.4** | **Boundary-Straddling Debris & Duplicate Cleaning Cycle Triggers** | Robot performs WetClean on Sector 0, advances to Sector 1, and re-triggers WetClean on the exact same spot. | A bird dropping located on the boundary between Sector 0 and Sector 1 appears in both wide-angle frames. | **Geometric Centroid Hysteresis & Visited Boundary Suppression:** Debris assignment is strictly bound to bounding box center-of-mass $x_{\text{world}}$. Boundary overlap buffer ($5\text{ cm}$) deduplicates detections across adjacent sector passes. |
| **T.5** | **ONNX Runtime Thread Contention Starving the FSM & Serial Loop** | Dropped serial interrupts, missing Arduino limit-switch E-Stop byte, physical gantry crash. | Default ONNX Runtime session spawns thread pool equal to `nproc` (4 threads on Pi), saturating all cores during matrix GEMM forward pass. | **Thread Pool Clamping & Affinity Partitioning:** ONNX session clamped to `intra_op_num_threads = 2`, `inter_op_num_threads = 1`. FSM and Serial HAL reader threads run at elevated POSIX priority (`SCHED_FIFO` / `SCHED_RR`). |

---

## 2. Mathematical Formulations & Formal Invariants

### 2.1 Optical Geometry, FOV & Gantry Kinematic Timing
The camera is rigidly mounted on a central mast at the leading edge of the gantry at mounting height $H_{\text{cam}}$ above the solar panel plane, angled downward by pitch angle $\theta$.

```
           Look-Ahead Distance (D_lookahead)
       ◄──────────────────────────────────────►
       Leading Bumper                          Gantry Trailing Tools
      ┌─────────────┐                         ┌───────────────────────┐
      │  [Camera]   │                         │ [Nozzles]   [Brushes] │
      └──────┬──────┘                         └───────────┬───────────┘
             │                                            │
             │ Optical Axis                               │ Actuation Plane
             ▼                                            ▼
══════════════════════════════════════════════════════════════════════ (Solar Array)
```

The time delta $t_{\text{actuation}}$ required for the water nozzle manifold to align with detected debris located at longitudinal distance $x_{\text{debris}}$ relative to the camera is formulated as:

$$t_{\text{actuation}} = t_{\text{capture}} + \frac{D_{\text{lookahead}} - x_{\text{debris}}}{v_{\text{gantry}}}$$

Where:
- $v_{\text{gantry}} \in [0.05, 0.30]\text{ m/s}$ is the forward velocity of the machine (calibrated default $0.15\text{ m/s}$).
- $D_{\text{lookahead}} = 0.20\text{ m}$ is the fixed distance between the camera optical center and the solenoid nozzle manifold.
- $x_{\text{debris}}$ is the forward longitudinal offset of the debris in camera coordinate space ($x_{\text{debris}} \in [0.0, 0.40]\text{ m}$).
- Boundary Condition: If $x_{\text{debris}} > D_{\text{lookahead}}$, actuation timer is clamped to $0$ (immediate pulse upon tool entry).

---

### 2.2 Aspect-Ratio Preserving Letterbox & Coordinate Inversion
To feed arbitrary camera sensor resolutions ($W_{\text{cam}} \times H_{\text{cam}}$) into a square YOLO neural network input ($W_{\text{net}} \times H_{\text{net}} = 640 \times 640$), the scaling factor $r$ and padding offsets $(\Delta x, \Delta y)$ are formally defined as:

$$r = \min\left( \frac{W_{\text{net}}}{W_{\text{cam}}}, \frac{H_{\text{net}}}{H_{\text{cam}}} \right)$$

$$\Delta x = \frac{W_{\text{net}} - (W_{\text{cam}} \cdot r)}{2}, \quad \Delta y = \frac{H_{\text{net}} - (H_{\text{cam}} \cdot r)}{2}$$

Given a bounding box prediction in network tensor space $[x_1^{\text{net}}, y_1^{\text{net}}, x_2^{\text{net}}, y_2^{\text{net}}]$, the inverse affine mapping to native camera sensor coordinates is:

$$x_1^{\text{cam}} = \frac{x_1^{\text{net}} - \Delta x}{r}, \quad x_2^{\text{cam}} = \frac{x_2^{\text{net}} - \Delta x}{r}$$

$$y_1^{\text{cam}} = \frac{y_1^{\text{net}} - \Delta y}{r}, \quad y_2^{\text{cam}} = \frac{y_2^{\text{net}} - \Delta y}{r}$$

**Formal Clamping Invariant:**
$$x_1^{\text{cam}} = \max\left(0.0, \min\left(x_1^{\text{cam}}, W_{\text{cam}} - 1\right)\right)$$
$$y_1^{\text{cam}} = \max\left(0.0, \min\left(y_1^{\text{cam}}, H_{\text{cam}} - 1\right)\right)$$

---

### 2.3 Vectorized Non-Maximum Suppression (NMS) & Intersection-Over-Union (IoU)
For two predicted candidate bounding boxes $B_i$ and $B_j$:

$$\text{IoU}(B_i, B_j) = \frac{\text{Area}(B_i \cap B_j)}{\text{Area}(B_i \cup B_j)} = \frac{\max\left(0, x_2^{\cap} - x_1^{\cap}\right) \cdot \max\left(0, y_2^{\cap} - y_1^{\cap}\right)}{\text{Area}(B_i) + \text{Area}(B_j) - \text{Area}(B_i \cap B_j)}$$

Where:
$$x_1^{\cap} = \max(x_{1,i}, x_{1,j}), \quad y_1^{\cap} = \max(y_{1,i}, y_{1,j})$$
$$x_2^{\cap} = \min(x_{2,i}, x_{2,j}), \quad y_2^{\cap} = \min(y_{2,i}, y_{2,j})$$

**Suppression Rule:** Given sorted detection candidates by confidence score $C_1 \ge C_2 \ge \dots \ge C_m$:
Candidate $B_j$ is discarded if $\exists B_i$ ($i < j$) such that $\text{Class}(B_i) = \text{Class}(B_j)$ and $\text{IoU}(B_i, B_j) > \tau_{\text{IoU}}$, where $\tau_{\text{IoU}} = 0.45$.

---

### 2.4 Multi-Label Debris Classification & Severity Arbitration
A sector may contain multiple concurrent debris types (e.g., general dust with localized bird droppings). The state machine requires an unambiguous, deterministic mapping to either [`DryClean`](file:///home/ritwikg/Repository/capstone/include/solaredge/Core/States.h) or [`WetClean`](file:///home/ritwikg/Repository/capstone/include/solaredge/Core/States.h).

Let $D = \{d_1, d_2, \dots, d_k\}$ be the set of valid post-NMS detections in sector $S$. Each debris class has a formal severity weight:

$$\text{Weight}(\text{BirdDropping}) = 3, \quad \text{Weight}(\text{Leaf}) = 2, \quad \text{Weight}(\text{Dust}) = 1, \quad \text{Weight}(\text{None}) = 0$$

The aggregated sector decision is determined by the maximum severity operator:

$$d^* = \arg\max_{d \in D} \left( \text{Weight}(\text{Class}(d)) \right)$$

$$\text{FSM Action} = \begin{cases} 
\text{WetClean} & \text{if } \text{Weight}(d^*) \ge 3 \ (\text{BirdDropping}) \\
\text{DryClean} & \text{if } 1 \le \text{Weight}(d^*) < 3 \ (\text{Dust or Leaf}) \\
\text{Advance/Skip} & \text{if } D = \emptyset \ (\text{Sector Clear})
\end{cases}$$

---

## 3. Component Implementation Breakdown (File-by-File)

```
include/solaredge/
├── Config/
│   └── Config.h                        [MODIFY: Add vision pipeline parameters]
├── Interfaces/
│   └── IVisionEngine.h                 [MODIFY: Add frame ingestion & profiling signatures]
└── Vision/
    ├── VisionTypes.h                   [NEW: BoundingBox, Detection, CameraFrame, IngestParams]
    ├── V4L2CameraPipeline.h            [NEW: Direct Linux kernel MMAP video streamer]
    ├── SpatialCoordinateMapper.h       [NEW: Optical pixel to gantry physical coordinate mapper]
    └── OnnxVisionEngine.h              [NEW: ONNX Runtime C++ engine with SIMD normalization]

src/
├── Config/
│   └── Config.cpp                      [MODIFY: Parse vision.* JSON configuration fields]
├── Vision/
    ├── V4L2CameraPipeline.cpp          [NEW: V4L2 buffer lifecycle, ioctl handling, streaming]
    ├── SpatialCoordinateMapper.cpp      [NEW: Inverse letterbox & nozzle selector]
    └── OnnxVisionEngine.cpp            [NEW: Model inference, NMS, and FSM event injection]
├── App/
│   └── Application.cpp                 [MODIFY: Dynamically wire OnnxVisionEngine if mock=false]
└── main.cpp                            [UNCHANGED]

tests/
├── CMakeLists.txt                      [MODIFY: Add test_vision_engine.cpp & test_spatial_mapper.cpp]
├── test_spatial_mapper.cpp             [NEW: Unit tests for optical math & coordinate inversion]
└── test_vision_engine.cpp              [NEW: Catch2 tests for ONNX inference & synthetic frames]
```

---

### 3.1 Data Structures: `include/solaredge/Vision/VisionTypes.h` [NEW]

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "solaredge/Core/Events.h"

namespace sse::vision {

struct BoundingBox {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;

    float width() const { return x2 - x1; }
    float height() const { return y2 - y1; }
    float area() const { return width() * height(); }
};

struct Detection {
    BoundingBox box;
    events::DebrisType debrisType = events::DebrisType::Unknown;
    int classId = -1;
    float confidence = 0.0f;
    
    // Physical coordinate relative to gantry bumper (in millimeters)
    float physicalXmm = 0.0f;
    float physicalYmm = 0.0f;
    int targetNozzleId = 0; // 0 to 3
};

struct CameraFrame {
    int width = 0;
    int height = 0;
    int channels = 3;
    uint32_t pixelFormat = 0; // V4L2 fourcc code
    uint64_t timestampUs = 0;
    std::vector<uint8_t> data;
};

struct PreprocessingParams {
    int netWidth = 640;
    int netHeight = 640;
    float scale = 1.0f;
    float padX = 0.0f;
    float padY = 0.0f;
};

} // namespace sse::vision
```

---

### 3.2 Configuration Expansion: `include/solaredge/Config/Config.h` & `src/Config/Config.cpp` [MODIFY]

Update `VisionConfig` in [`include/solaredge/Config/Config.h`](file:///home/ritwikg/Repository/capstone/include/solaredge/Config/Config.h) to parse and store production vision parameters:

```cpp
struct VisionConfig {
    std::string modelPath = "models/debris_detector.onnx";
    std::string cameraDevice = "/dev/video0";
    int cameraIndex = 0;
    int imageWidth = 1920;
    int imageHeight = 1080;
    int framerate = 30;
    float confidenceThreshold = 0.50f;
    float nmsIouThreshold = 0.45f;
    int intraOpThreads = 2;
    float gantrySpeedMps = 0.15f;
    float lookAheadDistanceM = 0.20f;
    int totalNozzles = 4;
};
```

Update [`src/Config/Config.cpp`](file:///home/ritwikg/Repository/capstone/src/Config/Config.cpp) with strict validation and fallbacks:
```cpp
// In ParseVisionConfig:
config.modelPath = section.value("model_path", "models/debris_detector.onnx");
config.cameraDevice = section.value("camera_device", "/dev/video0");
config.imageWidth = section.value("image_width", 1920);
config.imageHeight = section.value("image_height", 1080);
config.confidenceThreshold = section.value("confidence_threshold", 0.50f);
config.nmsIouThreshold = section.value("nms_iou_threshold", 0.45f);
config.intraOpThreads = section.value("intra_op_threads", 2);
config.gantrySpeedMps = section.value("gantry_speed_mps", 0.15f);
config.lookAheadDistanceM = section.value("look_ahead_distance_m", 0.20f);
config.totalNozzles = section.value("total_nozzles", 4);
```

---

### 3.3 Linux V4L2 Memory-Mapped Camera Pipeline: `include/solaredge/Vision/V4L2CameraPipeline.h` [NEW]

```cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <linux/videodev2.h>
#include "solaredge/Vision/VisionTypes.h"

namespace sse::vision {

class V4L2CameraPipeline {
public:
    explicit V4L2CameraPipeline(std::string devicePath = "/dev/video0",
                                int width = 1920, int height = 1080, int fps = 30);
    ~V4L2CameraPipeline();

    bool openDevice();
    bool startStreaming();
    bool grabFrame(CameraFrame& outFrame, int timeoutMs = 1000);
    void stopStreaming();
    void closeDevice();

    bool isOpened() const { return deviceFd_ >= 0; }
    bool isStreaming() const { return streaming_.load(); }

private:
    struct Buffer {
        void* start = nullptr;
        size_t length = 0;
    };

    std::string devicePath_;
    int width_;
    int height_;
    int fps_;
    int deviceFd_ = -1;
    std::atomic<bool> streaming_{false};

    std::vector<Buffer> buffers_;
};

} // namespace sse::vision
```

#### Detailed Implementation Highlights (`src/Vision/V4L2CameraPipeline.cpp`):
1. **Device Open & Caps Verification:** Opens `devicePath_` with `O_RDWR | O_NONBLOCK`. Queries `VIDIOC_QUERYCAP` to verify `V4L2_CAP_VIDEO_CAPTURE` and `V4L2_CAP_STREAMING`.
2. **Format Negotiation:** Sets `V4L2_PIX_FMT_YUYV` (or `V4L2_PIX_FMT_MJPEG` depending on webcam hardware caps) using `VIDIOC_S_FMT`.
3. **Mmap Buffer Allocation:** Requests 4 buffers via `VIDIOC_REQBUFS` (`V4L2_MEMORY_MMAP`). Maps each buffer pointer into userspace via `mmap(MAP_SHARED)`. Enqueues all buffers to the kernel with `VIDIOC_QBUF`.
4. **Frame Capture with `select()`:** Listens on `deviceFd_` with a high-resolution POSIX `select()` timeout. Executes `VIDIOC_DQBUF` to pop the captured frame, copies buffer payload with zero kernel context switches, and immediately requeues the buffer via `VIDIOC_QBUF`.

---

### 3.4 Spatial Coordinate Mapper: `include/solaredge/Vision/SpatialCoordinateMapper.h` [NEW]

```cpp
#pragma once

#include "solaredge/Vision/VisionTypes.h"

namespace sse::vision {

class SpatialCoordinateMapper {
public:
    SpatialCoordinateMapper(int cameraWidth, int cameraHeight,
                            float panelHeightM, float lookAheadM,
                            int totalNozzles, float gantrySpeedMps);

    // Converts network detection bounding box to physical panel millimeter offsets
    Detection mapToPhysical(const BoundingBox& netBox, int classId, float confidence,
                            const PreprocessingParams& prep);

    // Calculates which nozzle (0 to totalNozzles-1) covers the given vertical coordinate
    int selectNozzle(float physicalYmm) const;

    // Calculates actuation delay (in milliseconds) from look-ahead distance and velocity
    int calculateActuationDelayMs(float physicalXmm) const;

private:
    int cameraWidth_;
    int cameraHeight_;
    float panelHeightM_;
    float lookAheadM_;
    int totalNozzles_;
    float gantrySpeedMps_;
};

} // namespace sse::vision
```

---

### 3.5 Concrete ONNX Runtime Engine: `include/solaredge/Vision/OnnxVisionEngine.h` [NEW]

```cpp
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Interfaces/IVisionEngine.h"
#include "solaredge/Vision/SpatialCoordinateMapper.h"
#include "solaredge/Vision/V4L2CameraPipeline.h"
#include "solaredge/Vision/VisionTypes.h"

// Forward declaration of ONNX Runtime C++ types
namespace Ort {
    class Env;
    class Session;
    class SessionOptions;
    class MemoryInfo;
    class Value;
}

namespace sse::vision {

class OnnxVisionEngine : public iface::IVisionEngine {
public:
    OnnxVisionEngine(core::EventQueue& queue, const config::Config& config);
    ~OnnxVisionEngine() override;

    void initialize() override;
    void requestInference(int sectorId) override;
    void shutdown() override;

    // Public test inspection hook: execute inference on a pre-supplied CameraFrame
    std::vector<Detection> inferFrame(const CameraFrame& frame, float& outInferenceMs);

private:
    void workerLoop();
    void preprocessFrame(const CameraFrame& frame, std::vector<float>& outTensor, PreprocessingParams& outParams);
    std::vector<Detection> postprocessOutput(const float* rawOutput, int outputRows, int outputCols,
                                            const PreprocessingParams& params);
    void applyNMS(std::vector<Detection>& candidates, float iouThreshold);

    core::EventQueue& queue_;
    config::Config config_;
    SpatialCoordinateMapper mapper_;
    std::unique_ptr<V4L2CameraPipeline> camera_;

    // ONNX Runtime session handles
    std::unique_ptr<Ort::Env> ortEnv_;
    std::unique_ptr<Ort::Session> ortSession_;
    std::vector<const char*> inputNames_;
    std::vector<const char*> outputNames_;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};

    enum class TaskType { None, Initialize, Inference };
    TaskType pendingTask_ = TaskType::None;
    int pendingSectorId_ = 0;
};

} // namespace sse::vision
```

---

### 3.6 Application Injection: `src/App/Application.cpp` [MODIFY]

Update [`src/App/Application.cpp`](file:///home/ritwikg/Repository/capstone/src/App/Application.cpp) to dynamically instantiate `OnnxVisionEngine` when `mock.enabled = false`:

```cpp
#include "solaredge/Vision/OnnxVisionEngine.h"

// In Application constructor:
vision_(config_.mock().enabled
    ? static_cast<std::unique_ptr<iface::IVisionEngine>>(std::make_unique<mocks::MockVisionEngine>(queue_, config_.mock()))
    : static_cast<std::unique_ptr<iface::IVisionEngine>>(std::make_unique<vision::OnnxVisionEngine>(queue_, config_)))
```

---

### 3.7 CMake Integration: `CMakeLists.txt` [MODIFY]

Add ONNX Runtime to [`CMakeLists.txt`](file:///home/ritwikg/Repository/capstone/CMakeLists.txt). On Linux systems, ONNX Runtime is acquired via `FetchContent` targeting official precompiled C/C++ release tarballs (or via system `find_package`):

```cmake
# --- ONNX Runtime Engine ---
set(ONNXRUNTIME_VERSION "1.18.0")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(ORT_ARCH "aarch64")
else()
  set(ORT_ARCH "x64")
endif()

FetchContent_Declare(onnxruntime_bin
  URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-${ORT_ARCH}-${ONNXRUNTIME_VERSION}.tgz"
)
FetchContent_MakeAvailable(onnxruntime_bin)

add_library(onnxruntime INTERFACE)
target_include_directories(onnxruntime INTERFACE ${onnxruntime_bin_SOURCE_DIR}/include)
target_link_libraries(onnxruntime INTERFACE ${onnxruntime_bin_SOURCE_DIR}/lib/libonnxruntime.so)

# Add Vision sources to daemon_core
add_library(daemon_core STATIC
  ...
  src/Vision/V4L2CameraPipeline.cpp
  src/Vision/SpatialCoordinateMapper.cpp
  src/Vision/OnnxVisionEngine.cpp
  ...
)

target_link_libraries(daemon_core PUBLIC
  ...
  onnxruntime
)
```

---

## 4. Atomic Execution & Commit Roadmap

To preserve git bisectability and maintain clean pull requests, Phase 4 will be developed and committed across **8 atomic commits**:

```
Commit 1: build: configure ONNX Runtime and V4L2 kernel dependencies in CMake
Commit 2: feat(vision): define spatial data structures and enhanced VisionTypes
Commit 3: feat(vision): implement V4L2 zero-copy memory-mapped camera pipeline
Commit 4: feat(vision): implement spatial coordinate mapper and nozzle geometry
Commit 5: feat(vision): implement in-place letterbox preprocessing and vectorized NMS
Commit 6: feat(vision): implement OnnxVisionEngine with decoupled YOLO inference
Commit 7: feat(app): inject OnnxVisionEngine into Application composition root
Commit 8: test(vision): author comprehensive Catch2 vision and spatial test suite
```

---

## 5. Verification Scenarios & Test Suite (Falsifiable Acceptance Criteria)

All verification tests are implemented in [`tests/test_vision_engine.cpp`](file:///home/ritwikg/Repository/capstone/tests/test_vision_engine.cpp) and [`tests/test_spatial_mapper.cpp`](file:///home/ritwikg/Repository/capstone/tests/test_spatial_mapper.cpp) running under Catch2 v3.

### Formal Acceptance Verification Matrix

| # | Test Scenario | Preconditions | Input Stimulus / Action | Asserted Invariants | Edge Case Verified |
|---|---|---|---|---|---|
| **V.1** | **Inverse Letterbox Coordinate Reconstruction** | 1080p camera frame scaled to $640 \times 640$ input tensor. | Predict bounding box at $[100, 100, 200, 200]$ in tensor space. | $\text{box.x1} = \frac{100 - \text{padX}}{\text{scale}}$, $\text{box.y1} = \frac{100 - \text{padY}}{\text{scale}}$. Assert sub-pixel accuracy $< 0.01\text{ px}$. | Non-square aspect ratio ($16:9 \to 1:1$). |
| **V.2** | **Greedy NMS Deduplication** | 3 overlapping bounding boxes for `BirdDropping` with IoUs: $0.85$, $0.60$, $0.10$. | Run `applyNMS(candidates, 0.45)`. | Boxes with $\text{IoU} \ge 0.45$ are suppressed; box with $\text{IoU} = 0.10$ is preserved. Output size equals 2. | High candidate density. |
| **V.3** | **Multi-Label Severity Arbitration** | Frame contains $5 \times \text{Dust}$ detections and $1 \times \text{BirdDropping}$ detection. | Run `postprocessOutput` and dispatch to FSM. | Highest severity wins: State Machine transitions to `WetClean` (not `DryClean`). `birdDroppingCount` increments. | Co-occurring contamination. |
| **V.4** | **Nozzle Array Zone Selection** | Gantry equipped with 4 nozzles across $1.6\text{ m}$ panel height. | Pass physical coordinates $y = 200\text{ mm}$, $600\text{ mm}$, $1000\text{ mm}$, $1400\text{ mm}$. | Correct nozzle IDs selected ($0, 1, 2, 3$). Clamping asserts $y > 1600\text{ mm}$ defaults to nozzle 3. | Out-of-bounds bounding boxes. |
| **V.5** | **Look-Ahead Kinematic Delay Calculation** | $v = 0.15\text{ m/s}$, $D_{\text{lookahead}} = 0.20\text{ m}$, debris at $x = 0.05\text{ m}$. | Call `calculateActuationDelayMs(50.0f)`. | $\Delta t = \frac{0.20 - 0.05}{0.15} = 1.0\text{ s} \ (1000\text{ ms})$. Assert exact delay $\pm 1\text{ ms}$. | High/low velocity boundary values. |
| **V.6** | **Session Warmup & Memory Leaks** | ONNX Runtime initialized with dummy weights. | Execute 100 consecutive `inferFrame` cycles. | Peak RSS memory remains flat ($< 1\text{ MB}$ variation); zero memory leaks detected via AddressSanitizer. | Long-running daemon stability. |
| **V.7** | **Zero Detection Fallback to EvSectorClear** | Synthetic frame with zero debris above confidence threshold $0.50$. | Call `requestInference(sectorId = 3)`. | Event queue receives [`EvSectorClear{3}`](file:///home/ritwikg/Repository/capstone/include/solaredge/Core/Events.h). State machine bypasses cleaning actuators. | Clean solar panel pass. |
| **V.8** | **Camera Timeout & Degraded Hardware Fault** | Camera disconnected or `grabFrame` times out ($> 1000\text{ ms}$). | Call `requestInference(0)`. | Queue receives [`EvBootVisionFailed`](file:///home/ritwikg/Repository/capstone/include/solaredge/Core/Events.h). State machine escalates to `Hardware_Fault` after retries. | Physical cable disconnect / USB stall. |
