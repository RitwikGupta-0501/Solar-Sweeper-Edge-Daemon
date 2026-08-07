#pragma once

#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/States.h"

namespace sse::testutil {

// TinyFSM states are process-global singletons, so every FSM test must
// start from a known state. Call this at the top of every TEST_CASE that
// touches EdgeStateMachine. Context pointers default to nullptr: every
// react()/entry() in States.h guards hardware/network/vision calls with a
// null check, so pure FSM-logic tests don't need real mocks at all.
inline void ResetFsmForTest(int totalSectors = 3, int bootMaxRetries = 3) {
    core::g_estop_flag.store(false, std::memory_order_release);
    core::EdgeStateMachine::ResetContextForTesting();
    core::EdgeStateMachine::SetContext(/*hardware=*/nullptr, /*network=*/nullptr, /*vision=*/nullptr,
                                        /*queue=*/nullptr, totalSectors, bootMaxRetries);
    core::EdgeStateMachine::start();
}

} // namespace sse::testutil
