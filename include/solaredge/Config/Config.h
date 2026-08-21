#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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
    bool hardwareDriver = true;
    bool networkClient = true;
    bool visionEngine = true;
    bool hardwareFaultInjection = false;
    bool networkStartOffline = false;
    int visionCycleMs = 800;
    int cleaningDurationMs = 500;
};

struct SerialCommands {
    uint8_t dryClean = 0x01;
    uint8_t wetClean = 0x02;
    uint8_t emergencyHalt = 0xFF;
    uint8_t ping = 0x00;
};

struct SerialInterruptDef {
    std::string eventType; // "estop", "clean_complete", "ack", "warning"
    std::string reason;
};

struct SerialConfig {
    std::string devicePath = "/dev/ttyACM0";
    int baudRate = 115200;
    int timeoutMs = 500;
    std::string protocolMode = "single_byte"; // single_byte, framed_packet, ascii_line
    SerialCommands commands;
    std::unordered_map<uint8_t, SerialInterruptDef> interrupts;
};

struct MqttRoute {
    std::string topic;
    std::string action; // "START", "ABORT", "RESET"
};

struct MqttConfig {
    std::string brokerHost = "localhost";
    int brokerPort = 1883;
    std::string clientId = "edge-daemon-01";
    std::string topicPrefix = "solarsweeper/robot01";
    int keepaliveSec = 10;
    std::string cachePath = "/var/lib/edge-daemon/cache.json";
    std::vector<MqttRoute> routes;
    std::unordered_map<std::string, std::string> telemetryTopics;
};

struct VisionClassDef {
    std::string name;
    std::string action; // "dry_clean", "wet_clean", "none"
};

struct VisionConfig {
    std::string modelPath = "models/debris_detector.onnx";
    int cameraIndex = 0;
    float confidenceThreshold = 0.65f;
    std::unordered_map<int, VisionClassDef> classMap;
};

// Loads and validates daemon.config.json.
// All hardware opcodes, MQTT routes, and vision class mappings are loaded into
// structured tables to provide malleability without code changes.
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
