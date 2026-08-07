#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Interfaces/IHardwareDriver.h"

namespace sse::mocks {

// Stands in for Phase 2's real SerialHardwareDriver. Owns one background
// worker thread for its entire lifetime (constructor to shutdown()) that
// simulates self-test and cleaning-duration latency by sleeping, then
// pushes the corresponding event -- this exercises genuine concurrent
// producer/consumer behavior against the FSM, not just single-threaded
// fake-event injection.
class MockHardwareDriver : public iface::IHardwareDriver {
public:
    MockHardwareDriver(core::EventQueue& queue, config::MockConfig config);
    ~MockHardwareDriver() override;

    void requestSelfTest() override;
    void beginDryClean(int sectorId) override;
    void beginWetClean(int sectorId) override;
    void stopActuators() override;
    void emergencyHalt() override;
    void shutdown() override;

    // Deterministic test/console hook: flips g_estop_flag synchronously
    // and only then pushes EvEmergencyStop, mirroring the exact two-step
    // contract a real interrupt handler must follow.
    void triggerSyntheticEstop(std::string reason);

private:
    enum class PendingAction { None, SelfTest, DryClean, WetClean };

    void WorkerLoop();

    core::EventQueue& queue_;
    config::MockConfig config_;

    std::thread worker_;
    std::atomic<bool> running_{false};

    std::mutex mutex_;
    std::condition_variable cv_;
    PendingAction pendingAction_ = PendingAction::None;
    int pendingSectorId_ = -1;
};

} // namespace sse::mocks
