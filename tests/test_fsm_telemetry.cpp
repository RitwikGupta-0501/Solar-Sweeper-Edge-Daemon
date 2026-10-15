#include <catch2/catch_test_macros.hpp>
#include "FsmTestHelpers.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Core/States.h"
#include "solaredge/Mocks/MockHardwareDriver.h"
#include "solaredge/Mocks/MockNetworkClient.h"
#include "solaredge/Mocks/MockVisionEngine.h"

TEST_CASE("FSM state transitions publish telemetry to INetworkClient", "[fsm][telemetry]") {
    sse::core::EventQueue queue(64);
    sse::config::MockConfig mockCfg;
    mockCfg.networkStartOffline = false;

    sse::mocks::MockHardwareDriver hw(queue, mockCfg);
    sse::mocks::MockNetworkClient net(queue, mockCfg);
    sse::mocks::MockVisionEngine vis(queue, mockCfg);

    sse::core::g_estop_flag.store(false, std::memory_order_release);
    sse::core::EdgeStateMachine::ResetContextForTesting();
    sse::core::EdgeStateMachine::SetContext(&hw, &net, &vis, &queue, 2, 3);
    sse::core::EdgeStateMachine::start(); // Enters Boot_Init

    CHECK(net.statusCallCount() >= 1);
    CHECK(net.lastStatusState() == "Boot_Init");

    SECTION("boot completion publishes Idle status") {
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootHardwareReady{});
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootVisionReady{});
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootNetworkReady{});

        CHECK(net.lastStatusState() == "Idle");
        CHECK(net.lastStatusSector() == -1);
    }

    SECTION("start cycle publishes Inspection status and debris detection telemetry") {
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootHardwareReady{});
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootVisionReady{});
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootNetworkReady{});

        // Start cycle
        sse::core::EdgeStateMachine::dispatch(sse::events::EvStart{});
        CHECK(net.lastStatusState() == "Inspection");
        CHECK(net.lastStatusSector() == 0);

        // Debris detected
        sse::events::EvDebrisDetected debrisEv;
        debrisEv.sectorId = 0;
        debrisEv.debrisType = sse::events::DebrisType::BirdDropping;
        debrisEv.confidence = 92.5f;
        debrisEv.cameraCaptureMs = 28.0f;
        debrisEv.inferenceTimeMs = 105.0f;

        sse::core::EdgeStateMachine::dispatch(debrisEv);

        CHECK(net.detectionCallCount() == 1);
        CHECK(net.lastDetectionType() == "bird_dropping");
        CHECK(net.lastStatusState() == "WetClean");
        CHECK(net.lastStatusSector() == 0);
    }

    SECTION("cleaning cycle completion publishes cycle summary") {
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootHardwareReady{});
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootVisionReady{});
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootNetworkReady{});

        sse::core::EdgeStateMachine::dispatch(sse::events::EvStart{}); // Sector 0 inspection
        sse::core::EdgeStateMachine::dispatch(sse::events::EvSectorClear{{}, 0}); // Move to Sector 1
        CHECK(net.lastStatusSector() == 1);

        sse::core::EdgeStateMachine::dispatch(sse::events::EvSectorClear{{}, 1}); // Complete cycle
        CHECK(net.cycleSummaryCallCount() == 1);
        CHECK(net.lastCycleSectors() == 2);
        CHECK(net.lastStatusState() == "Idle");
    }

    SECTION("emergency stop publishes Error status and critical alert") {
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootHardwareReady{});
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootVisionReady{});
        sse::core::EdgeStateMachine::dispatch(sse::events::EvBootNetworkReady{});

        sse::core::EdgeStateMachine::dispatch(sse::events::EvStart{});
        sse::core::EdgeStateMachine::dispatch(sse::events::EvEmergencyStop{{}, "test_estop_button"});

        CHECK(net.lastStatusState() == "Error");
        CHECK(net.alertCallCount() == 1);
        CHECK(net.lastAlertSeverity() == "CRITICAL");
        CHECK(net.lastAlertCode() == "ESTOP_TRIGGERED");
        CHECK(net.lastAlertReason() == "test_estop_button");
    }
}
