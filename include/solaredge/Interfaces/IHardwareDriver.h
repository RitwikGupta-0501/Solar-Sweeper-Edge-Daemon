#pragma once

namespace sse::iface {

// Boundary between the FSM and the actuator layer. Phase 1 talks only to
// MockHardwareDriver; Phase 2's SerialHardwareDriver implements the exact
// same shape against the real Arduino byte protocol, so the FSM/event loop
// never change when the real driver lands.
class IHardwareDriver {
public:
    virtual ~IHardwareDriver() = default;

    // Async: begins/re-begins the boot self-test. Result is delivered later
    // via EvBootHardwareReady or EvBootHardwareFailed on the shared queue.
    virtual void requestSelfTest() = 0;

    virtual void beginDryClean(int sectorId) = 0;
    virtual void beginWetClean(int sectorId) = 0;

    // Idempotent: safe to call repeatedly, including when already stopped.
    virtual void stopActuators() = 0;

    // Symbolic in Phase 1 — the real halt already happened via the atomic
    // E-Stop flag before this is ever called. Real drivers may use this to
    // release/park hardware explicitly.
    virtual void emergencyHalt() = 0;

    // Joins any internal worker thread. Must be safe to call from the
    // destructor path.
    virtual void shutdown() = 0;
};

} // namespace sse::iface
