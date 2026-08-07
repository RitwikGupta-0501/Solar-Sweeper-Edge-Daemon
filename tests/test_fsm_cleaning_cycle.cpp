#include <catch2/catch_test_macros.hpp>

#include "FsmTestHelpers.h"
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

TEST_CASE("Full cycle: Idle -> Inspection -> DryClean -> Inspection -> WetClean -> Idle", "[fsm][cycle]") {
    BootToIdle(/*totalSectors=*/2);
    REQUIRE(EdgeStateMachine::is_in_state<Idle>());

    EdgeStateMachine::dispatch(EvStart{});
    CHECK(EdgeStateMachine::is_in_state<Inspection>());

    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, DebrisType::Dust, 0.9f});
    CHECK(EdgeStateMachine::is_in_state<DryClean>());

    EdgeStateMachine::dispatch(EvCleanComplete{{}, 0});
    CHECK(EdgeStateMachine::is_in_state<Inspection>());

    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 1, DebrisType::BirdDropping, 0.95f});
    CHECK(EdgeStateMachine::is_in_state<WetClean>());

    EdgeStateMachine::dispatch(EvCleanComplete{{}, 1});
    CHECK(EdgeStateMachine::is_in_state<Idle>());
}

TEST_CASE("EvSectorClear advances sectors without cleaning; last sector returns to Idle", "[fsm][cycle]") {
    BootToIdle(/*totalSectors=*/2);
    EdgeStateMachine::dispatch(EvStart{});
    REQUIRE(EdgeStateMachine::is_in_state<Inspection>());

    EdgeStateMachine::dispatch(EvSectorClear{{}, 0});
    CHECK(EdgeStateMachine::is_in_state<Inspection>());

    EdgeStateMachine::dispatch(EvSectorClear{{}, 1});
    CHECK(EdgeStateMachine::is_in_state<Idle>());
}

TEST_CASE("Only BirdDropping routes to WetClean; other debris types route to DryClean", "[fsm][cycle]") {
    for (const DebrisType type : {DebrisType::Dust, DebrisType::Leaf, DebrisType::Unknown}) {
        BootToIdle();
        EdgeStateMachine::dispatch(EvStart{});
        EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, type, 0.5f});
        CHECK(EdgeStateMachine::is_in_state<DryClean>());
    }

    BootToIdle();
    EdgeStateMachine::dispatch(EvStart{});
    EdgeStateMachine::dispatch(EvDebrisDetected{{}, 0, DebrisType::BirdDropping, 0.5f});
    CHECK(EdgeStateMachine::is_in_state<WetClean>());
}
