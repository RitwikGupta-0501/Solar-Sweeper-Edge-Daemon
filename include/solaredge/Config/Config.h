#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#include "solaredge/Util/Logger.h"

namespace sse::config {

class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& message) : std::runtime_error(message) {}
};

struct SystemConfig {
    util::LogLevel logLevel = util::LogLevel::Info;
    int totalSectors = 1;
    std::size_t queueCapacityHint = 1024;
    int bootTimeoutMs = 5000;
    int bootMaxRetries = 3;
};

struct MockConfig {
    bool enabled = true;
    bool hardwareFaultInjection = false;
    bool networkStartOffline = false;
    int visionCycleMs = 800;
    int cleaningDurationMs = 500;
};

// Phase 1 only parses and stores these; nothing reads them yet, so deep
// validation is deferred to the phase that implements the real client
// (Phase 3 for MQTT, Phase 2 for serial, Phase 4 for vision).
struct MqttConfig {
    std::string brokerHost = "localhost";
    int brokerPort = 1883;
    std::string clientId = "edge-daemon-01";
    std::string topicPrefix = "solarsweeper/robot01";
};

struct SerialConfig {
    std::string devicePath = "/dev/ttyACM0";
    int baudRate = 115200;
};

struct VisionConfig {
    std::string modelPath = "models/debris_detector.onnx";
    int cameraIndex = 0;
};

// Loads and validates daemon.config.json. system.* and mock.* fields are
// strictly validated (fail fast, name the exact field path) because Phase 1
// code actually depends on them being correct; mqtt/serial/vision are only
// checked for existing-as-object, since no real client consumes them yet.
class Config {
public:
    static Config LoadFromFile(const std::string& path);

    const SystemConfig& system() const { return system_; }
    const MockConfig& mock() const { return mock_; }
    const MqttConfig& mqtt() const { return mqtt_; }
    const SerialConfig& serial() const { return serial_; }
    const VisionConfig& vision() const { return vision_; }

private:
    SystemConfig system_;
    MockConfig mock_;
    MqttConfig mqtt_;
    SerialConfig serial_;
    VisionConfig vision_;
};

} // namespace sse::config
