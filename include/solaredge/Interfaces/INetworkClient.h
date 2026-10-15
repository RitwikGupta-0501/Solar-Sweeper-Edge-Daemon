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

    // Fire-and-forget telemetry publish methods
    virtual void publishStatus(const std::string& state, int sector) = 0;
    virtual void publishDetection(int sector, const std::string& type, float conf, float cameraMs, float infMs) = 0;
    virtual void publishHealth(float cpuTemp, int ramMb) = 0;
    virtual void publishCycleSummary(float duration, float pumpUptime, int sectors, int bird, int dust, int leaf, float avgInf) = 0;
    virtual void publishAlert(const std::string& severity, const std::string& code, const std::string& reason) = 0;

    virtual bool isConnected() const = 0;

    virtual void shutdown() = 0;
};

} // namespace sse::iface
