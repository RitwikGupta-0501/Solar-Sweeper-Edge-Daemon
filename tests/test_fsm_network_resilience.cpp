#include <catch2/catch_test_macros.hpp>

#include "FsmTestHelpers.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Core/States.h"

using namespace sse::core;
using namespace sse::events;

namespace {
void BootToIdle(int totalSectors = 1) {
    sse::testutil::ResetFsmForTest(totalSectors);
    EdgeStateMachine::dispatch(EvBootHardwareReady{});
    EdgeStateMachine::dispatch(EvBootVisionReady{});
    EdgeStateMachine::dispatch(EvBootNetworkReady{});
}
} // namespace

TEST_CASE("EvNetworkLost during Inspection does not abort the cycle", "[fsm][network]") {
    BootToIdle();
    EdgeStateMachine::dispatch(EvStart{});
    REQUIRE(EdgeStateMachine::is_in_state<Inspection>());

    EdgeStateMachine::dispatch(EvNetworkLost{});
    CHECK(EdgeStateMachine::is_in_state<Inspection>());
}

TEST_CASE("EvNetworkLost during DryClean/WetClean does not abort the cycle", "[fsm][network]") {
    BootToIdle();
    EdgeStateMachine::dispatch(EvStart{});
    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, DebrisType::Dust, 0.5f});
    REQUIRE(EdgeStateMachine::is_in_state<DryClean>());

    EdgeStateMachine::dispatch(EvNetworkLost{});
    CHECK(EdgeStateMachine::is_in_state<DryClean>());
}

TEST_CASE("A network drop mid-cycle lands the completed cycle in Offline_Idle, not Idle", "[fsm][network]") {
    BootToIdle();
    EdgeStateMachine::dispatch(EvStart{});
    EdgeStateMachine::dispatch(EvNetworkLost{});
    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, DebrisType::Dust, 0.5f});
    REQUIRE(EdgeStateMachine::is_in_state<DryClean>());

    EdgeStateMachine::dispatch(EvCleanComplete{{}, 0});
    CHECK(EdgeStateMachine::is_in_state<Offline_Idle>());
}

TEST_CASE("EvNetworkRestored while Offline_Idle returns to Idle", "[fsm][network]") {
    BootToIdle();
    EdgeStateMachine::dispatch(EvStart{});
    EdgeStateMachine::dispatch(EvNetworkLost{});
    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, DebrisType::Dust, 0.5f});
    EdgeStateMachine::dispatch(EvCleanComplete{{}, 0});
    REQUIRE(EdgeStateMachine::is_in_state<Offline_Idle>());

    EdgeStateMachine::dispatch(EvNetworkRestored{});
    CHECK(EdgeStateMachine::is_in_state<Idle>());
}

TEST_CASE("Network loss then restore before cycle completion still ends in Idle", "[fsm][network]") {
    // Proves ctx.networkAvailable is re-checked at completion time rather
    // than latched permanently the moment a drop is first observed.
    BootToIdle();
    EdgeStateMachine::dispatch(EvStart{});
    EdgeStateMachine::dispatch(EvNetworkLost{});
    EdgeStateMachine::dispatch(EvNetworkRestored{});
    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, DebrisType::Dust, 0.5f});
    EdgeStateMachine::dispatch(EvCleanComplete{{}, 0});

    CHECK(EdgeStateMachine::is_in_state<Idle>());
}
