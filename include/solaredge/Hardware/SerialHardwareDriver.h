#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "solaredge/Config/Config.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Interfaces/IHardwareDriver.h"

namespace sse::hardware {

class SerialHardwareDriver : public iface::IHardwareDriver {
public:
    SerialHardwareDriver(core::EventQueue& queue, config::SerialConfig config, int bootTimeoutMs = 5000);
    ~SerialHardwareDriver() override;

    void requestSelfTest() override;
    void beginDryClean(int sectorId) override;
    void beginWetClean(int sectorId) override;
    void stopActuators() override;
    void emergencyHalt() override;
    void shutdown() override;

    // Helper for testing / inspecting port state
    bool isPortOpen() const { return fd_ >= 0; }

private:
    bool OpenPort();
    void ClosePort();
    bool WriteByte(uint8_t byte);
    void ReaderLoop();
    void HandleIncomingByte(uint8_t byte);

    core::EventQueue& queue_;
    config::SerialConfig config_;
    int bootTimeoutMs_;

    int fd_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<bool> awaitingPingAck_{false};
    std::atomic<int> currentSector_{0};
    std::thread readerThread_;
};

} // namespace sse::hardware
