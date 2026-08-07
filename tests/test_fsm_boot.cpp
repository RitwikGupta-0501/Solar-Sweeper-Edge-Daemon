#include <catch2/catch_test_macros.hpp>

#include "FsmTestHelpers.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Core/States.h"

using namespace sse::core;
using namespace sse::events;
using sse::testutil::ResetFsmForTest;

TEST_CASE("Boot_Init moves to Idle once hardware, network, and vision are all ready", "[fsm][boot]") {
    ResetFsmForTest();
    REQUIRE(EdgeStateMachine::is_in_state<Boot_Init>());

    EdgeStateMachine::dispatch(EvBootHardwareReady{});
    EdgeStateMachine::dispatch(EvBootVisionReady{});
    EdgeStateMachine::dispatch(EvBootNetworkReady{});

    CHECK(EdgeStateMachine::is_in_state<Idle>());
}

TEST_CASE("Boot_Init moves to Offline_Idle when network never becomes ready", "[fsm][boot]") {
    ResetFsmForTest();

    EdgeStateMachine::dispatch(EvBootHardwareReady{});
    EdgeStateMachine::dispatch(EvBootVisionReady{});
    // No EvBootNetworkReady -- network is best-effort at boot, it must not
    // block hardware+vision (the safety/decision-critical subsystems) from
    // bringing the daemon into service.

    CHECK(EdgeStateMachine::is_in_state<Offline_Idle>());
}

TEST_CASE("Boot_Init escalates to Hardware_Fault once retries are exhausted", "[fsm][boot]") {
    ResetFsmForTest(/*totalSectors=*/3, /*bootMaxRetries=*/2);

    EdgeStateMachine::dispatch(EvBootHardwareFailed{{}, "fault 1"});
    CHECK(EdgeStateMachine::is_in_state<Boot_Init>());

    EdgeStateMachine::dispatch(EvBootHardwareFailed{{}, "fault 2"});
    CHECK(EdgeStateMachine::is_in_state<Boot_Init>());

    EdgeStateMachine::dispatch(EvBootHardwareFailed{{}, "fault 3"});
    CHECK(EdgeStateMachine::is_in_state<Hardware_Fault>());
}

TEST_CASE("A vision self-test failure escalates to Hardware_Fault exactly like a hardware failure", "[fsm][boot]") {
    ResetFsmForTest(/*totalSectors=*/3, /*bootMaxRetries=*/0);

    EdgeStateMachine::dispatch(EvBootVisionFailed{{}, "no camera detected"});

    CHECK(EdgeStateMachine::is_in_state<Hardware_Fault>());
}

TEST_CASE("Hardware_Fault is terminal within the running process", "[fsm][boot]") {
    ResetFsmForTest(/*totalSectors=*/3, /*bootMaxRetries=*/0);
    EdgeStateMachine::dispatch(EvBootHardwareFailed{{}, "fatal"});
    REQUIRE(EdgeStateMachine::is_in_state<Hardware_Fault>());

    EdgeStateMachine::dispatch(EvEmergencyCleared{});
    CHECK(EdgeStateMachine::is_in_state<Hardware_Fault>());
}
