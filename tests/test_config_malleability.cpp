#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <unistd.h>

#include "solaredge/Config/Config.h"
#include "solaredge/Util/JsonHelper.h"

using nlohmann::json;
using sse::config::Config;
using sse::util::JsonHelper;

TEST_CASE("JsonHelper extracts fields with fallback pointers", "[malleability]") {
    SECTION("Direct field name") {
        json j = {{"sector_id", 5}};
        int sector = JsonHelper::ExtractField<int>(j, {"/sector_id", "/sectorId", "/sector"}, 0);
        CHECK(sector == 5);
    }

    SECTION("CamelCase fallback") {
        json j = {{"sectorId", 7}};
        int sector = JsonHelper::ExtractField<int>(j, {"/sector_id", "/sectorId", "/sector"}, 0);
        CHECK(sector == 7);
    }

    SECTION("Nested payload fallback") {
        json j = {{"data", {{"target_sector", 9}}}};
        int sector = JsonHelper::ExtractField<int>(j, {"/sector_id", "/data/target_sector", "/sector"}, 0);
        CHECK(sector == 9);
    }

    SECTION("Default when missing") {
        json j = {{"unrelated", "hello"}};
        int sector = JsonHelper::ExtractField<int>(j, {"/sector_id", "/sectorId"}, 42);
        CHECK(sector == 42);
    }
}

TEST_CASE("JsonHelper parses hex and decimal byte values", "[malleability]") {
    CHECK(JsonHelper::ParseByte(json("0x01")) == 0x01);
    CHECK(JsonHelper::ParseByte(json("0xFF")) == 0xFF);
    CHECK(JsonHelper::ParseByte(json("0xE1")) == 0xE1);
    CHECK(JsonHelper::ParseByte(json(16)) == 16);
    CHECK(JsonHelper::ParseByte(json("invalid"), 0xAA) == 0xAA);
}

TEST_CASE("Config loads malleable serial and vision tables", "[malleability]") {
    const std::string customConfigJson = R"({
      "system": {
        "log_level": "info",
        "total_sectors": 4,
        "queue_capacity_hint": 128,
        "boot_timeout_ms": 2000,
        "boot_max_retries": 1
      },
      "mock": {
        "enabled": true,
        "hardware_fault_injection": false,
        "network_start_offline": false,
        "vision_cycle_ms": 50,
        "cleaning_duration_ms": 50
      },
      "serial": {
        "device_path": "/dev/ttyCustom",
        "baud_rate": 57600,
        "timeout_ms": 1000,
        "commands": {
          "dry_clean": "0x10",
          "wet_clean": "0x20",
          "emergency_halt": "0xEE",
          "ping": "0x05"
        },
        "interrupts": {
          "0x33": { "event": "estop", "reason": "laser_tripwire" },
          "0x77": { "event": "clean_complete" }
        }
      },
      "mqtt": {
        "broker_host": "cloud.solar.internal",
        "broker_port": 8883,
        "routes": [
          { "topic": "robot/sweep/start", "action": "START" },
          { "topic": "robot/sweep/kill",  "action": "ABORT" }
        ],
        "telemetry_topics": {
          "status": "robot/heartbeat"
        }
      },
      "vision": {
        "model_path": "models/custom_yolo.onnx",
        "class_map": {
          "0": { "name": "sand", "action": "dry_clean" },
          "5": { "name": "mud",  "action": "wet_clean" }
        }
      }
    })";

    // Write temporary config file to test loading
    const std::string tmpPath = "/tmp/test_malleable_config.json";
    {
        std::ofstream out(tmpPath);
        out << customConfigJson;
    }

    const Config cfg = Config::LoadFromFile(tmpPath);

    // Verify serial opcodes
    CHECK(cfg.serial().commands.dryClean == 0x10);
    CHECK(cfg.serial().commands.wetClean == 0x20);
    CHECK(cfg.serial().commands.emergencyHalt == 0xEE);
    CHECK(cfg.serial().commands.ping == 0x05);
    CHECK(cfg.serial().baudRate == 57600);

    // Verify custom interrupt lookup
    REQUIRE(cfg.serial().interrupts.find(0x33) != cfg.serial().interrupts.end());
    CHECK(cfg.serial().interrupts.at(0x33).eventType == "estop");
    CHECK(cfg.serial().interrupts.at(0x33).reason == "laser_tripwire");

    REQUIRE(cfg.serial().interrupts.find(0x77) != cfg.serial().interrupts.end());
    CHECK(cfg.serial().interrupts.at(0x77).eventType == "clean_complete");

    // Verify vision class lookup
    REQUIRE(cfg.vision().classMap.find(0) != cfg.vision().classMap.end());
    CHECK(cfg.vision().classMap.at(0).name == "sand");
    CHECK(cfg.vision().classMap.at(0).action == "dry_clean");

    REQUIRE(cfg.vision().classMap.find(5) != cfg.vision().classMap.end());
    CHECK(cfg.vision().classMap.at(5).name == "mud");
    CHECK(cfg.vision().classMap.at(5).action == "wet_clean");

    // Verify MQTT routes
    REQUIRE(cfg.mqtt().routes.size() == 2);
    CHECK(cfg.mqtt().routes[0].topic == "robot/sweep/start");
    CHECK(cfg.mqtt().routes[0].action == "START");

    ::unlink(tmpPath.c_str());
}
