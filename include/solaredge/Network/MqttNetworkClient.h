#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Interfaces/INetworkClient.h"
#include "solaredge/Network/TelemetrySpooler.h"

#include <MQTTAsync.h>

namespace sse::network {

class MqttNetworkClient : public iface::INetworkClient {
public:
    MqttNetworkClient(core::EventQueue& queue, const config::Config& config,
                      std::string spoolPath = "cache/telemetry_spool.jsonl");
    ~MqttNetworkClient() override;

    void initialize() override;

    void publishStatus(const std::string& state, int sector) override;
    void publishDetection(int sector, const std::string& type, float conf, float cameraMs, float infMs) override;
    void publishHealth(float cpuTemp, int ramMb) override;
    void publishCycleSummary(float duration, float pumpUptime, int sectors, int bird, int dust, int leaf, float avgInf) override;
    void publishAlert(const std::string& severity, const std::string& code, const std::string& reason) override;

    bool isConnected() const override;
    void shutdown() override;

    // Direct command parser accessible for unit testing.
    void handleInboundCommand(const std::string& payload);

    TelemetrySpooler& spooler() { return spooler_; }

private:
    void doConnect();
    void subscribeCommands();
    void publishJson(const std::string& topicSuffix, const std::string& jsonPayload);
    std::string buildTopic(const std::string& suffix) const;
    long long getTimestampMs() const;

    void startDrainWorker();
    void drainWorkerLoop();

    static void onConnectFailure(void* context, MQTTAsync_failureData* response);
    static void onConnectSuccess(void* context, MQTTAsync_successData* response);
    static void onConnectionLost(void* context, char* cause);
    static int onMessageArrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message);

    core::EventQueue& queue_;
    config::Config config_;

    MQTTAsync client_{nullptr};
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};

    std::string baseTopic_;
    std::string commandsTopic_;

    TelemetrySpooler spooler_;
    std::thread drainThread_;
    std::mutex drainMutex_;
    std::condition_variable drainCv_;
    std::atomic<bool> drainRequested_{false};
};

} // namespace sse::network
