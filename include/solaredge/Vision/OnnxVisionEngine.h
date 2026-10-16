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

namespace Ort {
    struct Env;
    struct Session;
    struct MemoryInfo;
}

namespace sse::vision {

class OnnxVisionEngine : public iface::IVisionEngine {
public:
    OnnxVisionEngine(core::EventQueue& queue,
                     const config::Config& config,
                     std::unique_ptr<V4L2CameraPipeline> customCamera = nullptr);
    ~OnnxVisionEngine() override;

    void initialize() override;
    void requestInference(int sectorId) override;
    void shutdown() override;

    // Public inference hook for testing with pre-supplied CameraFrame
    std::vector<Detection> inferFrame(const CameraFrame& frame, float& outInferenceMs);

    // Image pre-processing & aspect-preserving letterbox
    static void preprocessFrame(const CameraFrame& frame,
                                int targetWidth, int targetHeight,
                                std::vector<float>& outTensor,
                                PreprocessingParams& outParams);

    // Vectorized greedy NMS algorithm
    static void applyNMS(std::vector<Detection>& candidates, float iouThreshold);

    // Calculate Intersection over Union
    static float computeIoU(const BoundingBox& a, const BoundingBox& b);

    // Multi-label severity arbitration: BirdDropping (3) > Leaf (2) > Dust (1) > None (0)
    static events::DebrisType arbitrateDebris(const std::vector<Detection>& detections, float& outBestConfidence);

    // Post-process raw YOLO output tensor
    std::vector<Detection> postprocessOutput(const float* rawOutput,
                                            int dim1, int dim2,
                                            const PreprocessingParams& params);

    // Frame processing and queue dispatch
    void processFrameAndDispatch(const CameraFrame& frame, int sectorId, float captureMs);

    bool isInitialized() const { return initialized_.load(); }
    const SpatialCoordinateMapper& mapper() const { return mapper_; }

private:
    void workerLoop();
    void doInitialize();
    void doInference(int sectorId);

    core::EventQueue& queue_;
    config::Config config_;
    SpatialCoordinateMapper mapper_;
    std::unique_ptr<V4L2CameraPipeline> camera_;

    // ONNX Runtime state
    std::unique_ptr<Ort::Env> ortEnv_;
    std::unique_ptr<Ort::Session> ortSession_;
    std::unique_ptr<Ort::MemoryInfo> memoryInfo_;
    std::vector<std::string> inputNamesAlloc_;
    std::vector<std::string> outputNamesAlloc_;
    std::vector<const char*> inputNames_;
    std::vector<const char*> outputNames_;
    std::vector<int64_t> inputShape_;
    std::vector<int64_t> outputShape_;

    // Threading & task synchronization
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};

    enum class TaskType { None, Initialize, Inference };
    struct Task {
        TaskType type = TaskType::None;
        int sectorId = 0;
    };
    std::vector<Task> taskQueue_;
};

} // namespace sse::vision
