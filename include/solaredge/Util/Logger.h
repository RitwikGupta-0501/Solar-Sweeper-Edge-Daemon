#pragma once

#include <string>

namespace sse::util {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

// Minimal leveled stdout logger. Deliberately dependency-free: Phase 1 runs
// interactively, not yet as a systemd service, so there is no journald to
// hand structured/rotating logs off to (that arrives with Phase 2's service
// unit). Thread-safe: multiple mock/consumer threads log concurrently.
class Logger {
public:
    static void SetLevel(LogLevel level);
    static void Log(LogLevel level, const std::string& tag, const std::string& message);

    static void Trace(const std::string& tag, const std::string& message);
    static void Debug(const std::string& tag, const std::string& message);
    static void Info(const std::string& tag, const std::string& message);
    static void Warn(const std::string& tag, const std::string& message);
    static void Error(const std::string& tag, const std::string& message);
};

LogLevel ParseLogLevel(const std::string& text);

} // namespace sse::util
