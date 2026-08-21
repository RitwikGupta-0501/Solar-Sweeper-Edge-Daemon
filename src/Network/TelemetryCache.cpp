#include "solaredge/Network/TelemetryCache.h"

#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

#include "solaredge/Util/Logger.h"

namespace sse::network {

using nlohmann::json;

TelemetryCache::TelemetryCache(std::string cacheFilePath, std::string deviceId)
    : filePath_(std::move(cacheFilePath)), deviceId_(std::move(deviceId)) {
    // Determine the highest sequenceId already recorded in existing cache file
    auto existing = readAll();
    for (const auto& rec : existing) {
        if (rec.sequenceId > sequenceCounter_.load()) {
            sequenceCounter_.store(rec.sequenceId);
        }
    }
}

bool TelemetryCache::append(const std::string& topic, const std::string& jsonPayload) {
    std::lock_guard<std::mutex> lock(mutex_);

    int fd = ::open(filePath_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        util::Logger::Warn("TelemetryCache", "Failed to open cache file for writing: " +
                                                filePath_ + " (" + std::strerror(errno) + ")");
        return false;
    }

    uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    uint64_t seq = ++sequenceCounter_;

    std::string msgId = deviceId_ + "-" + std::to_string(nowMs) + "-" + std::to_string(seq);

    // Ingest or enhance payload JSON with idempotency metadata
    json enrichedPayload;
    try {
        enrichedPayload = json::parse(jsonPayload);
    } catch (...) {
        enrichedPayload = json::object();
        enrichedPayload["raw"] = jsonPayload;
    }

    if (!enrichedPayload.contains("message_id")) {
        enrichedPayload["message_id"] = msgId;
    }
    if (!enrichedPayload.contains("seq_id")) {
        enrichedPayload["seq_id"] = seq;
    }
    if (!enrichedPayload.contains("timestamp_ms")) {
        enrichedPayload["timestamp_ms"] = nowMs;
    }
    if (!enrichedPayload.contains("device_id")) {
        enrichedPayload["device_id"] = deviceId_;
    }

    json record;
    record["message_id"] = msgId;
    record["seq_id"] = seq;
    record["topic"] = topic;
    record["payload"] = enrichedPayload.dump();
    record["timestamp_ms"] = nowMs;

    std::string line = record.dump() + "\n";

    ssize_t written = ::write(fd, line.data(), line.size());
    if (written < 0 || static_cast<size_t>(written) != line.size()) {
        util::Logger::Warn("TelemetryCache", "Failed to write complete record to disk: " +
                                                std::string(std::strerror(errno)));
        ::close(fd);
        return false;
    }

    // Industrial Hardening: Force kernel page cache to physical non-volatile storage immediately!
    // This eliminates data loss during sudden Raspberry Pi power cuts / plug pulls.
    if (::fdatasync(fd) != 0) {
        util::Logger::Warn("TelemetryCache", "fdatasync failed: " + std::string(std::strerror(errno)));
    }

    ::close(fd);
    return true;
}

std::vector<CachedRecord> TelemetryCache::readAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CachedRecord> records;

    std::ifstream stream(filePath_);
    if (!stream.is_open()) {
        return records;
    }

    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            json record = json::parse(line);
            CachedRecord item;
            item.messageId = record.value("message_id", "");
            item.sequenceId = record.value("seq_id", 0ULL);
            item.topic = record.value("topic", "");
            item.payload = record.value("payload", "");
            item.timestampMs = record.value("timestamp_ms", 0ULL);
            records.push_back(std::move(item));
        } catch (...) {
            // Ignore malformed line
        }
    }

    return records;
}

bool TelemetryCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    int fd = ::open(filePath_.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd < 0) {
        return false;
    }
    ::fdatasync(fd);
    ::close(fd);
    return true;
}

std::size_t TelemetryCache::count() {
    return readAll().size();
}

} // namespace sse::network
