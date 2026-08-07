#include "solaredge/Util/Logger.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace sse::util {

namespace {

std::atomic<LogLevel> g_currentLevel{LogLevel::Info};
std::mutex g_writeMutex;

const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::string TimestampNow() {
    const auto now = std::chrono::system_clock::now();
    const auto nowTimeT = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tmBuf{};
    localtime_r(&nowTimeT, &tmBuf);

    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace

void Logger::SetLevel(LogLevel level) {
    g_currentLevel.store(level, std::memory_order_relaxed);
}

void Logger::Log(LogLevel level, const std::string& tag, const std::string& message) {
    if (level < g_currentLevel.load(std::memory_order_relaxed)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_writeMutex);
    std::cout << '[' << TimestampNow() << "] [" << LevelName(level) << "] [" << tag << "] " << message
               << std::endl;
}

void Logger::Trace(const std::string& tag, const std::string& message) { Log(LogLevel::Trace, tag, message); }
void Logger::Debug(const std::string& tag, const std::string& message) { Log(LogLevel::Debug, tag, message); }
void Logger::Info(const std::string& tag, const std::string& message) { Log(LogLevel::Info, tag, message); }
void Logger::Warn(const std::string& tag, const std::string& message) { Log(LogLevel::Warn, tag, message); }
void Logger::Error(const std::string& tag, const std::string& message) { Log(LogLevel::Error, tag, message); }

LogLevel ParseLogLevel(const std::string& text) {
    if (text == "trace") return LogLevel::Trace;
    if (text == "debug") return LogLevel::Debug;
    if (text == "info") return LogLevel::Info;
    if (text == "warn") return LogLevel::Warn;
    if (text == "error") return LogLevel::Error;
    throw std::invalid_argument("Unknown log level: " + text);
}

} // namespace sse::util
