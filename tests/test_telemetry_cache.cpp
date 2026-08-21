#include <catch2/catch_test_macros.hpp>

#include <unistd.h>
#include <fstream>

#include "solaredge/Network/TelemetryCache.h"

TEST_CASE("TelemetryCache persists and drains offline telemetry records", "[telemetry]") {
    const std::string tmpCachePath = "/tmp/test_telemetry_cache.jsonl";
    ::unlink(tmpCachePath.c_str());

    {
        sse::network::TelemetryCache cache(tmpCachePath);
        CHECK(cache.count() == 0);

        REQUIRE(cache.append("solarsweeper/robot01/telemetry/status", "{\"state\":\"Inspection\"}"));
        REQUIRE(cache.append("solarsweeper/robot01/telemetry/clean_result", "{\"sector_id\":1,\"success\":true}"));
        REQUIRE(cache.append("solarsweeper/robot01/telemetry/alert", "{\"alert_type\":\"E_STOP\"}"));

        CHECK(cache.count() == 3);

        auto records = cache.readAll();
        REQUIRE(records.size() == 3);
        CHECK(records[0].topic == "solarsweeper/robot01/telemetry/status");
        CHECK(records[0].payload == "{\"state\":\"Inspection\"}");
        CHECK(records[0].timestampMs > 0);

        CHECK(records[1].topic == "solarsweeper/robot01/telemetry/clean_result");
        CHECK(records[2].topic == "solarsweeper/robot01/telemetry/alert");
    }

    // Simulate daemon restart before cache flush: records must persist
    {
        sse::network::TelemetryCache cache(tmpCachePath);
        CHECK(cache.count() == 3);

        auto records = cache.readAll();
        REQUIRE(records.size() == 3);

        // Simulate successful cloud flush
        REQUIRE(cache.clear());
        CHECK(cache.count() == 0);
        CHECK(cache.readAll().empty());
    }

    ::unlink(tmpCachePath.c_str());
}
