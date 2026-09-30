#pragma once

#include <atomic>
#include <string>
#include <thread>

#include <asio.hpp>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Interfaces/IHardwareDriver.h"

namespace sse::hw {

class SerialHardwareDriver : public iface::IHardwareDriver {
public:
    SerialHardwareDriver(core::EventQueue& queue, config::SerialConfig config);
    ~SerialHardwareDriver() override;

    void requestSelfTest() override;
    void beginDryClean(int sectorId) override;
    void beginWetClean(int sectorId) override;
    void stopActuators() override;
    void emergencyHalt() override;
    void shutdown() override;

private:
    void doRead();
    void processLine(const std::string& line);
    void sendCommand(const std::string& cmd);

    core::EventQueue& queue_;
    config::SerialConfig config_;

    asio::io_context ioContext_;
    asio::serial_port serialPort_;
    std::thread ioThread_;

    asio::streambuf readBuf_;
    
    std::atomic<bool> isShuttingDown_{false};
};

} // namespace sse::hw
