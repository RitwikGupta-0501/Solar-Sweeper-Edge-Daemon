#include "solaredge/Config/Config.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "solaredge/Util/JsonHelper.h"

namespace sse::config {

namespace {

using nlohmann::json;

const json& RequireField(const json& j, const char* sectionPath, const char* key) {
    if (!j.contains(key)) {
        throw ConfigError(std::string(sectionPath) + "." + key + ": missing required field");
    }
    return j.at(key);
}

int RequireInt(const json& j, const char* sectionPath, const char* key) {
    const json& value = RequireField(j, sectionPath, key);
    if (!value.is_number_integer()) {
        throw ConfigError(std::string(sectionPath) + "." + key + ": expected integer");
    }
    return value.get<int>();
}

bool RequireBool(const json& j, const char* sectionPath, const char* key) {
    const json& value = RequireField(j, sectionPath, key);
    if (!value.is_boolean()) {
        throw ConfigError(std::string(sectionPath) + "." + key + ": expected boolean");
    }
    return value.get<bool>();
}

std::string RequireString(const json& j, const char* sectionPath, const char* key) {
    const json& value = RequireField(j, sectionPath, key);
    if (!value.is_string()) {
        throw ConfigError(std::string(sectionPath) + "." + key + ": expected string");
    }
    return value.get<std::string>();
}

const json& RequireSection(const json& root, const char* key) {
    if (!root.contains(key) || !root.at(key).is_object()) {
        throw ConfigError(std::string(key) + ": missing or not an object");
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
    cfg.hardwareDriver = section.value("hardware_driver", cfg.enabled);
    cfg.networkClient = section.value("network_client", cfg.enabled);
    cfg.visionEngine = section.value("vision_engine", cfg.enabled);

    // If global mock is disabled and no sub-driver overrides are specified,
    // verify we have driver support. Vision engine is not implemented yet in Phase 2.
    if (!cfg.enabled && !section.contains("hardware_driver") && !section.contains("network_client") && !section.contains("vision_engine")) {
        cfg.hardwareDriver = false;
        cfg.networkClient = false;
        cfg.visionEngine = false;
    }

    cfg.hardwareFaultInjection = section.value("hardware_fault_injection", false);
    cfg.networkStartOffline = section.value("network_start_offline", false);

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

MqttConfig ParseMqttConfig(const json& root) {
    const json& section = RequireSection(root, "mqtt");
    MqttConfig cfg;
    cfg.brokerHost = section.value("broker_host", cfg.brokerHost);
    cfg.brokerPort = section.value("broker_port", cfg.brokerPort);
    cfg.clientId = section.value("client_id", cfg.clientId);
    cfg.topicPrefix = section.value("topic_prefix", cfg.topicPrefix);
    cfg.keepaliveSec = section.value("keepalive_sec", cfg.keepaliveSec);
    cfg.cachePath = section.value("cache_path", cfg.cachePath);

    if (section.contains("routes") && section.at("routes").is_array()) {
        for (const auto& r : section.at("routes")) {
            if (r.is_object() && r.contains("topic") && r.contains("action")) {
                cfg.routes.push_back({r.at("topic").get<std::string>(), r.at("action").get<std::string>()});
            }
        }
    }

    if (section.contains("telemetry_topics") && section.at("telemetry_topics").is_object()) {
        for (auto it = section.at("telemetry_topics").begin(); it != section.at("telemetry_topics").end(); ++it) {
            if (it.value().is_string()) {
                cfg.telemetryTopics[it.key()] = it.value().get<std::string>();
            }
        }
    }

    return cfg;
}

SerialConfig ParseSerialConfig(const json& root) {
    const json& section = RequireSection(root, "serial");
    SerialConfig cfg;
    cfg.devicePath = section.value("device_path", cfg.devicePath);
    cfg.baudRate = section.value("baud_rate", cfg.baudRate);
    cfg.timeoutMs = section.value("timeout_ms", cfg.timeoutMs);
    cfg.protocolMode = section.value("protocol_mode", cfg.protocolMode);

    if (section.contains("commands") && section.at("commands").is_object()) {
        const auto& cmds = section.at("commands");
        if (cmds.contains("dry_clean")) {
            cfg.commands.dryClean = util::JsonHelper::ParseByte(cmds.at("dry_clean"), cfg.commands.dryClean);
        }
        if (cmds.contains("wet_clean")) {
            cfg.commands.wetClean = util::JsonHelper::ParseByte(cmds.at("wet_clean"), cfg.commands.wetClean);
        }
        if (cmds.contains("emergency_halt")) {
            cfg.commands.emergencyHalt = util::JsonHelper::ParseByte(cmds.at("emergency_halt"), cfg.commands.emergencyHalt);
        }
        if (cmds.contains("ping")) {
            cfg.commands.ping = util::JsonHelper::ParseByte(cmds.at("ping"), cfg.commands.ping);
        }
    }

    // Default built-in interrupt triggers if none specified in config
    cfg.interrupts[0xE1] = {"estop", "limit_switch_hit"};
    cfg.interrupts[0xE2] = {"estop", "motor_stall"};
    cfg.interrupts[0xD0] = {"clean_complete", ""};
    cfg.interrupts[0xA0] = {"ack_ping", ""};
    cfg.interrupts[0xA1] = {"ack", ""};
    cfg.interrupts[0xA2] = {"ack", ""};
    cfg.interrupts[0xAF] = {"ack", ""};

    if (section.contains("interrupts") && section.at("interrupts").is_object()) {
        for (auto it = section.at("interrupts").begin(); it != section.at("interrupts").end(); ++it) {
            uint8_t byteCode = util::JsonHelper::ParseByte(it.key(), 0);
            if (it.value().is_object()) {
                std::string evt = it.value().value("event", "");
                std::string reason = it.value().value("reason", "");
                cfg.interrupts[byteCode] = {evt, reason};
            }
        }
    }

    return cfg;
}

VisionConfig ParseVisionConfig(const json& root) {
    const json& section = RequireSection(root, "vision");
    VisionConfig cfg;
    cfg.modelPath = section.value("model_path", cfg.modelPath);
    cfg.cameraIndex = section.value("camera_index", cfg.cameraIndex);
    cfg.confidenceThreshold = section.value("confidence_threshold", cfg.confidenceThreshold);

    // Default classes
    cfg.classMap[0] = {"dust", "dry_clean"};
    cfg.classMap[1] = {"leaf", "dry_clean"};
    cfg.classMap[2] = {"bird_dropping", "wet_clean"};

    if (section.contains("class_map") && section.at("class_map").is_object()) {
        for (auto it = section.at("class_map").begin(); it != section.at("class_map").end(); ++it) {
            try {
                int classId = std::stoi(it.key());
                if (it.value().is_object()) {
                    std::string name = it.value().value("name", "");
                    std::string action = it.value().value("action", "dry_clean");
                    cfg.classMap[classId] = {name, action};
                }
            } catch (...) {}
        }
    }

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
