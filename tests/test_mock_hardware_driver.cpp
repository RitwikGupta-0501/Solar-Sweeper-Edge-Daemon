#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <variant>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Mocks/MockHardwareDriver.h"

using namespace sse;
using namespace std::chrono_literals;

namespace {
config::MockConfig FastMockConfig() {
    config::MockConfig cfg;
    cfg.enabled = true;
    cfg.hardwareFaultInjection = false;
    cfg.networkStartOffline = false;
    cfg.visionCycleMs = 10;
    cfg.cleaningDurationMs = 10;
    return cfg;
}
} // namespace

TEST_CASE("MockHardwareDriver reports EvBootHardwareReady after a self-test", "[mocks][hardware]") {
    core::EventQueue queue;
    mocks::MockHardwareDriver hardware(queue, FastMockConfig());

    hardware.requestSelfTest();

    core::EventVariant event;
    REQUIRE(queue.waitPop(event, 1s));
    CHECK(std::holds_alternative<events::EvBootHardwareReady>(event));

    hardware.shutdown();
}

TEST_CASE("MockHardwareDriver reports EvBootHardwareFailed when fault injection is enabled", "[mocks][hardware]") {
    core::EventQueue queue;
    auto cfg = FastMockConfig();
    cfg.hardwareFaultInjection = true;
    mocks::MockHardwareDriver hardware(queue, cfg);

    hardware.requestSelfTest();

    core::EventVariant event;
    REQUIRE(queue.waitPop(event, 1s));
    CHECK(std::holds_alternative<events::EvBootHardwareFailed>(event));

    hardware.shutdown();
}

TEST_CASE("MockHardwareDriver reports EvCleanComplete for the sector it was told to clean",
          "[mocks][hardware]") {
    core::EventQueue queue;
    mocks::MockHardwareDriver hardware(queue, FastMockConfig());

    hardware.beginDryClean(2);

    core::EventVariant event;
    REQUIRE(queue.waitPop(event, 1s));
    const auto* clean = std::get_if<events::EvCleanComplete>(&event);
    REQUIRE(clean != nullptr);
    CHECK(clean->sectorId == 2);

    hardware.shutdown();
}

TEST_CASE("triggerSyntheticEstop sets the flag before the event is even dequeued", "[mocks][hardware]") {
    core::EventQueue queue;
    mocks::MockHardwareDriver hardware(queue, FastMockConfig());
    core::g_estop_flag.store(false, std::memory_order_release);

    hardware.triggerSyntheticEstop("unit test");
    CHECK(core::IsEstopActive());

    core::EventVariant event;
    REQUIRE(queue.waitPop(event, 1s));
    const auto* stop = std::get_if<events::EvEmergencyStop>(&event);
    REQUIRE(stop != nullptr);
    CHECK(stop->reason == "unit test");

    hardware.shutdown();
}
