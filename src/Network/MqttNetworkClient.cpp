#include "solaredge/Network/MqttNetworkClient.h"
#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Util/Logger.h"

#include <chrono>
#include <cstring>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace sse::network {

MqttNetworkClient::MqttNetworkClient(core::EventQueue& queue, const config::Config& config,
                                     std::string spoolPath)
    : queue_(queue), config_(config), spooler_(std::move(spoolPath)) {
    baseTopic_ = config_.mqtt().topicPrefix;
    if (!baseTopic_.empty() && baseTopic_.back() == '/') {
        baseTopic_.pop_back();
    }
    commandsTopic_ = buildTopic("/commands");

    std::string brokerUrl = "tcp://" + config_.mqtt().brokerHost + ":" + std::to_string(config_.mqtt().brokerPort);
    std::string clientId = config_.mqtt().clientId;

    int rc = MQTTAsync_create(&client_, brokerUrl.c_str(), clientId.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
        util::Logger::Error("MqttNetworkClient", "Failed to create client, return code: " + std::to_string(rc));
    }

    MQTTAsync_setCallbacks(client_, this, onConnectionLost, onMessageArrived, nullptr);
}

MqttNetworkClient::~MqttNetworkClient() {
    shutdown();
    if (client_) {
        MQTTAsync_destroy(&client_);
        client_ = nullptr;
    }
}

void MqttNetworkClient::initialize() {
    running_.store(true);
    startDrainWorker();
    doConnect();
}

void MqttNetworkClient::startDrainWorker() {
    drainThread_ = std::thread(&MqttNetworkClient::drainWorkerLoop, this);
}

void MqttNetworkClient::drainWorkerLoop() {
    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(drainMutex_);
            drainCv_.wait(lock, [this]() {
                return !running_.load() || drainRequested_.load();
            });
            drainRequested_.store(false);
        }

        while (running_.load() && connected_.load() && spooler_.hasPending()) {
            auto batch = spooler_.peek(25);
            if (batch.empty()) break;

            std::size_t sentCount = 0;
            for (const auto& item : batch) {
                if (!connected_.load() || !running_.load()) break;

                MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
                pubmsg.payload = const_cast<void*>(static_cast<const void*>(item.payload.data()));
                pubmsg.payloadlen = static_cast<int>(item.payload.length());
                pubmsg.qos = 1;
                pubmsg.retained = 0;

                int rc = MQTTAsync_sendMessage(client_, item.topic.c_str(), &pubmsg, nullptr);
                if (rc == MQTTASYNC_SUCCESS) {
                    sentCount++;
                } else {
                    util::Logger::Warn("MqttNetworkClient", "Spool flush interrupted on topic: " + item.topic);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            spooler_.commitDrained(sentCount);
            if (sentCount < batch.size()) {
                break;
            }
        }
    }
}

void MqttNetworkClient::doConnect() {
    if (!running_.load() || !client_) return;

    util::Logger::Info("MqttNetworkClient", "Attempting connection to " + config_.mqtt().brokerHost +
                                                ":" + std::to_string(config_.mqtt().brokerPort));

    MQTTAsync_connectOptions connOpts = MQTTAsync_connectOptions_initializer;
    connOpts.keepAliveInterval = 20;
    connOpts.cleansession = 1;
    connOpts.onSuccess = onConnectSuccess;
    connOpts.onFailure = onConnectFailure;
    connOpts.context = this;

    int rc = MQTTAsync_connect(client_, &connOpts);
    if (rc != MQTTASYNC_SUCCESS) {
        util::Logger::Error("MqttNetworkClient", "Failed to start connect, return code: " + std::to_string(rc));
        connected_.store(false);
        queue_.push(events::EvNetworkLost{});
    }
}

void MqttNetworkClient::subscribeCommands() {
    if (!client_ || !connected_.load()) return;

    MQTTAsync_responseOptions subOpts = MQTTAsync_responseOptions_initializer;
    int rc = MQTTAsync_subscribe(client_, commandsTopic_.c_str(), 1, &subOpts);
    if (rc != MQTTASYNC_SUCCESS) {
        util::Logger::Error("MqttNetworkClient", "Failed to subscribe to commands topic: " + commandsTopic_ +
                                                     ", rc=" + std::to_string(rc));
    } else {
        util::Logger::Info("MqttNetworkClient", "Subscribed to commands topic: " + commandsTopic_);
    }
}

void MqttNetworkClient::onConnectSuccess(void* context, MQTTAsync_successData*) {
    auto* client = static_cast<MqttNetworkClient*>(context);
    util::Logger::Info("MqttNetworkClient", "Connected successfully.");
    client->connected_.store(true);
    client->queue_.push(events::EvNetworkRestored{});
    client->subscribeCommands();

    {
        std::lock_guard<std::mutex> lock(client->drainMutex_);
        client->drainRequested_.store(true);
    }
    client->drainCv_.notify_one();
}

void MqttNetworkClient::onConnectFailure(void* context, MQTTAsync_failureData* response) {
    auto* client = static_cast<MqttNetworkClient*>(context);
    util::Logger::Error("MqttNetworkClient", "Connect failed, rc: " + std::to_string(response ? response->code : -1));
    client->connected_.store(false);
    client->queue_.push(events::EvNetworkLost{});
}

void MqttNetworkClient::onConnectionLost(void* context, char* cause) {
    auto* client = static_cast<MqttNetworkClient*>(context);
    util::Logger::Warn("MqttNetworkClient", std::string("Connection lost: ") + (cause ? cause : "unknown"));
    client->connected_.store(false);
    client->queue_.push(events::EvNetworkLost{});
}

int MqttNetworkClient::onMessageArrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message) {
    auto* client = static_cast<MqttNetworkClient*>(context);
    std::string topic(topicName, topicLen > 0 ? static_cast<std::size_t>(topicLen) : std::strlen(topicName));
    std::string payload(static_cast<char*>(message->payload), static_cast<std::size_t>(message->payloadlen));

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);

    if (client && topic == client->commandsTopic_) {
        client->handleInboundCommand(payload);
    }
    return 1;
}

void MqttNetworkClient::handleInboundCommand(const std::string& payload) {
    try {
        auto j = json::parse(payload);
        std::string cmd = j.value("command", "");
        if (cmd == "START_CYCLE") {
            util::Logger::Info("MqttNetworkClient", "Received cloud command: START_CYCLE");
            queue_.push(events::EvStart{});
        } else if (cmd == "EMERGENCY_HALT") {
            util::Logger::Warn("MqttNetworkClient", "Received cloud command: EMERGENCY_HALT");
            core::g_estop_flag.store(true, std::memory_order_release);
            queue_.push(events::EvEmergencyStop{{}, "cloud_remote_halt"});
        } else if (cmd == "CLEAR_FAULT") {
            util::Logger::Info("MqttNetworkClient", "Received cloud command: CLEAR_FAULT");
            core::g_estop_flag.store(false, std::memory_order_release);
            queue_.push(events::EvEmergencyCleared{});
        } else {
            util::Logger::Warn("MqttNetworkClient", "Unknown cloud command ignored: " + cmd);
        }
    } catch (const std::exception& e) {
        util::Logger::Error("MqttNetworkClient", "Malformed cloud command JSON: " + std::string(e.what()));
    }
}

void MqttNetworkClient::shutdown() {
    if (running_.exchange(false)) {
        {
            std::lock_guard<std::mutex> lock(drainMutex_);
            drainRequested_.store(false);
        }
        drainCv_.notify_all();
        if (drainThread_.joinable()) {
            drainThread_.join();
        }

        if (client_ && connected_.load()) {
            MQTTAsync_disconnectOptions discOpts = MQTTAsync_disconnectOptions_initializer;
            MQTTAsync_disconnect(client_, &discOpts);
        }
        connected_.store(false);
        util::Logger::Info("MqttNetworkClient", "Shutting down MQTT client.");
    }
}

bool MqttNetworkClient::isConnected() const {
    return connected_.load();
}

std::string MqttNetworkClient::buildTopic(const std::string& suffix) const {
    return baseTopic_ + suffix;
}

long long MqttNetworkClient::getTimestampMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void MqttNetworkClient::publishJson(const std::string& topicSuffix, const std::string& jsonPayload) {
    std::string fullTopic = buildTopic(topicSuffix);

    if (!connected_.load() || !client_) {
        util::Logger::Warn("MqttNetworkClient", "Offline: spooling telemetry to disk for " + fullTopic);
        spooler_.spool(fullTopic, jsonPayload);
        return;
    }

    MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
    pubmsg.payload = const_cast<void*>(static_cast<const void*>(jsonPayload.data()));
    pubmsg.payloadlen = static_cast<int>(jsonPayload.length());
    pubmsg.qos = 0;
    pubmsg.retained = 0;

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    int rc = MQTTAsync_sendMessage(client_, fullTopic.c_str(), &pubmsg, &opts);
    if (rc != MQTTASYNC_SUCCESS) {
        util::Logger::Warn("MqttNetworkClient", "Failed to send message on " + fullTopic +
                                                    " (rc=" + std::to_string(rc) + "), spooling to disk");
        spooler_.spool(fullTopic, jsonPayload);
    } else {
        util::Logger::Debug("MqttNetworkClient", "Published to " + fullTopic);
    }
}

void MqttNetworkClient::publishStatus(const std::string& state, int sector) {
    json j = {
        {"timestamp_ms", getTimestampMs()},
        {"fsm_state", state},
        {"current_sector", sector},
        {"network_status", isConnected() ? "online" : "offline"}
    };
    publishJson("/telemetry/status", j.dump());
}

void MqttNetworkClient::publishDetection(int sector, const std::string& type, float conf, float cameraMs, float infMs) {
    json j = {
        {"timestamp_ms", getTimestampMs()},
        {"sector", sector},
        {"debris_type", type},
        {"confidence_score", conf},
        {"profiling", {
            {"camera_capture_ms", cameraMs},
            {"inference_time_ms", infMs}
        }}
    };
    publishJson("/telemetry/detections", j.dump());
}

void MqttNetworkClient::publishHealth(float cpuTemp, int ramMb) {
    json j = {
        {"timestamp_ms", getTimestampMs()},
        {"cpu_temp_c", cpuTemp},
        {"ram_usage_mb", ramMb},
        {"uptime_s", 0}
    };
    publishJson("/telemetry/health", j.dump());
}

void MqttNetworkClient::publishCycleSummary(float duration, float pumpUptime, int sectors, int bird, int dust, int leaf, float avgInf) {
    json j = {
        {"timestamp_ms", getTimestampMs()},
        {"total_duration_s", duration},
        {"water_pump_uptime_s", pumpUptime},
        {"total_sectors_cleaned", sectors},
        {"detections", {
            {"bird_dropping", bird},
            {"dust", dust},
            {"leaf", leaf}
        }},
        {"average_inference_time_ms", avgInf}
    };
    publishJson("/telemetry/cycle_summary", j.dump());
}

void MqttNetworkClient::publishAlert(const std::string& severity, const std::string& code, const std::string& reason) {
    json j = {
        {"timestamp_ms", getTimestampMs()},
        {"severity", severity},
        {"fault_code", code},
        {"reason", reason}
    };
    publishJson("/telemetry/alerts", j.dump());
}

} // namespace sse::network
