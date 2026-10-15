#include "solaredge/Network/TelemetrySpooler.h"
#include "solaredge/Util/Logger.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace sse::network {

TelemetrySpooler::TelemetrySpooler(std::string spoolFilePath, std::size_t maxSizeBytes)
    : spoolFilePath_(std::move(spoolFilePath)), maxSizeBytes_(maxSizeBytes) {
    ensureDirectoryExists();
}

void TelemetrySpooler::ensureDirectoryExists() {
    std::error_code ec;
    std::filesystem::path p(spoolFilePath_);
    if (p.has_parent_path() && !std::filesystem::exists(p.parent_path(), ec)) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
}

void TelemetrySpooler::spool(const std::string& topic, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureDirectoryExists();

    nlohmann::json record = {
        {"topic", topic},
        {"payload", payload}
    };
    std::string serialized = record.dump() + "\n";

    std::error_code ec;
    if (std::filesystem::exists(spoolFilePath_, ec)) {
        auto currentSize = std::filesystem::file_size(spoolFilePath_, ec);
        if (!ec && (currentSize + serialized.length() > maxSizeBytes_)) {
            util::Logger::Warn("TelemetrySpooler", "Spool file would exceed maximum size (" +
                                                       std::to_string(currentSize) + " bytes); dropping message");
            return;
        }
    }

    std::ofstream out(spoolFilePath_, std::ios::app);
    if (!out.is_open()) {
        util::Logger::Error("TelemetrySpooler", "Failed to open spool file for appending: " + spoolFilePath_);
        return;
    }

    out << serialized;
    out.flush();
}

bool TelemetrySpooler::hasPending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    return std::filesystem::exists(spoolFilePath_, ec) && std::filesystem::file_size(spoolFilePath_, ec) > 0;
}

std::vector<SpooledMessage> TelemetrySpooler::peek(std::size_t maxBatch) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SpooledMessage> batch;
    std::error_code ec;
    if (!std::filesystem::exists(spoolFilePath_, ec)) {
        return batch;
    }

    std::ifstream in(spoolFilePath_);
    if (!in.is_open()) {
        return batch;
    }

    std::string line;
    while (batch.size() < maxBatch && std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            batch.push_back({j.value("topic", ""), j.value("payload", "")});
        } catch (const std::exception& e) {
            util::Logger::Warn("TelemetrySpooler", "Corrupt line in spool file discarded: " + std::string(e.what()));
        }
    }

    return batch;
}

void TelemetrySpooler::commitDrained(std::size_t count) {
    if (count == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);

    std::error_code ec;
    if (!std::filesystem::exists(spoolFilePath_, ec)) {
        return;
    }

    std::ifstream in(spoolFilePath_);
    if (!in.is_open()) {
        return;
    }

    std::vector<std::string> remainingLines;
    std::string line;
    std::size_t skipped = 0;
    while (std::getline(in, line)) {
        if (skipped < count) {
            skipped++;
        } else {
            remainingLines.push_back(line);
        }
    }
    in.close();

    if (remainingLines.empty()) {
        std::filesystem::remove(spoolFilePath_, ec);
    } else {
        std::string tmpPath = spoolFilePath_ + ".tmp";
        std::ofstream out(tmpPath, std::ios::trunc);
        for (const auto& l : remainingLines) {
            out << l << "\n";
        }
        out.close();
        std::filesystem::rename(tmpPath, spoolFilePath_, ec);
    }
}

void TelemetrySpooler::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    std::filesystem::remove(spoolFilePath_, ec);
}

} // namespace sse::network
