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
    float area() const { return (width() > 0.0f && height() > 0.0f) ? (width() * height()) : 0.0f; }
};

struct Detection {
    BoundingBox box;
    events::DebrisType debrisType = events::DebrisType::Unknown;
    int classId = -1;
    float confidence = 0.0f;
    
    // Physical coordinate relative to gantry bumper (in millimeters)
    float physicalXmm = 0.0f;
    float physicalYmm = 0.0f;
    int targetNozzleId = 0; // 0 to 3 (or configured total nozzles - 1)
};

struct CameraFrame {
    int width = 0;
    int height = 0;
    int channels = 3;
    uint32_t pixelFormat = 0; // V4L2 fourcc code (e.g. V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_RGB24)
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
