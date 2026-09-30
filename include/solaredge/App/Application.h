#pragma once

#include <memory>
#include <thread>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Interfaces/IHardwareDriver.h"
#include "solaredge/Interfaces/INetworkClient.h"
#include "solaredge/Interfaces/IVisionEngine.h"

namespace sse::app {

// Composition root: owns the event queue, the three driver instances, and
// drives the FSM's lifecycle. main.cpp stays a thin entry point that just
// constructs this and calls run().
//
// Phase 1 always wires the Mock* implementations directly, since that is
// the entire point of this phase. Phase 2-4 introduce a config-driven or
// factory-based seam here to select real vs. mock per interface -- the FSM
// and event loop below don't change either way, since they only ever see
// the abstract interfaces.
class Application {
public:
    explicit Application(config::Config config, bool enableConsole = true);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Blocking: runs the boot sequence and the FSM event loop until
    // shutdown is requested, then tears down cleanly. Returns a process
    // exit code.
    int run();

    // Thread-safe: lets tests (and the SIGINT/SIGTERM handlers) request a
    // stop without needing a live Application reference.
    static void RequestShutdown();

private:
    void ConsoleLoop();
    static void PrintConsoleHelp();

    config::Config config_;
    bool enableConsole_;

    core::EventQueue queue_;
    std::unique_ptr<iface::IHardwareDriver> hardware_;
    std::unique_ptr<iface::INetworkClient> network_;
    std::unique_ptr<iface::IVisionEngine> vision_;

    std::thread consoleThread_;
};

} // namespace sse::app
