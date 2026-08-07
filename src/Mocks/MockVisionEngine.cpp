#include "solaredge/Mocks/MockVisionEngine.h"

#include <chrono>
#include <utility>
#include <vector>

#include "solaredge/Core/Events.h"
#include "solaredge/Util/Logger.h"

namespace sse::mocks {

namespace {
constexpr auto kInitLatency = std::chrono::milliseconds(100);
constexpr auto kPollInterval = std::chrono::milliseconds(50);

const std::vector<iface::InferenceResult>& CannedResults() {
    static const std::vector<iface::InferenceResult> results = {
        {events::DebrisType::Dust, 0.82f, false},
        {events::DebrisType::BirdDropping, 0.91f, false},
        {events::DebrisType::None, 0.0f, true},
        {events::DebrisType::Leaf, 0.77f, false},
    };
    return results;
}
} // namespace

MockVisionEngine::MockVisionEngine(core::EventQueue& queue, config::MockConfig config)
    : queue_(queue), config_(std::move(config)) {
    running_.store(true);
    worker_ = std::thread(&MockVisionEngine::WorkerLoop, this);
}

MockVisionEngine::~MockVisionEngine() {
    shutdown();
}

void MockVisionEngine::initialize() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingAction_ = PendingAction::Initialize;
    }
    cv_.notify_one();
}

void MockVisionEngine::requestInference(int sectorId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingAction_ = PendingAction::Inference;
        pendingSectorId_ = sectorId;
    }
    cv_.notify_one();
}

void MockVisionEngine::shutdown() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void MockVisionEngine::WorkerLoop() {
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
            case PendingAction::Initialize: {
                std::this_thread::sleep_for(kInitLatency);
                if (!running_.load()) return;
                queue_.push(events::EvBootVisionReady{});
                break;
            }
            case PendingAction::Inference: {
                std::this_thread::sleep_for(std::chrono::milliseconds(config_.visionCycleMs));
                if (!running_.load()) return;

                const auto& canned = CannedResults();
                const iface::InferenceResult result = canned[nextResultIndex_ % canned.size()];
                nextResultIndex_++;

                if (result.sectorClear) {
                    queue_.push(events::EvSectorClear{{}, sectorId});
                } else {
                    queue_.push(events::EvDebrisDetected{{}, sectorId, result.debrisType, result.confidence});
                }
                break;
            }
            case PendingAction::None:
                break;
        }
    }
}

} // namespace sse::mocks
