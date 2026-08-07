#include "solaredge/Mocks/MockHardwareDriver.h"

#include <chrono>
#include <utility>

#include "solaredge/Core/Events.h"
#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Util/Logger.h"

namespace sse::mocks {

namespace {
constexpr auto kSelfTestLatency = std::chrono::milliseconds(100);
constexpr auto kPollInterval = std::chrono::milliseconds(50);
} // namespace

MockHardwareDriver::MockHardwareDriver(core::EventQueue& queue, config::MockConfig config)
    : queue_(queue), config_(std::move(config)) {
    running_.store(true);
    worker_ = std::thread(&MockHardwareDriver::WorkerLoop, this);
}

MockHardwareDriver::~MockHardwareDriver() {
    shutdown();
}

void MockHardwareDriver::requestSelfTest() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingAction_ = PendingAction::SelfTest;
        pendingSectorId_ = -1;
    }
    cv_.notify_one();
}

void MockHardwareDriver::beginDryClean(int sectorId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingAction_ = PendingAction::DryClean;
        pendingSectorId_ = sectorId;
    }
    cv_.notify_one();
}

void MockHardwareDriver::beginWetClean(int sectorId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingAction_ = PendingAction::WetClean;
        pendingSectorId_ = sectorId;
    }
    cv_.notify_one();
}

void MockHardwareDriver::stopActuators() {
    // Idempotent no-op in the mock -- there is nothing physical to stop.
    // Real hardware safety comes from the FSM's g_estop_flag guard, not
    // from precisely cancelling this mock's in-flight countdown: a stray
    // EvCleanComplete that fires after an E-Stop is simply discarded by
    // whatever state is current by then (see States.h).
    util::Logger::Debug("MockHardwareDriver", "stopActuators()");
}

void MockHardwareDriver::emergencyHalt() {
    // Symbolic in Phase 1 -- the real halt already happened via the atomic
    // flag before this is ever called.
    util::Logger::Debug("MockHardwareDriver", "emergencyHalt() (symbolic)");
}

void MockHardwareDriver::shutdown() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void MockHardwareDriver::triggerSyntheticEstop(std::string reason) {
    core::g_estop_flag.store(true, std::memory_order_release);
    queue_.push(events::EvEmergencyStop{std::move(reason)});
}

void MockHardwareDriver::WorkerLoop() {
    while (running_.load()) {
        PendingAction action;
        int sectorId;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, kPollInterval,
                         [this] { return pendingAction_ != PendingAction::None || !running_.load(); });
            if (!running_.load()) {
                return;
            }
            if (pendingAction_ == PendingAction::None) {
                continue;
            }
            action = pendingAction_;
            sectorId = pendingSectorId_;
            pendingAction_ = PendingAction::None;
        }

        switch (action) {
            case PendingAction::SelfTest: {
                std::this_thread::sleep_for(kSelfTestLatency);
                if (!running_.load()) return;
                if (config_.hardwareFaultInjection) {
                    queue_.push(events::EvBootHardwareFailed{"simulated hardware fault (config injected)"});
                } else {
                    queue_.push(events::EvBootHardwareReady{});
                }
                break;
            }
            case PendingAction::DryClean:
            case PendingAction::WetClean: {
                std::this_thread::sleep_for(std::chrono::milliseconds(config_.cleaningDurationMs));
                if (!running_.load()) return;
                queue_.push(events::EvCleanComplete{sectorId});
                break;
            }
            case PendingAction::None:
                break;
        }
    }
}

} // namespace sse::mocks
