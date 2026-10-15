#include <catch2/catch_test_macros.hpp>

#include "solaredge/Config/Config.h"

using sse::config::Config;
using sse::config::ConfigError;

namespace {
std::string FixturePath(const std::string& name) {
    return std::string(FIXTURE_DIR) + "/" + name;
}
} // namespace

TEST_CASE("Config loads a valid file, including // comments", "[config]") {
    const Config cfg = Config::LoadFromFile(FixturePath("valid_config.json"));

    CHECK(cfg.system().totalSectors == 3);
    CHECK(cfg.system().queueCapacityHint == 64);
    CHECK(cfg.system().bootTimeoutMs == 1000);
    CHECK(cfg.system().bootMaxRetries == 2);
    CHECK(cfg.mock().enabled);
    CHECK(cfg.mock().visionCycleMs == 10);
    CHECK(cfg.mock().cleaningDurationMs == 10);
    CHECK(cfg.mqtt().brokerHost == "test-broker");
    CHECK(cfg.mqtt().brokerPort == 1884);
    CHECK(cfg.serial().baudRate == 9600);
    CHECK(cfg.vision().cameraIndex == 1);
}

TEST_CASE("Config rejects a missing file", "[config]") {
    REQUIRE_THROWS_AS(Config::LoadFromFile(FixturePath("does_not_exist.json")), ConfigError);
}

TEST_CASE("Config rejects malformed JSON", "[config]") {
    REQUIRE_THROWS_AS(Config::LoadFromFile(FixturePath("malformed_config.json")), ConfigError);
}

TEST_CASE("Config rejects a missing required field with the field path named", "[config]") {
    // config_missing_field.json omits system.log_level.
    try {
        Config::LoadFromFile(FixturePath("config_missing_field.json"));
        FAIL("expected ConfigError to be thrown");
    } catch (const ConfigError& e) {
        CHECK(std::string(e.what()).find("system.log_level") != std::string::npos);
    }
}



TEST_CASE("Config tolerates unknown fields in mqtt/serial/vision sections", "[config]") {
    // Nothing in Phase 1 consumes these sections deeply, so unrecognized
    // fields (e.g. one a teammate added ahead of Phase 3) must not break
    // loading.
    CHECK_NOTHROW(Config::LoadFromFile(FixturePath("extra_fields_config.json")));
}
