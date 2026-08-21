#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace sse::network {

struct CachedRecord {
    std::string topic;
    std::string payload;
    uint64_t timestampMs = 0;
};

class TelemetryCache {
public:
    explicit TelemetryCache(std::string cacheFilePath);
    ~TelemetryCache() = default;

    // Appends an unsent telemetry record to local disk
    bool append(const std::string& topic, const std::string& jsonPayload);

    // Reads all persisted records in chronological order
    std::vector<CachedRecord> readAll();

    // Clears the persisted cache file after successful MQTT flush
    bool clear();

    // Returns the number of cached entries
    std::size_t count();

    const std::string& filePath() const { return filePath_; }

private:
    std::string filePath_;
    std::mutex mutex_;
};

} // namespace sse::network
