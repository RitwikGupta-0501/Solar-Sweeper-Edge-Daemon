#include <catch2/catch_test_macros.hpp>

#include "FsmTestHelpers.h"
#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Core/States.h"

using namespace sse::core;
using namespace sse::events;

namespace {
void BootToIdle(int totalSectors = 3) {
    sse::testutil::ResetFsmForTest(totalSectors);
    EdgeStateMachine::dispatch(EvBootHardwareReady{});
    EdgeStateMachine::dispatch(EvBootVisionReady{});
    EdgeStateMachine::dispatch(EvBootNetworkReady{});
}
} // namespace

TEST_CASE("EvEmergencyStop transitions Inspection to Error", "[fsm][estop]") {
    BootToIdle();
    EdgeStateMachine::dispatch(EvStart{});
    REQUIRE(EdgeStateMachine::is_in_state<Inspection>());

    EdgeStateMachine::dispatch(EvEmergencyStop{{}, "test"});
    CHECK(EdgeStateMachine::is_in_state<Error>());
}

TEST_CASE("EvEmergencyStop transitions DryClean/WetClean to Error", "[fsm][estop]") {
    BootToIdle();
    EdgeStateMachine::dispatch(EvStart{});
    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, DebrisType::Dust, 0.5f});
    REQUIRE(EdgeStateMachine::is_in_state<DryClean>());

    EdgeStateMachine::dispatch(EvEmergencyStop{{}, "test"});
    CHECK(EdgeStateMachine::is_in_state<Error>());
}

TEST_CASE("Error swallows subsequent events with no state change", "[fsm][estop]") {
    BootToIdle();
    EdgeStateMachine::dispatch(EvEmergencyStop{{}, "test"});
    REQUIRE(EdgeStateMachine::is_in_state<Error>());

    EdgeStateMachine::dispatch(EvStart{});
    CHECK(EdgeStateMachine::is_in_state<Error>());

    EdgeStateMachine::dispatch(EvCleanComplete{{}, 0});
    CHECK(EdgeStateMachine::is_in_state<Error>());

    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, DebrisType::Dust, 0.5f});
    CHECK(EdgeStateMachine::is_in_state<Error>());
}

TEST_CASE("Error recovers to Boot_Init via EvEmergencyCleared", "[fsm][estop]") {
    BootToIdle();
    EdgeStateMachine::dispatch(EvEmergencyStop{{}, "test"});
    REQUIRE(EdgeStateMachine::is_in_state<Error>());

    EdgeStateMachine::dispatch(EvEmergencyCleared{});
    CHECK(EdgeStateMachine::is_in_state<Boot_Init>());
}

TEST_CASE("Race A: a queued EvStart processed after E-Stop already latched is discarded", "[fsm][estop][race]") {
    // Simulates two producer threads racing on the lock-free queue: one
    // already flipped the atomic flag before this event is popped.
    BootToIdle();
    REQUIRE(EdgeStateMachine::is_in_state<Idle>());

    g_estop_flag.store(true, std::memory_order_release);
    EdgeStateMachine::dispatch(EvStart{});

    CHECK(EdgeStateMachine::is_in_state<Idle>());
}

TEST_CASE("Race B: a queued EvCleanComplete processed after E-Stop already latched is discarded",
          "[fsm][estop][race]") {
    BootToIdle();
    EdgeStateMachine::dispatch(EvStart{});
    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, DebrisType::Dust, 0.5f});
    REQUIRE(EdgeStateMachine::is_in_state<DryClean>());

    g_estop_flag.store(true, std::memory_order_release);
    EdgeStateMachine::dispatch(EvCleanComplete{{}, 0});
    CHECK(EdgeStateMachine::is_in_state<DryClean>());

    // The formal event that actually latches Error -- still pending on the
    // real queue in production -- arrives next and is honored normally.
    EdgeStateMachine::dispatch(EvEmergencyStop{{}, "test"});
    CHECK(EdgeStateMachine::is_in_state<Error>());
}
