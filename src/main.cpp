#include <csignal>
#include <iostream>
#include <string>

#include "solaredge/App/Application.h"
#include "solaredge/Config/Config.h"
#include "solaredge/Core/ShutdownFlag.h"

namespace {

// Async-signal-safe by convention: touches only a lock-free atomic<bool>,
// no logging or allocation. Application::run() polls this once per
// event-loop iteration to begin a graceful shutdown.
extern "C" void HandleShutdownSignal(int /*signal*/) {
    sse::core::g_shutdownRequested.store(true, std::memory_order_relaxed);
}

} // namespace

int main(int argc, char** argv) {
    const std::string configPath = argc > 1 ? argv[1] : "config/daemon.config.json";

    std::signal(SIGINT, HandleShutdownSignal);
    std::signal(SIGTERM, HandleShutdownSignal);

    try {
        sse::config::Config config = sse::config::Config::LoadFromFile(configPath);
        sse::app::Application app(std::move(config));
        return app.run();
    } catch (const sse::config::ConfigError& e) {
        std::cerr << "[FATAL] Config error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
}
