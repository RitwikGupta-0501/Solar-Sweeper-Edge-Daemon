#include "solaredge/Network/TelemetryCache.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

#include "solaredge/Util/Logger.h"

namespace sse::network {

using nlohmann::json;

TelemetryCache::TelemetryCache(std::string cacheFilePath)
    : filePath_(std::move(cacheFilePath)) {}

bool TelemetryCache::append(const std::string& topic, const std::string& jsonPayload) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream stream(filePath_, std::ios::app);
    if (!stream.is_open()) {
        util::Logger::Warn("TelemetryCache", "Failed to open cache file for writing: " + filePath_);
        return false;
    }

    uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    json record;
    record["topic"] = topic;
    record["payload"] = jsonPayload;
    record["timestamp_ms"] = nowMs;

    stream << record.dump() << "\n";
    stream.flush();
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
    std::ofstream stream(filePath_, std::ios::trunc);
    return stream.is_open();
}

std::size_t TelemetryCache::count() {
    return readAll().size();
}

} // namespace sse::network
