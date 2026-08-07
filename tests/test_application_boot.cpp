#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

#include "solaredge/App/Application.h"
#include "solaredge/Config/Config.h"
#include "solaredge/Core/ShutdownFlag.h"

using namespace sse;
using namespace std::chrono_literals;

namespace {
std::string FixturePath(const std::string& name) {
    return std::string(FIXTURE_DIR) + "/" + name;
}
} // namespace

TEST_CASE("Application boots and shuts down cleanly with the console disabled", "[application]") {
    core::g_shutdownRequested.store(false, std::memory_order_relaxed);

    config::Config cfg = config::Config::LoadFromFile(FixturePath("valid_config.json"));
    app::Application application(std::move(cfg), /*enableConsole=*/false);

    std::thread runner([&application] { application.run(); });

    // valid_config.json's mock latencies are well under this window, so by
    // the time we request shutdown the daemon has reached Idle/Offline_Idle.
    std::this_thread::sleep_for(300ms);
    app::Application::RequestShutdown();

    runner.join();
    // Reaching this line without hanging means shutdown joined every
    // worker thread cleanly -- that is the assertion.
    SUCCEED("Application shut down without hanging");
}
