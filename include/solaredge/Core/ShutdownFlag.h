#pragma once

#include <atomic>

namespace sse::core {

// Set by the SIGINT/SIGTERM handlers in main.cpp. The handlers themselves
// must stay async-signal-safe (no logging, no allocation), so this is the
// only thing they touch; Application::run() polls it once per event-loop
// iteration to decide when to begin a graceful shutdown.
extern std::atomic<bool> g_shutdownRequested;

} // namespace sse::core
