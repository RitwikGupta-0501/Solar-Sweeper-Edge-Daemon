#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace sse::network {

struct CachedRecord {
    std::string messageId;
    uint64_t sequenceId = 0;
    std::string topic;
    std::string payload;
    uint64_t timestampMs = 0;
};

// Thread-safe, power-cut-resilient persistent telemetry buffer on non-volatile disk.
// Uses POSIX fdatasync() on every append to guarantee immediate physical flash commit
// on embedded systems (e.g. Raspberry Pi), surviving unexpected power cuts/crashes.
class TelemetryCache {
public:
    explicit TelemetryCache(std::string cacheFilePath, std::string deviceId = "edge-daemon-01");
    ~TelemetryCache() = default;

    // Appends an unsent telemetry record to non-volatile disk and forces physical flash sync (fdatasync)
    bool append(const std::string& topic, const std::string& jsonPayload);

    // Reads all persisted records in chronological order
    std::vector<CachedRecord> readAll();

    // Atomically clears the persisted cache file after successful MQTT flush
    bool clear();

    // Returns the number of cached entries
    std::size_t count();

    const std::string& filePath() const { return filePath_; }
    const std::string& deviceId() const { return deviceId_; }

private:
    std::string filePath_;
    std::string deviceId_;
    std::atomic<uint64_t> sequenceCounter_{0};
    std::mutex mutex_;
};

} // namespace sse::network
