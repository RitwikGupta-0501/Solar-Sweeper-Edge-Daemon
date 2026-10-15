#include <catch2/catch_test_macros.hpp>
#include "solaredge/Config/Config.h"
#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Network/MqttNetworkClient.h"

#include <filesystem>
#include <nlohmann/json.hpp>

TEST_CASE("MqttNetworkClient inbound commands and offline telemetry", "[network][mqtt]") {
    sse::core::EventQueue queue(64);
    sse::config::Config config = sse::config::Config::LoadFromFile(std::string(FIXTURE_DIR) + "/valid_config.json");

    std::string testSpool = "build/test_mqtt_spool.jsonl";
    std::filesystem::remove(testSpool);

    sse::network::MqttNetworkClient client(queue, config, testSpool);

    SECTION("handleInboundCommand parses START_CYCLE and pushes EvStart") {
        client.handleInboundCommand("{\"command\": \"START_CYCLE\", \"args\": {}}");

        sse::core::EventVariant ev;
        REQUIRE(queue.tryPop(ev));
        CHECK(std::holds_alternative<sse::events::EvStart>(ev));
    }

    SECTION("handleInboundCommand parses EMERGENCY_HALT, sets atomic estop and pushes EvEmergencyStop") {
        sse::core::g_estop_flag.store(false);
        client.handleInboundCommand("{\"command\": \"EMERGENCY_HALT\", \"args\": {}}");

        CHECK(sse::core::g_estop_flag.load());

        sse::core::EventVariant ev;
        REQUIRE(queue.tryPop(ev));
        REQUIRE(std::holds_alternative<sse::events::EvEmergencyStop>(ev));
        CHECK(std::get<sse::events::EvEmergencyStop>(ev).reason == "cloud_remote_halt");
    }

    SECTION("handleInboundCommand parses CLEAR_FAULT, clears atomic estop and pushes EvEmergencyCleared") {
        sse::core::g_estop_flag.store(true);
        client.handleInboundCommand("{\"command\": \"CLEAR_FAULT\", \"args\": {}}");

        CHECK_FALSE(sse::core::g_estop_flag.load());

        sse::core::EventVariant ev;
        REQUIRE(queue.tryPop(ev));
        CHECK(std::holds_alternative<sse::events::EvEmergencyCleared>(ev));
    }

    SECTION("handleInboundCommand safely ignores unknown commands and malformed json") {
        CHECK_NOTHROW(client.handleInboundCommand("{\"command\": \"UNKNOWN_COMMAND\"}"));
        CHECK_NOTHROW(client.handleInboundCommand("not valid json at all"));

        sse::core::EventVariant ev;
        CHECK_FALSE(queue.tryPop(ev));
    }

    SECTION("publishStatus while offline spools message according to contract schema") {
        client.spooler().clear();
        client.publishStatus("Idle", 0);

        REQUIRE(client.spooler().hasPending());
        auto batch = client.spooler().peek(10);
        REQUIRE(batch.size() == 1);
        CHECK(batch[0].topic == config.mqtt().topicPrefix + "/telemetry/status");

        auto payload = nlohmann::json::parse(batch[0].payload);
        CHECK(payload.contains("timestamp_ms"));
        CHECK(payload["fsm_state"] == "Idle");
        CHECK(payload["current_sector"] == 0);
        CHECK(payload["network_status"] == "offline");
    }

    SECTION("publishDetection while offline spools valid detection contract schema") {
        client.spooler().clear();
        client.publishDetection(2, "bird_dropping", 95.5f, 32.0f, 110.0f);

        REQUIRE(client.spooler().hasPending());
        auto batch = client.spooler().peek(10);
        REQUIRE(batch.size() == 1);
        CHECK(batch[0].topic == config.mqtt().topicPrefix + "/telemetry/detections");

        auto payload = nlohmann::json::parse(batch[0].payload);
        CHECK(payload.contains("timestamp_ms"));
        CHECK(payload["sector"] == 2);
        CHECK(payload["debris_type"] == "bird_dropping");
        CHECK(payload["confidence_score"] == 95.5f);
        CHECK(payload["profiling"]["camera_capture_ms"] == 32.0f);
        CHECK(payload["profiling"]["inference_time_ms"] == 110.0f);
    }

    SECTION("publishAlert while offline spools valid alert contract schema") {
        client.spooler().clear();
        client.publishAlert("CRITICAL", "ESTOP_TRIGGERED", "limit_switch");

        REQUIRE(client.spooler().hasPending());
        auto batch = client.spooler().peek(10);
        REQUIRE(batch.size() == 1);
        CHECK(batch[0].topic == config.mqtt().topicPrefix + "/telemetry/alerts");

        auto payload = nlohmann::json::parse(batch[0].payload);
        CHECK(payload["severity"] == "CRITICAL");
        CHECK(payload["fault_code"] == "ESTOP_TRIGGERED");
        CHECK(payload["reason"] == "limit_switch");
    }

    client.spooler().clear();
    std::filesystem::remove(testSpool);
}
