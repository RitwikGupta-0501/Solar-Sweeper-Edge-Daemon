#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/ShutdownFlag.h"

namespace sse::core {

std::atomic<bool> g_estop_flag{false};
std::atomic<bool> g_shutdownRequested{false};

} // namespace sse::core
