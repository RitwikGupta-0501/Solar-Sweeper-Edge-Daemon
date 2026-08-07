#pragma once

#include <string>

namespace sse::iface {

// Boundary between the FSM/Application and the cloud link. Phase 1 talks
// only to MockNetworkClient; Phase 3's PahoNetworkClient implements the
// same shape against the real MQTT broker and topic contract.
class INetworkClient {
public:
    virtual ~INetworkClient() = default;

    // Async: begins connecting. Result is delivered later via
    // EvBootNetworkReady on the shared queue (or never, if configured to
    // start offline — exercises Boot_Init's offline-boot path).
    virtual void initialize() = 0;

    // Fire-and-forget telemetry publish. Phase 1's mock only logs it.
    virtual void publishTelemetry(const std::string& topic, const std::string& jsonPayload) = 0;

    virtual bool isConnected() const = 0;

    virtual void shutdown() = 0;
};

} // namespace sse::iface
