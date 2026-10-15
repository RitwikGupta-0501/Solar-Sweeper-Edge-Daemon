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
#include "solaredge/Hardware/SerialHardwareDriver.h"
#include "solaredge/Mocks/MockHardwareDriver.h"
#include "solaredge/Mocks/MockNetworkClient.h"
#include "solaredge/Mocks/MockVisionEngine.h"
#include "solaredge/Network/MqttNetworkClient.h"

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
      queue_(config_.system().queueCapacityHint),
      hardware_(config_.mock().enabled 
          ? static_cast<std::unique_ptr<iface::IHardwareDriver>>(std::make_unique<mocks::MockHardwareDriver>(queue_, config_.mock()))
          : static_cast<std::unique_ptr<iface::IHardwareDriver>>(std::make_unique<hw::SerialHardwareDriver>(queue_, config_.serial()))),
      network_(config_.mock().enabled
          ? static_cast<std::unique_ptr<iface::INetworkClient>>(std::make_unique<mocks::MockNetworkClient>(queue_, config_.mock()))
          : static_cast<std::unique_ptr<iface::INetworkClient>>(std::make_unique<network::MqttNetworkClient>(queue_, config_))),
      vision_(std::make_unique<mocks::MockVisionEngine>(queue_, config_.mock())) {}

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
    util::Logger::Info("Application", "Solar Sweeper Edge Daemon starting (Phase 3)");

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
            if (auto m = dynamic_cast<mocks::MockNetworkClient*>(network_.get())) m->simulateNetworkLoss();
        } else if (command == "network-restored") {
            if (auto m = dynamic_cast<mocks::MockNetworkClient*>(network_.get())) m->simulateNetworkRestore();
        } else if (command == "estop") {
            std::string reason;
            std::getline(iss, reason);
            if (auto m = dynamic_cast<mocks::MockHardwareDriver*>(hardware_.get())) {
                m->triggerSyntheticEstop(reason.empty() ? "console" : reason);
            } else {
                hardware_->emergencyHalt();
            }
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
