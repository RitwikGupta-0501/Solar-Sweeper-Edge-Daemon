#include "solaredge/Hardware/SerialHardwareDriver.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>
#include <chrono>

#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Util/Logger.h"

namespace sse::hardware {

namespace {

speed_t GetBaudConstant(int baudRate) {
    switch (baudRate) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return B115200;
    }
}

} // namespace

SerialHardwareDriver::SerialHardwareDriver(core::EventQueue& queue, config::SerialConfig config, int bootTimeoutMs)
    : queue_(queue), config_(std::move(config)), bootTimeoutMs_(bootTimeoutMs) {
    OpenPort();
}

SerialHardwareDriver::~SerialHardwareDriver() {
    shutdown();
}

bool SerialHardwareDriver::OpenPort() {
    if (fd_ >= 0) {
        return true;
    }

    fd_ = ::open(config_.devicePath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        util::Logger::Warn("SerialDriver", "Unable to open serial device " + config_.devicePath +
                                                ": " + std::strerror(errno));
        return false;
    }

    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd_, &tty) != 0) {
        util::Logger::Warn("SerialDriver", "tcgetattr failed: " + std::string(std::strerror(errno)));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    speed_t speed = GetBaudConstant(config_.baudRate);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // 8-N-1 raw mode configuration
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit chars
    tty.c_cflag &= ~PARENB;                    // no parity
    tty.c_cflag &= ~CSTOPB;                    // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                   // no hardware flow control
    tty.c_cflag |= (CLOCAL | CREAD);           // enable read & ignore ctrl lines

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);    // no software flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN); // raw input
    tty.c_oflag &= ~OPOST;                                    // raw output

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5; // 0.5s timeout per read

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        util::Logger::Warn("SerialDriver", "tcsetattr failed: " + std::string(std::strerror(errno)));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    running_.store(true);
    readerThread_ = std::thread(&SerialHardwareDriver::ReaderLoop, this);
    util::Logger::Info("SerialDriver", "Serial port " + config_.devicePath + " opened successfully at " +
                                            std::to_string(config_.baudRate) + " baud");
    return true;
}

void SerialHardwareDriver::ClosePort() {
    running_.store(false);
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SerialHardwareDriver::WriteByte(uint8_t byte) {
    if (fd_ < 0) {
        if (!OpenPort()) {
            return false;
        }
    }

    ssize_t written = ::write(fd_, &byte, 1);
    if (written != 1) {
        util::Logger::Warn("SerialDriver", "Failed to write byte 0x" + std::to_string(byte) +
                                                " to serial: " + std::strerror(errno));
        return false;
    }
    return true;
}

void SerialHardwareDriver::requestSelfTest() {
    if (fd_ < 0) {
        if (!OpenPort()) {
            queue_.push(events::EvBootHardwareFailed{{}, "serial_port_unavailable"});
            return;
        }
    }

    awaitingPingAck_.store(true);
    if (!WriteByte(config_.commands.ping)) {
        awaitingPingAck_.store(false);
        queue_.push(events::EvBootHardwareFailed{{}, "ping_write_failed"});
        return;
    }

    // Ping write succeeded. If Arduino replies with ACK_PING (0xA0),
    // ReaderLoop() will dispatch EvBootHardwareReady.
    // In background, if no response within timeout, deliver boot failure.
    std::thread([this]() {
        auto start = std::chrono::steady_clock::now();
        while (awaitingPingAck_.load() && running_.load()) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count() > bootTimeoutMs_) {
                if (awaitingPingAck_.exchange(false)) {
                    util::Logger::Warn("SerialDriver", "Self-test ping timed out after " +
                                                            std::to_string(bootTimeoutMs_) + "ms");
                    queue_.push(events::EvBootHardwareFailed{{}, "ping_timeout"});
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }).detach();
}

void SerialHardwareDriver::beginDryClean(int sectorId) {
    currentSector_.store(sectorId);
    util::Logger::Info("SerialDriver", "Sending dry clean command (0x" +
                                            std::to_string(config_.commands.dryClean) +
                                            ") for sector " + std::to_string(sectorId));
    WriteByte(config_.commands.dryClean);
}

void SerialHardwareDriver::beginWetClean(int sectorId) {
    currentSector_.store(sectorId);
    util::Logger::Info("SerialDriver", "Sending wet clean command (0x" +
                                            std::to_string(config_.commands.wetClean) +
                                            ") for sector " + std::to_string(sectorId));
    WriteByte(config_.commands.wetClean);
}

void SerialHardwareDriver::stopActuators() {
    util::Logger::Info("SerialDriver", "Sending stop actuators command (0x" +
                                            std::to_string(config_.commands.emergencyHalt) + ")");
    WriteByte(config_.commands.emergencyHalt);
}

void SerialHardwareDriver::emergencyHalt() {
    util::Logger::Info("SerialDriver", "Sending emergency halt command (0x" +
                                            std::to_string(config_.commands.emergencyHalt) + ")");
    WriteByte(config_.commands.emergencyHalt);
}

void SerialHardwareDriver::shutdown() {
    ClosePort();
}

void SerialHardwareDriver::ReaderLoop() {
    while (running_.load()) {
        if (fd_ < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(fd_, &readFds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout for select

        int ret = select(fd_ + 1, &readFds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(fd_, &readFds)) {
            uint8_t buffer[64];
            ssize_t bytesRead = ::read(fd_, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                for (ssize_t i = 0; i < bytesRead; ++i) {
                    HandleIncomingByte(buffer[i]);
                }
            } else if (bytesRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                util::Logger::Warn("SerialDriver", "Serial read error: " + std::string(std::strerror(errno)));
            }
        }
    }
}

void SerialHardwareDriver::HandleIncomingByte(uint8_t byte) {
    auto it = config_.interrupts.find(byte);
    if (it != config_.interrupts.end()) {
        const auto& def = it->second;
        if (def.eventType == "estop") {
            // SOTA Safety: Atomic hardware bypass ALWAYS fires first!
            core::g_estop_flag.store(true, std::memory_order_release);
            util::Logger::Error("SerialDriver", "Hardware interrupt triggered E-Stop: " + def.reason);
            queue_.push(events::EvEmergencyStop{{}, def.reason.empty() ? "hardware_interrupt" : def.reason});
        } else if (def.eventType == "clean_complete") {
            util::Logger::Info("SerialDriver", "Received clean complete signal from Arduino for sector " +
                                                    std::to_string(currentSector_.load()));
            queue_.push(events::EvCleanComplete{{}, currentSector_.load()});
        } else if (def.eventType == "ack_ping") {
            util::Logger::Info("SerialDriver", "Received ping ACK from Arduino");
            if (awaitingPingAck_.exchange(false)) {
                queue_.push(events::EvBootHardwareReady{});
            }
        } else if (def.eventType == "ack") {
            util::Logger::Debug("SerialDriver", "Received command ACK (0x" + std::to_string(byte) + ")");
        }
    } else {
        util::Logger::Debug("SerialDriver", "Unhandled incoming byte: 0x" + std::to_string(byte));
    }
}

} // namespace sse::hardware
