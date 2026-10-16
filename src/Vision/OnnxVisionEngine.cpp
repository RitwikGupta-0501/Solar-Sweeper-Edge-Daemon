#include "solaredge/Vision/OnnxVisionEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <onnxruntime_cxx_api.h>

#include "solaredge/Util/Logger.h"

namespace sse::vision {

OnnxVisionEngine::OnnxVisionEngine(core::EventQueue& queue,
                                   const config::Config& config,
                                   std::unique_ptr<V4L2CameraPipeline> customCamera)
    : queue_(queue),
      config_(config),
      mapper_(config.vision().imageWidth, config.vision().imageHeight,
              config.vision().panelHeightM, config.vision().lookAheadDistanceM,
              config.vision().totalNozzles, config.vision().gantrySpeedMps),
      camera_(std::move(customCamera)) {
    running_.store(true);
    worker_ = std::thread(&OnnxVisionEngine::workerLoop, this);
}

OnnxVisionEngine::~OnnxVisionEngine() {
    shutdown();
}

void OnnxVisionEngine::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    taskQueue_.push_back({TaskType::Initialize, 0});
    cv_.notify_one();
}

void OnnxVisionEngine::requestInference(int sectorId) {
    std::lock_guard<std::mutex> lock(mutex_);
    taskQueue_.push_back({TaskType::Inference, sectorId});
    cv_.notify_one();
}

void OnnxVisionEngine::shutdown() {
    if (!running_.load()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_.store(false);
        cv_.notify_all();
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    if (camera_) {
        camera_->closeDevice();
    }
}

void OnnxVisionEngine::workerLoop() {
    while (running_.load()) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return !running_.load() || !taskQueue_.empty();
            });

            if (!running_.load() && taskQueue_.empty()) {
                break;
            }

            if (!taskQueue_.empty()) {
                task = taskQueue_.front();
                taskQueue_.erase(taskQueue_.begin());
            }
        }

        if (task.type == TaskType::Initialize) {
            doInitialize();
        } else if (task.type == TaskType::Inference) {
            doInference(task.sectorId);
        }
    }
}

void OnnxVisionEngine::doInitialize() {
    util::Logger::Info("OnnxVision", "Initializing ONNX Runtime session and camera pipeline...");

    try {
        ortEnv_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SolarSweeperVision");

        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(config_.vision().intraOpThreads);
        sessionOptions.SetInterOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Verify model file exists
        const std::string& modelPath = config_.vision().modelPath;
        std::ifstream modelFile(modelPath, std::ios::binary);
        if (!modelFile.good()) {
            std::string err = "Model file not found or inaccessible: " + modelPath;
            util::Logger::Error("OnnxVision", err);
            queue_.push(events::EvBootVisionFailed{{}, err});
            return;
        }
        modelFile.close();

        ortSession_ = std::make_unique<Ort::Session>(*ortEnv_, modelPath.c_str(), sessionOptions);
        memoryInfo_ = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

        // Introspect input/output tensor names and shapes
        Ort::AllocatorWithDefaultOptions allocator;
        size_t numInputs = ortSession_->GetInputCount();
        inputNamesAlloc_.clear();
        inputNames_.clear();
        for (size_t i = 0; i < numInputs; ++i) {
            auto namePtr = ortSession_->GetInputNameAllocated(i, allocator);
            inputNamesAlloc_.push_back(namePtr.get());
            inputNames_.push_back(inputNamesAlloc_.back().c_str());
        }

        size_t numOutputs = ortSession_->GetOutputCount();
        outputNamesAlloc_.clear();
        outputNames_.clear();
        for (size_t i = 0; i < numOutputs; ++i) {
            auto outNamePtr = ortSession_->GetOutputNameAllocated(i, allocator);
            outputNamesAlloc_.push_back(outNamePtr.get());
            outputNames_.push_back(outputNamesAlloc_.back().c_str());
        }

        if (numInputs > 0) {
            auto typeInfo = ortSession_->GetInputTypeInfo(0);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            inputShape_ = tensorInfo.GetShape();
        }

        // Initialize camera pipeline if not already injected
        if (!camera_) {
            camera_ = std::make_unique<V4L2CameraPipeline>(
                config_.vision().cameraDevice,
                config_.vision().imageWidth,
                config_.vision().imageHeight,
                config_.vision().framerate
            );
        }

        if (!camera_->isStreaming()) {
            if (!camera_->openDevice() || !camera_->startStreaming()) {
                std::string err = "Failed to open or start V4L2 camera: " + config_.vision().cameraDevice;
                util::Logger::Error("OnnxVision", err);
                queue_.push(events::EvBootVisionFailed{{}, err});
                return;
            }
        }

        initialized_.store(true);
        util::Logger::Info("OnnxVision", "Vision engine initialized successfully");
        queue_.push(events::EvBootVisionReady{});

    } catch (const std::exception& e) {
        std::string err = "ONNX Runtime initialization exception: " + std::string(e.what());
        util::Logger::Error("OnnxVision", err);
        queue_.push(events::EvBootVisionFailed{{}, err});
    }
}

void OnnxVisionEngine::doInference(int sectorId) {
    if (!camera_) {
        util::Logger::Error("OnnxVision", "Inference requested but camera is null");
        queue_.push(events::EvBootVisionFailed{{}, "Camera is uninitialized"});
        return;
    }

    CameraFrame frame;
    auto t0 = std::chrono::steady_clock::now();
    bool grabbed = camera_->grabFrame(frame, 1000);
    auto t1 = std::chrono::steady_clock::now();
    float captureMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    if (!grabbed) {
        util::Logger::Error("OnnxVision", "Camera frame grab timed out for sector " + std::to_string(sectorId));
        queue_.push(events::EvBootVisionFailed{{}, "Camera grabFrame timeout"});
        return;
    }

    processFrameAndDispatch(frame, sectorId, captureMs);
}

void OnnxVisionEngine::processFrameAndDispatch(const CameraFrame& frame, int sectorId, float captureMs) {
    float inferenceMs = 0.0f;
    std::vector<Detection> detections = inferFrame(frame, inferenceMs);

    float bestConfidence = 0.0f;
    events::DebrisType debrisType = arbitrateDebris(detections, bestConfidence);

    if (debrisType != events::DebrisType::None) {
        queue_.push(events::EvDebrisDetected{
            {},
            sectorId,
            debrisType,
            bestConfidence,
            captureMs,
            inferenceMs
        });
        util::Logger::Info("OnnxVision", "Sector " + std::to_string(sectorId) + ": detected " +
                           events::DebrisTypeToString(debrisType) + " (conf=" +
                           std::to_string(bestConfidence) + ", inf=" +
                           std::to_string(inferenceMs) + "ms)");
    } else {
        queue_.push(events::EvSectorClear{{}, sectorId});
        util::Logger::Info("OnnxVision", "Sector " + std::to_string(sectorId) + ": clear (inf=" +
                           std::to_string(inferenceMs) + "ms)");
    }
}

std::vector<Detection> OnnxVisionEngine::inferFrame(const CameraFrame& frame, float& outInferenceMs) {
    if (frame.width <= 0 || frame.height <= 0) {
        outInferenceMs = 0.0f;
        return {};
    }

    int netW = 640;
    int netH = 640;
    if (!inputShape_.empty() && inputShape_.size() == 4) {
        if (inputShape_[2] > 0) netH = static_cast<int>(inputShape_[2]);
        if (inputShape_[3] > 0) netW = static_cast<int>(inputShape_[3]);
    }

    std::vector<float> inputTensor;
    PreprocessingParams prepParams;
    preprocessFrame(frame, netW, netH, inputTensor, prepParams);

    if (!ortSession_ || !memoryInfo_) {
        outInferenceMs = 0.0f;
        return {};
    }

    auto tStart = std::chrono::steady_clock::now();

    std::vector<int64_t> inShape = {1, 3, netH, netW};
    Ort::Value inValue = Ort::Value::CreateTensor<float>(
        *memoryInfo_,
        inputTensor.data(),
        inputTensor.size(),
        inShape.data(),
        inShape.size()
    );

    auto outputs = ortSession_->Run(
        Ort::RunOptions{nullptr},
        inputNames_.data(),
        &inValue,
        1,
        outputNames_.data(),
        outputNames_.size()
    );

    auto tEnd = std::chrono::steady_clock::now();
    outInferenceMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();

    if (outputs.empty()) {
        return {};
    }

    const float* rawOutput = outputs.front().GetTensorData<float>();
    auto shapeInfo = outputs.front().GetTensorTypeAndShapeInfo();
    auto outDims = shapeInfo.GetShape();

    if (outDims.size() < 3) {
        return {};
    }

    int dim1 = static_cast<int>(outDims[1]);
    int dim2 = static_cast<int>(outDims[2]);

    return postprocessOutput(rawOutput, dim1, dim2, prepParams);
}

void OnnxVisionEngine::preprocessFrame(const CameraFrame& frame,
                                      int targetWidth, int targetHeight,
                                      std::vector<float>& outTensor,
                                      PreprocessingParams& outParams) {
    outParams.netWidth = targetWidth;
    outParams.netHeight = targetHeight;

    if (frame.width <= 0 || frame.height <= 0) {
        outParams.scale = 1.0f;
        outParams.padX = 0.0f;
        outParams.padY = 0.0f;
        outTensor.assign(3 * targetWidth * targetHeight, 114.0f / 255.0f);
        return;
    }

    float scale = std::min(static_cast<float>(targetWidth) / frame.width,
                           static_cast<float>(targetHeight) / frame.height);
    float scaledW = std::round(frame.width * scale);
    float scaledH = std::round(frame.height * scale);

    outParams.scale = scale;
    outParams.padX = (targetWidth - scaledW) * 0.5f;
    outParams.padY = (targetHeight - scaledH) * 0.5f;

    const int totalPixels = targetWidth * targetHeight;
    outTensor.assign(3 * totalPixels, 114.0f / 255.0f);

    float* rChannel = outTensor.data();
    float* gChannel = outTensor.data() + totalPixels;
    float* bChannel = outTensor.data() + 2 * totalPixels;

    int padXInt = static_cast<int>(outParams.padX);
    int padYInt = static_cast<int>(outParams.padY);
    int sWInt = static_cast<int>(scaledW);
    int sHInt = static_cast<int>(scaledH);

    bool isRgb = (frame.channels == 3 && frame.data.size() >= static_cast<size_t>(frame.width * frame.height * 3));
    bool isYuyv = (frame.data.size() >= static_cast<size_t>(frame.width * frame.height * 2));

    for (int y = 0; y < sHInt; ++y) {
        int srcY = std::min(static_cast<int>(y / scale), frame.height - 1);
        int dstY = padYInt + y;
        int dstRowOffset = dstY * targetWidth;

        for (int x = 0; x < sWInt; ++x) {
            int srcX = std::min(static_cast<int>(x / scale), frame.width - 1);
            int dstX = padXInt + x;
            int dstIndex = dstRowOffset + dstX;

            float r = 0.447f, g = 0.447f, b = 0.447f;

            if (isRgb) {
                size_t srcIdx = static_cast<size_t>((srcY * frame.width + srcX) * 3);
                r = frame.data[srcIdx + 0] / 255.0f;
                g = frame.data[srcIdx + 1] / 255.0f;
                b = frame.data[srcIdx + 2] / 255.0f;
            } else if (isYuyv) {
                size_t yuyvIdx = static_cast<size_t>((srcY * frame.width + (srcX & ~1)) * 2);
                uint8_t yVal = (srcX & 1) ? frame.data[yuyvIdx + 2] : frame.data[yuyvIdx + 0];
                uint8_t uVal = frame.data[yuyvIdx + 1];
                uint8_t vVal = frame.data[yuyvIdx + 3];

                int c = static_cast<int>(yVal) - 16;
                int d = static_cast<int>(uVal) - 128;
                int e = static_cast<int>(vVal) - 128;

                int rI = (298 * c + 409 * e + 128) >> 8;
                int gI = (298 * c - 100 * d - 208 * e + 128) >> 8;
                int bI = (298 * c + 516 * d + 128) >> 8;

                r = std::max(0, std::min(rI, 255)) / 255.0f;
                g = std::max(0, std::min(gI, 255)) / 255.0f;
                b = std::max(0, std::min(bI, 255)) / 255.0f;
            }

            rChannel[dstIndex] = r;
            gChannel[dstIndex] = g;
            bChannel[dstIndex] = b;
        }
    }
}

float OnnxVisionEngine::computeIoU(const BoundingBox& a, const BoundingBox& b) {
    float x1 = std::max(a.x1, b.x1);
    float y1 = std::max(a.y1, b.y1);
    float x2 = std::min(a.x2, b.x2);
    float y2 = std::min(a.y2, b.y2);

    float interW = std::max(0.0f, x2 - x1);
    float interH = std::max(0.0f, y2 - y1);
    float interArea = interW * interH;
    float unionArea = a.area() + b.area() - interArea;

    if (unionArea <= 0.0f) {
        return 0.0f;
    }
    return interArea / unionArea;
}

void OnnxVisionEngine::applyNMS(std::vector<Detection>& candidates, float iouThreshold) {
    std::sort(candidates.begin(), candidates.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<Detection> kept;
    std::vector<bool> suppressed(candidates.size(), false);

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (suppressed[i]) {
            continue;
        }
        kept.push_back(candidates[i]);

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (suppressed[j]) {
                continue;
            }
            if (candidates[i].classId == candidates[j].classId) {
                float iou = computeIoU(candidates[i].box, candidates[j].box);
                if (iou >= iouThreshold) {
                    suppressed[j] = true;
                }
            }
        }
    }

    candidates = std::move(kept);
}

events::DebrisType OnnxVisionEngine::arbitrateDebris(const std::vector<Detection>& detections,
                                                    float& outBestConfidence) {
    int maxWeight = 0;
    events::DebrisType bestType = events::DebrisType::None;
    float bestConf = 0.0f;

    for (const auto& det : detections) {
        int weight = 0;
        if (det.debrisType == events::DebrisType::BirdDropping) {
            weight = 3;
        } else if (det.debrisType == events::DebrisType::Leaf) {
            weight = 2;
        } else if (det.debrisType == events::DebrisType::Dust) {
            weight = 1;
        }

        if (weight > maxWeight) {
            maxWeight = weight;
            bestType = det.debrisType;
            bestConf = det.confidence;
        } else if (weight == maxWeight && det.confidence > bestConf) {
            bestConf = det.confidence;
        }
    }

    outBestConfidence = bestConf;
    return bestType;
}

std::vector<Detection> OnnxVisionEngine::postprocessOutput(const float* rawOutput,
                                                          int dim1, int dim2,
                                                          const PreprocessingParams& params) {
    if (!rawOutput || dim1 <= 0 || dim2 <= 0) {
        return {};
    }

    bool channelsFirst = (dim1 <= 128);
    int numClasses = channelsFirst ? (dim1 - 4) : (dim2 - 4);
    int totalBoxes = channelsFirst ? dim2 : dim1;

    std::vector<Detection> candidates;

    for (int i = 0; i < totalBoxes; ++i) {
        float xc = 0.0f, yc = 0.0f, w = 0.0f, h = 0.0f;
        int bestClassId = -1;
        float bestScore = 0.0f;

        if (channelsFirst) {
            xc = rawOutput[0 * totalBoxes + i];
            yc = rawOutput[1 * totalBoxes + i];
            w  = rawOutput[2 * totalBoxes + i];
            h  = rawOutput[3 * totalBoxes + i];

            for (int c = 0; c < numClasses; ++c) {
                float s = rawOutput[(4 + c) * totalBoxes + i];
                if (s > bestScore) {
                    bestScore = s;
                    bestClassId = c;
                }
            }
        } else {
            int stride = numClasses + 4;
            xc = rawOutput[i * stride + 0];
            yc = rawOutput[i * stride + 1];
            w  = rawOutput[i * stride + 2];
            h  = rawOutput[i * stride + 3];

            for (int c = 0; c < numClasses; ++c) {
                float s = rawOutput[i * stride + 4 + c];
                if (s > bestScore) {
                    bestScore = s;
                    bestClassId = c;
                }
            }
        }

        if (bestScore >= config_.vision().confidenceThreshold && bestClassId >= 0) {
            BoundingBox netBox{
                xc - w * 0.5f,
                yc - h * 0.5f,
                xc + w * 0.5f,
                yc + h * 0.5f
            };
            Detection det = mapper_.mapToPhysical(netBox, bestClassId, bestScore, params);
            candidates.push_back(det);
        }
    }

    applyNMS(candidates, config_.vision().nmsIouThreshold);
    return candidates;
}

} // namespace sse::vision
