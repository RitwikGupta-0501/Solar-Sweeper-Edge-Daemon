#pragma once

#include <atomic>

namespace sse::core {

// Set to true the instant a hardware emergency-stop interrupt is observed,
// bypassing the event queue entirely so actuation halts in microseconds
// instead of waiting for the FSM to drain whatever is ahead of it.
// The formal EvEmergencyStop event is still pushed afterward so the FSM can
// record the transition for logging/telemetry, but the flag alone is what
// actually stops motion. See States.h for the guard convention: every
// react() that would command physical motion checks this flag first.
extern std::atomic<bool> g_estop_flag;

inline bool IsEstopActive() {
    return g_estop_flag.load(std::memory_order_acquire);
}

} // namespace sse::core
