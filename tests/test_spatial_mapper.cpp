#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "solaredge/Vision/SpatialCoordinateMapper.h"
#include "solaredge/Vision/VisionTypes.h"

using namespace sse::vision;
using Catch::Matchers::WithinAbs;

TEST_CASE("SpatialCoordinateMapper inverse letterbox coordinate reconstruction (V.1)", "[vision][spatial]") {
    // 1080p camera: 1920 x 1080 -> 640 x 640 net
    // Scale = min(640/1920, 640/1080) = 640/1920 = 1/3 (0.333333f)
    // Scaled W = 640, Scaled H = 360
    // PadX = (640 - 640)/2 = 0
    // PadY = (640 - 360)/2 = 140
    SpatialCoordinateMapper mapper(1920, 1080, 1.60f, 0.20f, 4, 0.15f);

    PreprocessingParams prep;
    prep.netWidth = 640;
    prep.netHeight = 640;
    prep.scale = 1.0f / 3.0f;
    prep.padX = 0.0f;
    prep.padY = 140.0f;

    SECTION("Exact sensor coordinate reconstruction without distortion") {
        BoundingBox netBox{100.0f, 140.0f, 200.0f, 260.0f};
        BoundingBox camBox = mapper.invertLetterbox(netBox, prep);

        // x1 = (100 - 0) * 3 = 300
        // y1 = (140 - 140) * 3 = 0
        // x2 = (200 - 0) * 3 = 600
        // y2 = (260 - 140) * 3 = 360
        CHECK_THAT(camBox.x1, WithinAbs(300.0f, 0.01f));
        CHECK_THAT(camBox.y1, WithinAbs(0.0f, 0.01f));
        CHECK_THAT(camBox.x2, WithinAbs(600.0f, 0.01f));
        CHECK_THAT(camBox.y2, WithinAbs(360.0f, 0.01f));
    }

    SECTION("Boundary clamping to camera sensor dimensions") {
        // Out-of-bounds net box
        BoundingBox netBox{-50.0f, 50.0f, 800.0f, 700.0f};
        BoundingBox camBox = mapper.invertLetterbox(netBox, prep);

        CHECK(camBox.x1 >= 0.0f);
        CHECK(camBox.y1 >= 0.0f);
        CHECK(camBox.x2 <= 1919.0f);
        CHECK(camBox.y2 <= 1079.0f);
    }
}

TEST_CASE("SpatialCoordinateMapper nozzle array zone selection (V.4)", "[vision][spatial]") {
    // 4 nozzles across 1.6m (1600mm) panel height -> 400mm zones
    // Nozzle 0: [0, 400)
    // Nozzle 1: [400, 800)
    // Nozzle 2: [800, 1200)
    // Nozzle 3: [1200, 1600)
    SpatialCoordinateMapper mapper(1920, 1080, 1.60f, 0.20f, 4, 0.15f);

    CHECK(mapper.selectNozzle(200.0f) == 0);
    CHECK(mapper.selectNozzle(600.0f) == 1);
    CHECK(mapper.selectNozzle(1000.0f) == 2);
    CHECK(mapper.selectNozzle(1400.0f) == 3);

    SECTION("Out-of-bounds vertical coordinates clamped safely") {
        CHECK(mapper.selectNozzle(-50.0f) == 0);
        CHECK(mapper.selectNozzle(1800.0f) == 3);
        CHECK(mapper.selectNozzle(5000.0f) == 3);
    }

    SECTION("Single nozzle fallback") {
        SpatialCoordinateMapper singleNozzleMapper(1920, 1080, 1.60f, 0.20f, 1, 0.15f);
        CHECK(singleNozzleMapper.selectNozzle(200.0f) == 0);
        CHECK(singleNozzleMapper.selectNozzle(1200.0f) == 0);
    }
}

TEST_CASE("SpatialCoordinateMapper look-ahead kinematic delay calculation (V.5)", "[vision][spatial]") {
    // Velocity v = 0.15 m/s (150 mm/s)
    // Look-ahead D = 0.20 m (200 mm)
    SpatialCoordinateMapper mapper(1920, 1080, 1.60f, 0.20f, 4, 0.15f);

    SECTION("Debris at 50mm forward offset") {
        // Delta D = 200 - 50 = 150mm. t = 150 / 150 = 1.0s = 1000ms
        int delayMs = mapper.calculateActuationDelayMs(50.0f);
        CHECK(delayMs == 1000);
    }

    SECTION("Debris at 0mm (optical horizon)") {
        // Delta D = 200 - 0 = 200mm. t = 200 / 150 = 1.333s = 1333ms
        int delayMs = mapper.calculateActuationDelayMs(0.0f);
        CHECK(delayMs == 1333);
    }

    SECTION("Debris at 200mm (under nozzle manifold)") {
        int delayMs = mapper.calculateActuationDelayMs(200.0f);
        CHECK(delayMs == 0);
    }

    SECTION("Debris past nozzle manifold (x > 200mm)") {
        int delayMs = mapper.calculateActuationDelayMs(250.0f);
        CHECK(delayMs == 0);
    }
}

TEST_CASE("SpatialCoordinateMapper full mapToPhysical pipeline", "[vision][spatial]") {
    SpatialCoordinateMapper mapper(1920, 1080, 1.60f, 0.20f, 4, 0.15f);

    PreprocessingParams prep;
    prep.netWidth = 640;
    prep.netHeight = 640;
    prep.scale = 1.0f / 3.0f;
    prep.padX = 0.0f;
    prep.padY = 140.0f;

    // Centered box in net space: x in [250, 350] (center 300), y in [200, 300] (center 250)
    BoundingBox netBox{250.0f, 200.0f, 350.0f, 300.0f};
    Detection det = mapper.mapToPhysical(netBox, 2, 0.92f, prep);

    CHECK(det.classId == 2);
    CHECK(det.debrisType == sse::events::DebrisType::BirdDropping);
    CHECK_THAT(det.confidence, WithinAbs(0.92f, 0.001f));

    // Cam center X = (250 + 350) / 2 * 3 = 900 px
    // physicalYmm = (900 / 1920) * 1600 = 750 mm (which is zone 1: [400, 800))
    CHECK_THAT(det.physicalYmm, WithinAbs(750.0f, 1.0f));
    CHECK(det.targetNozzleId == 1);
}
