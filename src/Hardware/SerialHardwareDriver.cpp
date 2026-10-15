#include "solaredge/Hardware/SerialHardwareDriver.h"

#include <iostream>
#include <string>

#include "solaredge/Core/Events.h"
#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/ShutdownFlag.h"
#include "solaredge/Util/Logger.h"

namespace sse::hw {

SerialHardwareDriver::SerialHardwareDriver(core::EventQueue& queue, config::SerialConfig config)
    : queue_(queue),
      config_(config),
      serialPort_(ioContext_) {
    
    util::Logger::Info("SerialHW", "Opening serial port: " + config_.devicePath + " at " + std::to_string(config_.baudRate) + " baud");
    try {
        serialPort_.open(config_.devicePath);
        serialPort_.set_option(asio::serial_port_base::baud_rate(config_.baudRate));
        serialPort_.set_option(asio::serial_port_base::character_size(8));
        serialPort_.set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::one));
        serialPort_.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::none));
        serialPort_.set_option(asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none));
        
        doRead();
        ioThread_ = std::thread([this]() { ioContext_.run(); });
    } catch (const std::exception& e) {
        util::Logger::Error("SerialHW", "Failed to open serial port: " + std::string(e.what()));
    }
}

SerialHardwareDriver::~SerialHardwareDriver() {
    shutdown();
}

void SerialHardwareDriver::requestSelfTest() {
    // In a real scenario, this might ping the Arduino. Here, we just send a boot ready event 
    // immediately or wait for the Arduino to send an 'R' (Ready) byte.
    // For now, let's assume the Arduino is ready if the port is open.
    if (serialPort_.is_open()) {
        queue_.push(events::EvBootHardwareReady{});
    } else {
        queue_.push(events::EvBootHardwareFailed{{}, "Port failed to open"});
    }
}

void SerialHardwareDriver::beginDryClean(int sectorId) {
    sendCommand("D" + std::to_string(sectorId) + "\n");
}

void SerialHardwareDriver::beginWetClean(int sectorId) {
    sendCommand("W" + std::to_string(sectorId) + "\n");
}

void SerialHardwareDriver::stopActuators() {
    sendCommand("H\n");
}

void SerialHardwareDriver::emergencyHalt() {
    sendCommand("H\n");
}

void SerialHardwareDriver::shutdown() {
    if (isShuttingDown_.exchange(true)) {
        return;
    }
    
    util::Logger::Info("SerialHW", "Shutting down serial driver");
    
    if (serialPort_.is_open()) {
        asio::error_code ec;
        serialPort_.cancel(ec);
        serialPort_.close(ec);
    }
    
    ioContext_.stop();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
}

void SerialHardwareDriver::sendCommand(const std::string& cmd) {
    if (!serialPort_.is_open()) return;
    
    asio::error_code ec;
    asio::write(serialPort_, asio::buffer(cmd), ec);
    if (ec) {
        util::Logger::Error("SerialHW", "Write error: " + ec.message());
    }
}

void SerialHardwareDriver::doRead() {
    if (!serialPort_.is_open() || isShuttingDown_.load()) {
        return;
    }
    
    asio::async_read_until(serialPort_, readBuf_, '\n',
        [this](asio::error_code ec, std::size_t bytesTransferred) {
            if (!ec) {
                std::istream is(&readBuf_);
                std::string line;
                std::getline(is, line);
                
                // Remove trailing \r if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                
                processLine(line);
                doRead(); // continue reading
            } else if (ec != asio::error::operation_aborted) {
                util::Logger::Error("SerialHW", "Read error: " + ec.message());
                // For a production system, we'd probably try to reconnect. 
                // For this capstone, we will just stop reading and maybe trigger an error.
            }
        });
}

void SerialHardwareDriver::processLine(const std::string& line) {
    if (line.empty()) return;
    
    char type = line[0];
    std::string payload = line.length() > 1 ? line.substr(1) : "";
    
    switch (type) {
        case 'R':
            queue_.push(events::EvBootHardwareReady{});
            break;
        case 'E':
            // The atomic flag is set first (simulating the fast path)
            core::g_estop_flag.store(true, std::memory_order_release);
            queue_.push(events::EvEmergencyStop{{}, payload.empty() ? "arduino_estop" : payload});
            break;
        case 'C': {
            int sector = 0;
            try { sector = std::stoi(payload); } catch(...) {}
            queue_.push(events::EvCleanComplete{{}, sector});
            break;
        }
        default:
            util::Logger::Warn("SerialHW", "Unknown Arduino message: " + line);
            break;
    }
}

} // namespace sse::hw
