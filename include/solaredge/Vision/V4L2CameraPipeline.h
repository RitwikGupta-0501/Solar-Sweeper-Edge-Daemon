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
    const std::string& devicePath() const { return devicePath_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int fps() const { return fps_; }
    uint32_t activePixelFormat() const { return activePixelFormat_; }

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
    uint32_t activePixelFormat_ = 0;
    std::atomic<bool> streaming_{false};

    std::vector<Buffer> buffers_;
};

} // namespace sse::vision
