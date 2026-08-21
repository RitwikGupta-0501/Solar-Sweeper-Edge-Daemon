#include <catch2/catch_test_macros.hpp>

#include <unistd.h>
#include <fstream>
#include <nlohmann/json.hpp>

#include "solaredge/Network/TelemetryCache.h"

TEST_CASE("TelemetryCache persists and drains offline telemetry records with fdatasync & sequence tracking", "[telemetry]") {
    const std::string tmpCachePath = "/tmp/test_telemetry_cache.jsonl";
    ::unlink(tmpCachePath.c_str());

    {
        sse::network::TelemetryCache cache(tmpCachePath, "robot01");
        CHECK(cache.count() == 0);

        REQUIRE(cache.append("solarsweeper/robot01/telemetry/status", "{\"state\":\"Inspection\"}"));
        REQUIRE(cache.append("solarsweeper/robot01/telemetry/clean_result", "{\"sector_id\":1,\"success\":true}"));
        REQUIRE(cache.append("solarsweeper/robot01/telemetry/alert", "{\"alert_type\":\"E_STOP\"}"));

        CHECK(cache.count() == 3);

        auto records = cache.readAll();
        REQUIRE(records.size() == 3);
        CHECK(records[0].topic == "solarsweeper/robot01/telemetry/status");
        CHECK(records[0].sequenceId == 1);
        CHECK(records[0].messageId.find("robot01-") == 0);
        CHECK(records[0].timestampMs > 0);

        CHECK(records[1].sequenceId == 2);
        CHECK(records[2].sequenceId == 3);

        // Verify payload JSON contains enriched message_id and seq_id
        auto payload0 = nlohmann::json::parse(records[0].payload);
        CHECK(payload0["message_id"] == records[0].messageId);
        CHECK(payload0["seq_id"] == 1);
        CHECK(payload0["device_id"] == "robot01");
    }

    // Simulate daemon restart before cache flush: records must persist and sequenceCounter must continue monotonically
    {
        sse::network::TelemetryCache cache(tmpCachePath, "robot01");
        CHECK(cache.count() == 3);

        // Append a 4th record after restart
        REQUIRE(cache.append("solarsweeper/robot01/telemetry/status", "{\"state\":\"Idle\"}"));
        CHECK(cache.count() == 4);

        auto records = cache.readAll();
        REQUIRE(records.size() == 4);
        CHECK(records[3].sequenceId == 4);

        // Simulate successful cloud flush
        REQUIRE(cache.clear());
        CHECK(cache.count() == 0);
        CHECK(cache.readAll().empty());
    }

    ::unlink(tmpCachePath.c_str());
}
