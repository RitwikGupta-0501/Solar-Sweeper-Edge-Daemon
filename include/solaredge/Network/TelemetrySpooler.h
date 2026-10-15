#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace sse::network {

struct SpooledMessage {
    std::string topic;
    std::string payload;
};

class TelemetrySpooler {
public:
    explicit TelemetrySpooler(std::string spoolFilePath = "cache/telemetry_spool.jsonl",
                              std::size_t maxSizeBytes = 50 * 1024 * 1024);
    ~TelemetrySpooler() = default;

    // Appends a message to the persistent spool file.
    void spool(const std::string& topic, const std::string& payload);

    // Checks if there are any spooled messages pending.
    bool hasPending() const;

    // Reads up to maxBatch messages from the front of the spool.
    std::vector<SpooledMessage> peek(std::size_t maxBatch = 100);

    // Discards the first `count` messages from the spool after successful transmission.
    void commitDrained(std::size_t count);

    // Clears all spooled messages and removes the file.
    void clear();

    const std::string& filePath() const { return spoolFilePath_; }

private:
    void ensureDirectoryExists();

    std::string spoolFilePath_;
    std::size_t maxSizeBytes_;
    mutable std::mutex mutex_;
};

} // namespace sse::network
