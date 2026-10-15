#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Interfaces/INetworkClient.h"

namespace sse::mocks {

// Stands in for Phase 3's real PahoNetworkClient. publishTelemetry() only
// logs and counts calls -- no real broker exists yet.
class MockNetworkClient : public iface::INetworkClient {
public:
    MockNetworkClient(core::EventQueue& queue, config::MockConfig config);
    ~MockNetworkClient() override;

    void initialize() override;
    void publishStatus(const std::string& state, int sector) override;
    void publishDetection(int sector, const std::string& type, float conf, float cameraMs, float infMs) override;
    void publishHealth(float cpuTemp, int ramMb) override;
    void publishCycleSummary(float duration, float pumpUptime, int sectors, int bird, int dust, int leaf, float avgInf) override;
    void publishAlert(const std::string& severity, const std::string& code, const std::string& reason) override;
    bool isConnected() const override;
    void shutdown() override;

    // Deterministic test/console hooks, pushed directly (no need to route
    // through the worker thread -- these are instantaneous by nature).
    void simulateNetworkLoss();
    void simulateNetworkRestore();

    int callCount() const;
    int statusCallCount() const;
    int detectionCallCount() const;
    int alertCallCount() const;
    int cycleSummaryCallCount() const;

    std::string lastStatusState() const;
    int lastStatusSector() const;
    std::string lastAlertSeverity() const;
    std::string lastAlertCode() const;
    std::string lastAlertReason() const;
    std::string lastDetectionType() const;
    float lastCycleDuration() const;
    int lastCycleSectors() const;

private:
    void WorkerLoop();

    core::EventQueue& queue_;
    config::MockConfig config_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<int> publishCallCount_{0};
    std::atomic<int> statusCallCount_{0};
    std::atomic<int> detectionCallCount_{0};
    std::atomic<int> alertCallCount_{0};
    std::atomic<int> cycleSummaryCallCount_{0};

    mutable std::mutex inspectMutex_;
    std::string lastStatusState_;
    int lastStatusSector_ = -1;
    std::string lastAlertSeverity_;
    std::string lastAlertCode_;
    std::string lastAlertReason_;
    std::string lastDetectionType_;
    float lastCycleDuration_ = 0.0f;
    int lastCycleSectors_ = 0;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool initializeRequested_ = false;
};

} // namespace sse::mocks
