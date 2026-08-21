#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <variant>

#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Hardware/SerialHardwareDriver.h"

namespace {

struct PtyPair {
    int masterFd = -1;
    std::string slavePath;

    PtyPair() {
        masterFd = posix_openpt(O_RDWR | O_NOCTTY);
        if (masterFd >= 0) {
            grantpt(masterFd);
            unlockpt(masterFd);
            char* name = ptsname(masterFd);
            if (name) {
                slavePath = name;
            }
        }
    }

    ~PtyPair() {
        if (masterFd >= 0) {
            ::close(masterFd);
        }
    }
};

} // namespace

TEST_CASE("SerialHardwareDriver communicates over virtual serial port", "[serial]") {
    PtyPair pty;
    REQUIRE(pty.masterFd >= 0);
    REQUIRE(!pty.slavePath.empty());

    sse::core::EventQueue queue(64);
    sse::core::g_estop_flag.store(false, std::memory_order_release);

    sse::config::SerialConfig cfg;
    cfg.devicePath = pty.slavePath;
    cfg.baudRate = 115200;
    cfg.timeoutMs = 500;
    cfg.commands.dryClean = 0x01;
    cfg.commands.wetClean = 0x02;
    cfg.commands.emergencyHalt = 0xFF;
    cfg.commands.ping = 0x00;

    cfg.interrupts[0xE1] = {"estop", "limit_switch_hit"};
    cfg.interrupts[0xD0] = {"clean_complete", ""};
    cfg.interrupts[0xA0] = {"ack_ping", ""};

    sse::hardware::SerialHardwareDriver driver(queue, cfg, 500);
    REQUIRE(driver.isPortOpen());

    SECTION("Outbound command serialization") {
        driver.beginDryClean(1);
        uint8_t rxByte = 0;
        ssize_t n = ::read(pty.masterFd, &rxByte, 1);
        CHECK(n == 1);
        CHECK(rxByte == 0x01);

        driver.beginWetClean(2);
        n = ::read(pty.masterFd, &rxByte, 1);
        CHECK(n == 1);
        CHECK(rxByte == 0x02);

        driver.emergencyHalt();
        n = ::read(pty.masterFd, &rxByte, 1);
        CHECK(n == 1);
        CHECK(rxByte == 0xFF);
    }

    SECTION("Inbound clean complete signal") {
        driver.beginDryClean(3);
        // Arduino finishes pass and transmits 0xD0
        uint8_t signal = 0xD0;
        ::write(pty.masterFd, &signal, 1);

        sse::core::EventVariant event;
        REQUIRE(queue.waitPop(event, std::chrono::milliseconds(500)));
        CHECK(std::holds_alternative<sse::events::EvCleanComplete>(event));
        auto completeEvt = std::get<sse::events::EvCleanComplete>(event);
        CHECK(completeEvt.sectorId == 3);
    }

    SECTION("Inbound hardware interrupt trips E-Stop flag immediately") {
        CHECK_FALSE(sse::core::IsEstopActive());

        // Arduino limit switch hits and transmits 0xE1
        uint8_t interrupt = 0xE1;
        ::write(pty.masterFd, &interrupt, 1);

        // Give reader thread a few milliseconds to process
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Atomic safety: flag must be latched true
        CHECK(sse::core::IsEstopActive());

        // Event must be queued
        sse::core::EventVariant event;
        REQUIRE(queue.waitPop(event, std::chrono::milliseconds(500)));
        CHECK(std::holds_alternative<sse::events::EvEmergencyStop>(event));
        auto estopEvt = std::get<sse::events::EvEmergencyStop>(event);
        CHECK(estopEvt.reason == "limit_switch_hit");
    }

    SECTION("Self-test ping and ACK handshake") {
        driver.requestSelfTest();

        uint8_t pingByte = 0xFF;
        ssize_t n = ::read(pty.masterFd, &pingByte, 1);
        CHECK(n == 1);
        CHECK(pingByte == 0x00);

        // Send ACK_PING response from Arduino
        uint8_t ack = 0xA0;
        ::write(pty.masterFd, &ack, 1);

        sse::core::EventVariant event;
        REQUIRE(queue.waitPop(event, std::chrono::milliseconds(500)));
        CHECK(std::holds_alternative<sse::events::EvBootHardwareReady>(event));
    }
}
