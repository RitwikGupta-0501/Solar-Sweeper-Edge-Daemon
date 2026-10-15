#include <catch2/catch_test_macros.hpp>
#include "solaredge/Network/TelemetrySpooler.h"

#include <filesystem>

TEST_CASE("TelemetrySpooler writes, peeks, and commits messages to disk", "[network][spooler]") {
    std::string testSpoolPath = "build/test_spool.jsonl";
    std::filesystem::remove(testSpoolPath);

    sse::network::TelemetrySpooler spooler(testSpoolPath);
    REQUIRE_FALSE(spooler.hasPending());

    SECTION("spool and peek") {
        spooler.spool("solarsweeper/robot01/telemetry/status", "{\"fsm_state\":\"Idle\"}");
        spooler.spool("solarsweeper/robot01/telemetry/health", "{\"cpu_temp_c\":65.0}");

        REQUIRE(spooler.hasPending());

        auto batch = spooler.peek(10);
        REQUIRE(batch.size() == 2);
        CHECK(batch[0].topic == "solarsweeper/robot01/telemetry/status");
        CHECK(batch[0].payload == "{\"fsm_state\":\"Idle\"}");
        CHECK(batch[1].topic == "solarsweeper/robot01/telemetry/health");
        CHECK(batch[1].payload == "{\"cpu_temp_c\":65.0}");

        // Commit first item only
        spooler.commitDrained(1);
        REQUIRE(spooler.hasPending());

        auto remaining = spooler.peek(10);
        REQUIRE(remaining.size() == 1);
        CHECK(remaining[0].topic == "solarsweeper/robot01/telemetry/health");

        // Commit remaining item
        spooler.commitDrained(1);
        REQUIRE_FALSE(spooler.hasPending());
    }

    SECTION("clear deletes file") {
        spooler.spool("topic", "payload");
        REQUIRE(spooler.hasPending());
        spooler.clear();
        REQUIRE_FALSE(spooler.hasPending());
    }

    SECTION("enforces max size limit") {
        // Create spooler with tiny 100 byte limit
        sse::network::TelemetrySpooler tinySpooler(testSpoolPath, 100);
        tinySpooler.spool("topic1", "{\"data\":\"some small payload\"}");
        REQUIRE(tinySpooler.hasPending());

        // This will exceed the 100 byte threshold
        tinySpooler.spool("topic2", "{\"data\":\"a very long payload that should be rejected by size bounding\"}");
        tinySpooler.spool("topic3", "{\"data\":\"another payload that should be rejected by size bounding\"}");

        auto batch = tinySpooler.peek(10);
        CHECK(batch.size() == 1);
        CHECK(batch[0].topic == "topic1");

        tinySpooler.clear();
    }

    std::filesystem::remove(testSpoolPath);
}
