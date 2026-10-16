#include "solaredge/Vision/V4L2CameraPipeline.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include "solaredge/Util/Logger.h"

namespace sse::vision {

V4L2CameraPipeline::V4L2CameraPipeline(std::string devicePath, int width, int height, int fps)
    : devicePath_(std::move(devicePath)),
      width_(width),
      height_(height),
      fps_(fps) {}

V4L2CameraPipeline::~V4L2CameraPipeline() {
    closeDevice();
}

bool V4L2CameraPipeline::openDevice() {
    if (isOpened()) {
        return true;
    }

    util::Logger::Info("V4L2", "Opening camera device: " + devicePath_);
    deviceFd_ = ::open(devicePath_.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (deviceFd_ < 0) {
        util::Logger::Error("V4L2", "Failed to open " + devicePath_ + ": " + std::strerror(errno));
        return false;
    }

    struct v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));
    if (::ioctl(deviceFd_, VIDIOC_QUERYCAP, &cap) < 0) {
        util::Logger::Error("V4L2", "VIDIOC_QUERYCAP failed on " + devicePath_ + ": " + std::strerror(errno));
        closeDevice();
        return false;
    }

    uint32_t capabilities = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        util::Logger::Error("V4L2", devicePath_ + " is not a video capture device");
        closeDevice();
        return false;
    }
    if (!(capabilities & V4L2_CAP_STREAMING)) {
        util::Logger::Error("V4L2", devicePath_ + " does not support streaming I/O");
        closeDevice();
        return false;
    }

    // Try format negotiation: first RGB24, then YUYV, then MJPEG
    const uint32_t preferredFormats[] = {
        V4L2_PIX_FMT_RGB24,
        V4L2_PIX_FMT_YUYV,
        V4L2_PIX_FMT_MJPEG
    };

    bool formatSet = false;
    struct v4l2_format fmt;
    for (uint32_t pixFmt : preferredFormats) {
        std::memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = width_;
        fmt.fmt.pix.height = height_;
        fmt.fmt.pix.pixelformat = pixFmt;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;

        if (::ioctl(deviceFd_, VIDIOC_S_FMT, &fmt) == 0) {
            activePixelFormat_ = fmt.fmt.pix.pixelformat;
            width_ = fmt.fmt.pix.width;
            height_ = fmt.fmt.pix.height;
            formatSet = true;
            util::Logger::Info("V4L2", "Negotiated format: " + std::to_string(width_) + "x" +
                               std::to_string(height_) + " fourcc=" + std::to_string(activePixelFormat_));
            break;
        }
    }

    if (!formatSet) {
        util::Logger::Error("V4L2", "Failed to negotiate a supported video format on " + devicePath_);
        closeDevice();
        return false;
    }

    // Attempt to set frame rate (best effort)
    struct v4l2_streamparm streamParm;
    std::memset(&streamParm, 0, sizeof(streamParm));
    streamParm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamParm.parm.capture.timeperframe.numerator = 1;
    streamParm.parm.capture.timeperframe.denominator = fps_;
    ::ioctl(deviceFd_, VIDIOC_S_PARM, &streamParm);

    return true;
}

bool V4L2CameraPipeline::startStreaming() {
    if (!isOpened()) {
        if (!openDevice()) {
            return false;
        }
    }

    if (streaming_.load()) {
        return true;
    }

    struct v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (::ioctl(deviceFd_, VIDIOC_REQBUFS, &req) < 0) {
        util::Logger::Error("V4L2", "VIDIOC_REQBUFS failed: " + std::string(std::strerror(errno)));
        return false;
    }

    if (req.count < 2) {
        util::Logger::Error("V4L2", "Insufficient buffer memory on " + devicePath_);
        return false;
    }

    buffers_.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (::ioctl(deviceFd_, VIDIOC_QUERYBUF, &buf) < 0) {
            util::Logger::Error("V4L2", "VIDIOC_QUERYBUF failed for buffer " + std::to_string(i));
            stopStreaming();
            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = ::mmap(nullptr, buf.length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED,
                                   deviceFd_, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED) {
            util::Logger::Error("V4L2", "mmap failed for buffer " + std::to_string(i));
            buffers_[i].start = nullptr;
            stopStreaming();
            return false;
        }

        if (::ioctl(deviceFd_, VIDIOC_QBUF, &buf) < 0) {
            util::Logger::Error("V4L2", "VIDIOC_QBUF initial enqueue failed for buffer " + std::to_string(i));
            stopStreaming();
            return false;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (::ioctl(deviceFd_, VIDIOC_STREAMON, &type) < 0) {
        util::Logger::Error("V4L2", "VIDIOC_STREAMON failed: " + std::string(std::strerror(errno)));
        stopStreaming();
        return false;
    }

    streaming_.store(true);
    util::Logger::Info("V4L2", "Started streaming on " + devicePath_);
    return true;
}

bool V4L2CameraPipeline::grabFrame(CameraFrame& outFrame, int timeoutMs) {
    if (!streaming_.load() || deviceFd_ < 0) {
        return false;
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(deviceFd_, &fds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ret = ::select(deviceFd_ + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) {
        if (ret == 0) {
            util::Logger::Warn("V4L2", "Frame grab timeout (" + std::to_string(timeoutMs) + " ms) on " + devicePath_);
        } else {
            util::Logger::Error("V4L2", "select() error on " + devicePath_ + ": " + std::strerror(errno));
        }
        return false;
    }

    struct v4l2_buffer buf;
    std::memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (::ioctl(deviceFd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) {
            return false;
        }
        util::Logger::Error("V4L2", "VIDIOC_DQBUF failed: " + std::string(std::strerror(errno)));
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    uint64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    outFrame.width = width_;
    outFrame.height = height_;
    outFrame.channels = (activePixelFormat_ == V4L2_PIX_FMT_RGB24) ? 3 : 2;
    outFrame.pixelFormat = activePixelFormat_;
    outFrame.timestampUs = nowUs;

    const auto* srcPtr = static_cast<const uint8_t*>(buffers_[buf.index].start);
    outFrame.data.assign(srcPtr, srcPtr + buf.bytesused);

    // Requeue the buffer back to kernel driver
    if (::ioctl(deviceFd_, VIDIOC_QBUF, &buf) < 0) {
        util::Logger::Error("V4L2", "VIDIOC_QBUF requeue failed: " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

void V4L2CameraPipeline::stopStreaming() {
    if (!streaming_.load() && buffers_.empty()) {
        return;
    }

    if (deviceFd_ >= 0 && streaming_.load()) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ::ioctl(deviceFd_, VIDIOC_STREAMOFF, &type);
    }
    streaming_.store(false);

    for (auto& b : buffers_) {
        if (b.start && b.start != MAP_FAILED) {
            ::munmap(b.start, b.length);
            b.start = nullptr;
        }
    }
    buffers_.clear();
    util::Logger::Info("V4L2", "Stopped streaming on " + devicePath_);
}

void V4L2CameraPipeline::closeDevice() {
    stopStreaming();
    if (deviceFd_ >= 0) {
        ::close(deviceFd_);
        deviceFd_ = -1;
        util::Logger::Info("V4L2", "Closed device " + devicePath_);
    }
}

} // namespace sse::vision
