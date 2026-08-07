#pragma once

#include <chrono>
#include <cstddef>
#include <utility>
#include <variant>

#include <blockingconcurrentqueue.h>

#include "solaredge/Core/Events.h"

namespace sse::core {

// Every event type the FSM can ever receive. Heterogeneous events flow
// through one moodycamel queue by being wrapped in this variant.
using EventVariant = std::variant<
    events::EvStart,
    events::EvDebrisDetected,
    events::EvSectorClear,
    events::EvCleanComplete,
    events::EvEmergencyStop,
    events::EvEmergencyCleared,
    events::EvNetworkLost,
    events::EvNetworkRestored,
    events::EvBootHardwareReady,
    events::EvBootNetworkReady,
    events::EvBootVisionReady,
    events::EvBootHardwareFailed,
    events::EvBootVisionFailed>;

// Thread-safe MPSC-in-practice queue: the network/hardware/vision mock
// threads and the console-command thread are producers, the Application
// event loop is the single consumer. Uses the blocking variant so the
// consumer thread can sleep between events instead of busy-spinning at
// 100% CPU — important once real vision inference is sharing the CPU
// budget in Phase 4.
class EventQueue {
public:
    explicit EventQueue(std::size_t capacityHint = 1024) : queue_(capacityHint) {}

    template <typename EventT>
    void push(EventT event) {
        queue_.enqueue(EventVariant{std::move(event)});
    }

    // Blocks up to `timeout` waiting for an event. Returns false on timeout
    // (empty queue) so the caller can re-check the shutdown flag promptly.
    bool waitPop(EventVariant& out, std::chrono::milliseconds timeout) {
        return queue_.wait_dequeue_timed(out, timeout);
    }

    // Non-blocking pop, used by tests.
    bool tryPop(EventVariant& out) {
        return queue_.try_dequeue(out);
    }

    std::size_t sizeApprox() const {
        return queue_.size_approx();
    }

private:
    moodycamel::BlockingConcurrentQueue<EventVariant> queue_;
};

} // namespace sse::core
