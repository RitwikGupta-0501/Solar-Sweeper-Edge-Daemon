#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Vision/OnnxVisionEngine.h"
#include "solaredge/Vision/VisionTypes.h"

using namespace sse::vision;
using Catch::Matchers::WithinAbs;

TEST_CASE("Greedy NMS Deduplication (V.2)", "[vision][nms]") {
    // 3 overlapping bounding boxes for BirdDropping (class 2)
    // Box 1: [100, 100, 200, 200] (Area = 10000), Conf = 0.95
    // Box 2: [110, 110, 200, 200] (High overlap with Box 1), Conf = 0.85
    // Box 3: [190, 190, 290, 290] (Small overlap with Box 1), Conf = 0.70

    std::vector<Detection> candidates;

    Detection d1;
    d1.box = {100.0f, 100.0f, 200.0f, 200.0f};
    d1.classId = 2;
    d1.confidence = 0.95f;
    d1.debrisType = sse::events::DebrisType::BirdDropping;
    candidates.push_back(d1);

    Detection d2;
    d2.box = {110.0f, 110.0f, 200.0f, 200.0f};
    d2.classId = 2;
    d2.confidence = 0.85f;
    d2.debrisType = sse::events::DebrisType::BirdDropping;
    candidates.push_back(d2);

    Detection d3;
    d3.box = {190.0f, 190.0f, 290.0f, 290.0f};
    d3.classId = 2;
    d3.confidence = 0.70f;
    d3.debrisType = sse::events::DebrisType::BirdDropping;
    candidates.push_back(d3);

    // Compute pairwise IoU
    float iou1_2 = OnnxVisionEngine::computeIoU(d1.box, d2.box);
    float iou1_3 = OnnxVisionEngine::computeIoU(d1.box, d3.box);

    REQUIRE(iou1_2 > 0.45f); // High overlap -> should be suppressed
    REQUIRE(iou1_3 < 0.45f); // Low overlap -> should be kept

    OnnxVisionEngine::applyNMS(candidates, 0.45f);

    REQUIRE(candidates.size() == 2);
    CHECK(candidates[0].confidence == 0.95f);
    CHECK(candidates[1].confidence == 0.70f);

    SECTION("Different classes with high overlap are not suppressed") {
        std::vector<Detection> multiClassCandidates;
        Detection bird = d1;
        Detection leaf = d2;
        leaf.classId = 1; // Leaf
        leaf.debrisType = sse::events::DebrisType::Leaf;

        multiClassCandidates.push_back(bird);
        multiClassCandidates.push_back(leaf);

        OnnxVisionEngine::applyNMS(multiClassCandidates, 0.45f);
        CHECK(multiClassCandidates.size() == 2);
    }
}

TEST_CASE("Multi-Label Debris Severity Arbitration (V.3)", "[vision][arbitration]") {
    SECTION("BirdDropping overrides Dust and Leaf") {
        std::vector<Detection> detections;
        // 5x Dust detections
        for (int i = 0; i < 5; ++i) {
            Detection dust;
            dust.debrisType = sse::events::DebrisType::Dust;
            dust.classId = 0;
            dust.confidence = 0.80f + 0.02f * i;
            detections.push_back(dust);
        }

        // 1x BirdDropping detection
        Detection bird;
        bird.debrisType = sse::events::DebrisType::BirdDropping;
        bird.classId = 2;
        bird.confidence = 0.75f;
        detections.push_back(bird);

        float bestConf = 0.0f;
        sse::events::DebrisType chosen = OnnxVisionEngine::arbitrateDebris(detections, bestConf);

        CHECK(chosen == sse::events::DebrisType::BirdDropping);
        CHECK_THAT(bestConf, WithinAbs(0.75f, 0.001f));
    }

    SECTION("Leaf overrides Dust") {
        std::vector<Detection> detections;
        for (int i = 0; i < 3; ++i) {
            Detection dust;
            dust.debrisType = sse::events::DebrisType::Dust;
            dust.classId = 0;
            dust.confidence = 0.90f;
            detections.push_back(dust);
        }

        Detection leaf;
        leaf.debrisType = sse::events::DebrisType::Leaf;
        leaf.classId = 1;
        leaf.confidence = 0.65f;
        detections.push_back(leaf);

        float bestConf = 0.0f;
        sse::events::DebrisType chosen = OnnxVisionEngine::arbitrateDebris(detections, bestConf);

        CHECK(chosen == sse::events::DebrisType::Leaf);
        CHECK_THAT(bestConf, WithinAbs(0.65f, 0.001f));
    }

    SECTION("Empty detections return None") {
        std::vector<Detection> detections;
        float bestConf = 0.0f;
        sse::events::DebrisType chosen = OnnxVisionEngine::arbitrateDebris(detections, bestConf);

        CHECK(chosen == sse::events::DebrisType::None);
        CHECK(bestConf == 0.0f);
    }
}

TEST_CASE("Letterbox Preprocessing Normalization (V.6)", "[vision][preprocessing]") {
    CameraFrame frame;
    frame.width = 1920;
    frame.height = 1080;
    frame.channels = 3;
    frame.pixelFormat = 0; // RGB24
    frame.data.assign(frame.width * frame.height * 3, 255); // Solid white image

    std::vector<float> tensor;
    PreprocessingParams params;
    OnnxVisionEngine::preprocessFrame(frame, 640, 640, tensor, params);

    CHECK(params.netWidth == 640);
    CHECK(params.netHeight == 640);
    CHECK_THAT(params.scale, WithinAbs(640.0f / 1920.0f, 0.001f));
    CHECK_THAT(params.padX, WithinAbs(0.0f, 0.01f));
    CHECK_THAT(params.padY, WithinAbs(140.0f, 0.01f));

    REQUIRE(tensor.size() == 3 * 640 * 640);

    // Padding region (y = 0..139) should contain letterbox gray (114 / 255 ~= 0.447f)
    CHECK_THAT(tensor[0], WithinAbs(114.0f / 255.0f, 0.01f));

    // Active image region (y = 140..499, x = 0..639) should be normalized 1.0f (255/255)
    int activeIdx = 140 * 640 + 320;
    CHECK_THAT(tensor[activeIdx], WithinAbs(1.0f, 0.01f));
}

TEST_CASE("ProcessFrameAndDispatch dispatches correct FSM events (V.7)", "[vision][dispatch]") {
    sse::core::EventQueue queue(32);
    sse::config::Config cfg = sse::config::Config::LoadFromFile(std::string(FIXTURE_DIR) + "/valid_config.json");

    OnnxVisionEngine engine(queue, cfg);

    SECTION("Empty detections emit EvSectorClear") {
        CameraFrame cleanFrame;
        cleanFrame.width = 640;
        cleanFrame.height = 480;
        cleanFrame.channels = 3;
        cleanFrame.data.assign(640 * 480 * 3, 0);

        engine.processFrameAndDispatch(cleanFrame, 3, 12.5f);

        sse::core::EventVariant event;
        REQUIRE(queue.waitPop(event, std::chrono::milliseconds(200)));
        CHECK(std::holds_alternative<sse::events::EvSectorClear>(event));
        auto clearEvt = std::get<sse::events::EvSectorClear>(event);
        CHECK(clearEvt.sectorId == 3);
    }
}

TEST_CASE("YOLO Decoupled Head Post-Processing and Spatial Mapping", "[vision][postprocess]") {
    sse::core::EventQueue queue(32);
    sse::config::Config cfg = sse::config::Config::LoadFromFile(std::string(FIXTURE_DIR) + "/valid_config.json");
    OnnxVisionEngine engine(queue, cfg);

    PreprocessingParams prep;
    prep.netWidth = 640;
    prep.netHeight = 640;
    prep.scale = 1.0f / 3.0f;
    prep.padX = 0.0f;
    prep.padY = 140.0f;

    // Simulate channels-first YOLOv8 output: [1, 7, 100] (xc, yc, w, h, dust, leaf, bird)
    // 7 channels, 100 candidate anchors
    const int numChannels = 7;
    const int numAnchors = 100;
    std::vector<float> rawOutput(numChannels * numAnchors, 0.0f);

    // Anchor 10: high confidence bird dropping at net center [300, 250, 60, 40]
    rawOutput[0 * numAnchors + 10] = 300.0f; // xc
    rawOutput[1 * numAnchors + 10] = 250.0f; // yc
    rawOutput[2 * numAnchors + 10] = 60.0f;  // w
    rawOutput[3 * numAnchors + 10] = 40.0f;  // h
    rawOutput[4 * numAnchors + 10] = 0.05f;  // dust
    rawOutput[5 * numAnchors + 10] = 0.10f;  // leaf
    rawOutput[6 * numAnchors + 10] = 0.88f;  // bird dropping

    // Anchor 20: low confidence below threshold
    rawOutput[0 * numAnchors + 20] = 100.0f;
    rawOutput[1 * numAnchors + 20] = 100.0f;
    rawOutput[2 * numAnchors + 20] = 20.0f;
    rawOutput[3 * numAnchors + 20] = 20.0f;
    rawOutput[4 * numAnchors + 20] = 0.30f; // dust (below 0.50)

    auto detections = engine.postprocessOutput(rawOutput.data(), numChannels, numAnchors, prep);

    REQUIRE(detections.size() == 1);
    CHECK(detections[0].classId == 2);
    CHECK(detections[0].debrisType == sse::events::DebrisType::BirdDropping);
    CHECK_THAT(detections[0].confidence, WithinAbs(0.88f, 0.001f));
    CHECK(detections[0].targetNozzleId >= 0);
    CHECK(detections[0].targetNozzleId < 4);
}

TEST_CASE("OnnxVisionEngine Camera Timeout & Degraded Hardware Escalation (V.8)", "[vision][fault]") {
    sse::core::EventQueue queue(32);
    sse::config::Config cfg = sse::config::Config::LoadFromFile(std::string(FIXTURE_DIR) + "/valid_config.json");

    OnnxVisionEngine engine(queue, cfg);

    // Initializing with nonexistent model or camera fails gracefully with EvBootVisionFailed
    engine.initialize();

    sse::core::EventVariant event;
    REQUIRE(queue.waitPop(event, std::chrono::milliseconds(500)));
    CHECK(std::holds_alternative<sse::events::EvBootVisionFailed>(event));
}

