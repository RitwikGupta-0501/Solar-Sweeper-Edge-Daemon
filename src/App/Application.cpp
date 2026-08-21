#include "solaredge/App/Application.h"

#include <unistd.h>

#include <chrono>
#include <iostream>
#include <sstream>
#include <utility>

#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Core/ShutdownFlag.h"
#include "solaredge/Core/States.h"
#include "solaredge/Hardware/SerialHardwareDriver.h"
#include "solaredge/Mocks/MockHardwareDriver.h"
#include "solaredge/Mocks/MockNetworkClient.h"
#include "solaredge/Mocks/MockVisionEngine.h"
#include "solaredge/Util/Logger.h"

namespace sse::app {

namespace {

events::DebrisType ParseDebrisType(const std::string& text) {
    if (text == "dust") return events::DebrisType::Dust;
    if (text == "leaf") return events::DebrisType::Leaf;
    if (text == "bird") return events::DebrisType::BirdDropping;
    return events::DebrisType::Unknown;
}

} // namespace

Application::Application(config::Config config, bool enableConsole)
    : config_(std::move(config)),
      enableConsole_(enableConsole),
      queue_(config_.system().queueCapacityHint) {
    
    // Dynamic Factory: selects Mock vs Real implementations based on configuration flags
    if (config_.mock().hardwareDriver) {
        util::Logger::Info("Application", "Using MockHardwareDriver");
        hardware_ = std::make_unique<mocks::MockHardwareDriver>(queue_, config_.mock());
    } else {
        util::Logger::Info("Application", "Instantiating real SerialHardwareDriver on " + config_.serial().devicePath);
        hardware_ = std::make_unique<hardware::SerialHardwareDriver>(queue_, config_.serial(), config_.system().bootTimeoutMs);
    }

    if (config_.mock().networkClient) {
        util::Logger::Info("Application", "Using MockNetworkClient");
        network_ = std::make_unique<mocks::MockNetworkClient>(queue_, config_.mock());
    } else {
        // Will swap to PahoNetworkClient in Phase 3
        util::Logger::Info("Application", "Using MockNetworkClient (Real MQTT client scheduled for Phase 3)");
        network_ = std::make_unique<mocks::MockNetworkClient>(queue_, config_.mock());
    }

    if (config_.mock().visionEngine) {
        util::Logger::Info("Application", "Using MockVisionEngine");
        vision_ = std::make_unique<mocks::MockVisionEngine>(queue_, config_.mock());
    } else {
        // Will swap to OnnxVisionEngine in Phase 4
        util::Logger::Info("Application", "Using MockVisionEngine (Real ONNX vision scheduled for Phase 4)");
        vision_ = std::make_unique<mocks::MockVisionEngine>(queue_, config_.mock());
    }
}

Application::~Application() {
    if (hardware_) hardware_->shutdown();
    if (network_) network_->shutdown();
    if (vision_) vision_->shutdown();
    if (consoleThread_.joinable()) {
        ::close(STDIN_FILENO);
        consoleThread_.join();
    }
}

void Application::RequestShutdown() {
    core::g_shutdownRequested.store(true, std::memory_order_relaxed);
}

int Application::run() {
    util::Logger::SetLevel(config_.system().logLevel);
    util::Logger::Info("Application", "Solar Sweeper Edge Daemon starting (Malleable Architecture, Phase 2 HAL ready)");

    core::EdgeStateMachine::SetContext(hardware_.get(), network_.get(), vision_.get(), &queue_,
                                        config_.system().totalSectors, config_.system().bootMaxRetries);
    core::EdgeStateMachine::start();

    if (enableConsole_) {
        PrintConsoleHelp();
        consoleThread_ = std::thread(&Application::ConsoleLoop, this);
    }

    while (!core::g_shutdownRequested.load()) {
        core::EventVariant event;
        if (queue_.waitPop(event, std::chrono::milliseconds(50))) {
            core::DispatchToFsm(event);
        }
    }

    util::Logger::Info("Application", "Shutdown requested -- stopping");
    if (hardware_) hardware_->stopActuators();
    if (hardware_) hardware_->shutdown();
    if (network_) network_->shutdown();
    if (vision_) vision_->shutdown();

    if (consoleThread_.joinable()) {
        ::close(STDIN_FILENO);
        consoleThread_.join();
    }

    util::Logger::Info("Application", "Shutdown complete");
    return 0;
}

void Application::ConsoleLoop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string command;
        iss >> command;

        if (command.empty()) {
            continue;
        }
        if (command == "start") {
            queue_.push(events::EvStart{});
        } else if (command == "debris") {
            std::string typeText;
            iss >> typeText;
            queue_.push(events::EvDebrisDetected{{}, 0, ParseDebrisType(typeText), 1.0f});
        } else if (command == "clear") {
            queue_.push(events::EvSectorClear{{}, 0});
        } else if (command == "network-lost") {
            queue_.push(events::EvNetworkLost{});
        } else if (command == "network-restored") {
            queue_.push(events::EvNetworkRestored{});
        } else if (command == "estop") {
            std::string reason;
            std::getline(iss, reason);
            core::g_estop_flag.store(true, std::memory_order_release);
            queue_.push(events::EvEmergencyStop{{}, reason.empty() ? "console" : reason});
        } else if (command == "estop-clear") {
            queue_.push(events::EvEmergencyCleared{});
        } else if (command == "quit" || command == "exit") {
            RequestShutdown();
            break;
        } else if (command == "help") {
            PrintConsoleHelp();
        } else {
            util::Logger::Warn("Console", "unknown command '" + command + "' -- try 'help'");
        }
    }
}

void Application::PrintConsoleHelp() {
    std::cout << "\nAvailable commands:\n"
                 "  start              begin a cleaning pass\n"
                 "  debris <type>      report debris in the current sector (dust|leaf|bird|unknown)\n"
                 "  clear              report the current sector as clear\n"
                 "  network-lost       simulate an MQTT/Wi-Fi drop\n"
                 "  network-restored   simulate the network coming back\n"
                 "  estop [reason]     trigger a synthetic emergency stop\n"
                 "  estop-clear        clear the emergency stop and re-run boot self-test\n"
                 "  quit               shut the daemon down\n"
              << std::endl;
}

} // namespace sse::app
