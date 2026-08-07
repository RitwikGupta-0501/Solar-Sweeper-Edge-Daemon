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
    void publishTelemetry(const std::string& topic, const std::string& jsonPayload) override;
    bool isConnected() const override;
    void shutdown() override;

    // Deterministic test/console hooks, pushed directly (no need to route
    // through the worker thread -- these are instantaneous by nature).
    void simulateNetworkLoss();
    void simulateNetworkRestore();

    int callCount() const;

private:
    void WorkerLoop();

    core::EventQueue& queue_;
    config::MockConfig config_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<int> publishCallCount_{0};

    std::mutex mutex_;
    std::condition_variable cv_;
    bool initializeRequested_ = false;
};

} // namespace sse::mocks
