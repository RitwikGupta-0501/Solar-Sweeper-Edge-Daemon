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

    sse::hw::SerialHardwareDriver driver(queue, cfg);

    SECTION("Outbound command serialization") {
        driver.beginDryClean(1);
        char buf[16] = {0};
        ssize_t n = ::read(pty.masterFd, buf, sizeof(buf) - 1);
        CHECK(n > 0);
        CHECK(std::string(buf, n) == "D1\n");

        driver.beginWetClean(2);
        memset(buf, 0, sizeof(buf));
        n = ::read(pty.masterFd, buf, sizeof(buf) - 1);
        CHECK(n > 0);
        CHECK(std::string(buf, n) == "W2\n");

        driver.emergencyHalt();
        memset(buf, 0, sizeof(buf));
        n = ::read(pty.masterFd, buf, sizeof(buf) - 1);
        CHECK(n > 0);
        CHECK(std::string(buf, n) == "H\n");
    }

    SECTION("Inbound clean complete signal") {
        driver.beginDryClean(3);
        const char* signal = "C 3\n";
        ssize_t written = ::write(pty.masterFd, signal, strlen(signal));
        (void)written;

        sse::core::EventVariant event;
        REQUIRE(queue.waitPop(event, std::chrono::milliseconds(500)));
        CHECK(std::holds_alternative<sse::events::EvCleanComplete>(event));
        auto completeEvt = std::get<sse::events::EvCleanComplete>(event);
        CHECK(completeEvt.sectorId == 3);
    }

    SECTION("Inbound hardware interrupt trips E-Stop flag immediately") {
        CHECK_FALSE(sse::core::IsEstopActive());

        const char* interrupt = "E limit_switch_hit\n";
        ssize_t written = ::write(pty.masterFd, interrupt, strlen(interrupt));
        (void)written;

        // Give reader thread a few milliseconds to process
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Atomic safety: flag must be latched true
        CHECK(sse::core::IsEstopActive());

        // Event must be queued
        sse::core::EventVariant event;
        REQUIRE(queue.waitPop(event, std::chrono::milliseconds(500)));
        CHECK(std::holds_alternative<sse::events::EvEmergencyStop>(event));
        auto estopEvt = std::get<sse::events::EvEmergencyStop>(event);
        CHECK(estopEvt.reason == " limit_switch_hit");
    }

    SECTION("Self-test and ready handshake") {
        driver.requestSelfTest();

        sse::core::EventVariant event;
        REQUIRE(queue.waitPop(event, std::chrono::milliseconds(500)));
        CHECK(std::holds_alternative<sse::events::EvBootHardwareReady>(event));

        // Inbound 'R' signal also emits EvBootHardwareReady
        const char* readySig = "R\n";
        ssize_t written = ::write(pty.masterFd, readySig, strlen(readySig));
        (void)written;

        REQUIRE(queue.waitPop(event, std::chrono::milliseconds(500)));
        CHECK(std::holds_alternative<sse::events::EvBootHardwareReady>(event));
    }
}
