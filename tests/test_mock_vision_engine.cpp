#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <variant>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Mocks/MockVisionEngine.h"

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

TEST_CASE("MockVisionEngine reports EvBootVisionReady after initialize", "[mocks][vision]") {
    core::EventQueue queue;
    mocks::MockVisionEngine vision(queue, FastMockConfig());

    vision.initialize();

    core::EventVariant event;
    REQUIRE(queue.waitPop(event, 1s));
    CHECK(std::holds_alternative<events::EvBootVisionReady>(event));

    vision.shutdown();
}

TEST_CASE("MockVisionEngine cycles through its canned results deterministically", "[mocks][vision]") {
    // Canned sequence (see MockVisionEngine.cpp): Dust, BirdDropping, clear, Leaf, then repeats.
    core::EventQueue queue;
    mocks::MockVisionEngine vision(queue, FastMockConfig());

    vision.requestInference(0);
    core::EventVariant first;
    REQUIRE(queue.waitPop(first, 1s));
    const auto* debris1 = std::get_if<events::EvDebrisDetected>(&first);
    REQUIRE(debris1 != nullptr);
    CHECK(debris1->sectorId == 0);
    CHECK(debris1->debrisType == events::DebrisType::Dust);

    vision.requestInference(1);
    core::EventVariant second;
    REQUIRE(queue.waitPop(second, 1s));
    const auto* debris2 = std::get_if<events::EvDebrisDetected>(&second);
    REQUIRE(debris2 != nullptr);
    CHECK(debris2->debrisType == events::DebrisType::BirdDropping);

    vision.requestInference(2);
    core::EventVariant third;
    REQUIRE(queue.waitPop(third, 1s));
    CHECK(std::holds_alternative<events::EvSectorClear>(third));

    vision.shutdown();
}
