#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Interfaces/IVisionEngine.h"

namespace sse::mocks {

// Stands in for Phase 4's real OnnxVisionEngine. Cycles through a fixed,
// deterministic list of canned InferenceResults so tests can assert exactly
// what the Nth requestInference() call produces.
class MockVisionEngine : public iface::IVisionEngine {
public:
    MockVisionEngine(core::EventQueue& queue, config::MockConfig config);
    ~MockVisionEngine() override;

    void initialize() override;
    void requestInference(int sectorId) override;
    void shutdown() override;

private:
    enum class PendingAction { None, Initialize, Inference };

    void WorkerLoop();

    core::EventQueue& queue_;
    config::MockConfig config_;

    std::thread worker_;
    std::atomic<bool> running_{false};

    std::mutex mutex_;
    std::condition_variable cv_;
    PendingAction pendingAction_ = PendingAction::None;
    int pendingSectorId_ = -1;
    std::size_t nextResultIndex_ = 0;
};

} // namespace sse::mocks
