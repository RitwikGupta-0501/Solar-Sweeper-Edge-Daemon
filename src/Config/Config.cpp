#include "solaredge/Config/Config.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace sse::config {

namespace {

using nlohmann::json;

// Fetches j[key], throwing a ConfigError that names the exact field path
// (e.g. "system.log_level") instead of letting nlohmann::json's generic
// "key not found" propagate — the field path is what makes the error
// message actually actionable for whoever misconfigured the file.
const json& RequireField(const json& j, const std::string& sectionPath, const std::string& key) {
    if (!j.contains(key)) {
        throw ConfigError(sectionPath + "." + key + ": missing required field");
    }
    return j.at(key);
}

int RequireInt(const json& j, const std::string& sectionPath, const std::string& key) {
    const json& value = RequireField(j, sectionPath, key);
    if (!value.is_number_integer()) {
        throw ConfigError(sectionPath + "." + key + ": expected integer");
    }
    return value.get<int>();
}

bool RequireBool(const json& j, const std::string& sectionPath, const std::string& key) {
    const json& value = RequireField(j, sectionPath, key);
    if (!value.is_boolean()) {
        throw ConfigError(sectionPath + "." + key + ": expected boolean");
    }
    return value.get<bool>();
}

std::string RequireString(const json& j, const std::string& sectionPath, const std::string& key) {
    const json& value = RequireField(j, sectionPath, key);
    if (!value.is_string()) {
        throw ConfigError(sectionPath + "." + key + ": expected string");
    }
    return value.get<std::string>();
}

const json& RequireSection(const json& root, const std::string& key) {
    if (!root.contains(key) || !root.at(key).is_object()) {
        throw ConfigError(key + ": missing or not an object");
    }
    return root.at(key);
}

SystemConfig ParseSystemConfig(const json& root) {
    const json& section = RequireSection(root, "system");
    SystemConfig cfg;

    const std::string levelText = RequireString(section, "system", "log_level");
    try {
        cfg.logLevel = util::ParseLogLevel(levelText);
    } catch (const std::invalid_argument&) {
        throw ConfigError("system.log_level: expected one of trace|debug|info|warn|error, got '" + levelText + "'");
    }

    const int totalSectors = RequireInt(section, "system", "total_sectors");
    if (totalSectors < 1) {
        throw ConfigError("system.total_sectors: must be >= 1");
    }
    cfg.totalSectors = totalSectors;

    const int queueCapacityHint = RequireInt(section, "system", "queue_capacity_hint");
    if (queueCapacityHint < 1) {
        throw ConfigError("system.queue_capacity_hint: must be > 0");
    }
    cfg.queueCapacityHint = static_cast<std::size_t>(queueCapacityHint);

    const int bootTimeoutMs = RequireInt(section, "system", "boot_timeout_ms");
    if (bootTimeoutMs < 1) {
        throw ConfigError("system.boot_timeout_ms: must be > 0");
    }
    cfg.bootTimeoutMs = bootTimeoutMs;

    const int bootMaxRetries = RequireInt(section, "system", "boot_max_retries");
    if (bootMaxRetries < 0) {
        throw ConfigError("system.boot_max_retries: must be >= 0");
    }
    cfg.bootMaxRetries = bootMaxRetries;

    return cfg;
}

MockConfig ParseMockConfig(const json& root) {
    const json& section = RequireSection(root, "mock");
    MockConfig cfg;

    cfg.enabled = RequireBool(section, "mock", "enabled");


    cfg.hardwareFaultInjection = RequireBool(section, "mock", "hardware_fault_injection");
    cfg.networkStartOffline = RequireBool(section, "mock", "network_start_offline");

    const int visionCycleMs = RequireInt(section, "mock", "vision_cycle_ms");
    if (visionCycleMs < 1) {
        throw ConfigError("mock.vision_cycle_ms: must be > 0");
    }
    cfg.visionCycleMs = visionCycleMs;

    const int cleaningDurationMs = RequireInt(section, "mock", "cleaning_duration_ms");
    if (cleaningDurationMs < 1) {
        throw ConfigError("mock.cleaning_duration_ms: must be > 0");
    }
    cfg.cleaningDurationMs = cleaningDurationMs;

    return cfg;
}

// mqtt/serial/vision: only require the section to exist as an object.
// Sub-fields are read with defaults — nothing in Phase 1 consumes them, so
// strict validation here would protect nothing and would just have to be
// redone once the real client that owns each section is written.
MqttConfig ParseMqttConfig(const json& root) {
    const json& section = RequireSection(root, "mqtt");
    MqttConfig cfg;
    cfg.brokerHost = section.value("broker_host", cfg.brokerHost);
    cfg.brokerPort = section.value("broker_port", cfg.brokerPort);
    cfg.clientId = section.value("client_id", cfg.clientId);
    cfg.topicPrefix = section.value("topic_prefix", cfg.topicPrefix);
    return cfg;
}

SerialConfig ParseSerialConfig(const json& root) {
    const json& section = RequireSection(root, "serial");
    SerialConfig cfg;
    cfg.devicePath = section.value("device_path", cfg.devicePath);
    cfg.baudRate = section.value("baud_rate", cfg.baudRate);
    return cfg;
}

VisionConfig ParseVisionConfig(const json& root) {
    const json& section = RequireSection(root, "vision");
    VisionConfig cfg;
    cfg.modelPath = section.value("model_path", cfg.modelPath);
    cfg.cameraIndex = section.value("camera_index", cfg.cameraIndex);
    return cfg;
}

} // namespace

Config Config::LoadFromFile(const std::string& path) {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        throw ConfigError("Config file not found: " + path);
    }

    json root;
    try {
        // ignore_comments=true lets the JSON config carry // and /* */
        // comments despite being strict JSON otherwise.
        root = json::parse(stream, /*cb=*/nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);
    } catch (const json::parse_error& e) {
        throw ConfigError(std::string("Failed to parse config file '") + path + "': " + e.what());
    }

    Config cfg;
    cfg.system_ = ParseSystemConfig(root);
    cfg.mock_ = ParseMockConfig(root);
    cfg.mqtt_ = ParseMqttConfig(root);
    cfg.serial_ = ParseSerialConfig(root);
    cfg.vision_ = ParseVisionConfig(root);
    return cfg;
}

} // namespace sse::config
