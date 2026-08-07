#pragma once

#include <string>

#include <tinyfsm.hpp>

#include "solaredge/Core/EstopFlag.h"
#include "solaredge/Core/EventQueue.h"
#include "solaredge/Core/Events.h"
#include "solaredge/Interfaces/IHardwareDriver.h"
#include "solaredge/Interfaces/INetworkClient.h"
#include "solaredge/Interfaces/IVisionEngine.h"
#include "solaredge/Util/Logger.h"

namespace sse::core {

// Pops one event out of an EventVariant and dispatches it to whichever
// concrete event type it actually holds. Declared here (not EventQueue.h)
// so EventQueue.h doesn't need to know about EdgeStateMachine at all.
void DispatchToFsm(const EventVariant& event);

// TinyFSM state instances are stateless singletons, so anything that needs
// to persist across states (which interfaces to call, which sector we're
// on, whether the network is up) lives in this shared static Context
// rather than as member data on any one state.
class EdgeStateMachine : public tinyfsm::Fsm<EdgeStateMachine> {
public:
    struct Context {
        iface::IHardwareDriver* hardware = nullptr;
        iface::INetworkClient* network = nullptr;
        iface::IVisionEngine* vision = nullptr;
        EventQueue* queue = nullptr;

        int totalSectors = 1;
        int currentSectorIndex = 0;
        bool networkAvailable = true;

        bool hardwareReady = false;
        bool networkReady = false;
        bool visionReady = false;
        int bootRetryCount = 0;
        int bootMaxRetries = 3;

        // Reason string for the most recent EvEmergencyStop / boot failure,
        // read by Error/Hardware_Fault::entry() for logging. Events can't
        // carry data directly into transit<>(), so it's threaded through
        // here instead.
        std::string lastFaultReason;
    };

    static Context ctx;

    static void SetContext(iface::IHardwareDriver* hardware, iface::INetworkClient* network,
                            iface::IVisionEngine* vision, EventQueue* queue, int totalSectors,
                            int bootMaxRetries);

    // Resets all shared static state. TinyFSM states are process-global
    // singletons, so Catch2 fixtures must call this (and re-enter Boot_Init
    // via start()) at the top of every FSM test to avoid state bleeding
    // between test cases.
    static void ResetContextForTesting();

    virtual void entry() {}
    virtual void exit() {}

    virtual void react(events::EvStart const&) {}
    virtual void react(events::EvDebrisDetected const&) {}
    virtual void react(events::EvSectorClear const&) {}
    virtual void react(events::EvCleanComplete const&) {}
    virtual void react(events::EvEmergencyStop const&) {}
    virtual void react(events::EvEmergencyCleared const&) {}
    virtual void react(events::EvNetworkLost const&) {}
    virtual void react(events::EvNetworkRestored const&) {}
    virtual void react(events::EvBootHardwareReady const&) {}
    virtual void react(events::EvBootNetworkReady const&) {}
    virtual void react(events::EvBootVisionReady const&) {}
    virtual void react(events::EvBootHardwareFailed const&) {}
    virtual void react(events::EvBootVisionFailed const&) {}

protected:
    // Shared by Inspection::react(EvSectorClear) and
    // CleaningStateBase::react(EvCleanComplete): advances to the next
    // sector, or finishes the pass, picking Idle vs Offline_Idle based on
    // *current* network availability -- re-checked here rather than
    // latched earlier, so a network drop-then-restore mid-cycle still
    // lands in Idle. Defined in StateMachine.cpp.
    void AdvanceSectorOrFinish();
};

// Forward-declared so any state below can transit<>() to any other state
// regardless of textual definition order.
class Boot_Init;
class Idle;
class Offline_Idle;
class Inspection;
class DryClean;
class WetClean;
class Error;
class Hardware_Fault;

// Shared react(EvStart)/react(EvEmergencyStop) for Idle and Offline_Idle --
// both idle-family states start a cleaning pass and fault out identically.
class IdleStateBase : public EdgeStateMachine {
public:
    void react(events::EvStart const&) override {
        if (IsEstopActive()) {
            util::Logger::Warn("FSM", "discarded EvStart -- estop active");
            return;
        }
        ctx.currentSectorIndex = 0;
        transit<Inspection>();
    }

    void react(events::EvEmergencyStop const& event) override {
        ctx.lastFaultReason = event.reason;
        transit<Error>();
    }

protected:
    void SafePark() {
        if (ctx.hardware) {
            ctx.hardware->stopActuators();
        }
    }
};

// Shared behavior for DryClean/WetClean: a network drop never aborts an
// active mechanical cycle (Edge Daemon Details use case 4); an E-Stop
// always wins; a completion event is re-checked against the atomic flag
// before being acted on, which is what makes a stale EvCleanComplete that
// raced a just-latched E-Stop safe to discard.
class CleaningStateBase : public EdgeStateMachine {
public:
    void exit() override {
        if (ctx.hardware) {
            ctx.hardware->stopActuators();
        }
    }

    void react(events::EvNetworkLost const&) override {
        ctx.networkAvailable = false;
    }

    void react(events::EvEmergencyStop const& event) override {
        ctx.lastFaultReason = event.reason;
        if (ctx.hardware) {
            ctx.hardware->emergencyHalt();
        }
        transit<Error>();
    }

    void react(events::EvCleanComplete const&) override {
        if (IsEstopActive()) {
            util::Logger::Warn("FSM", "discarded EvCleanComplete -- estop active");
            return;
        }
        AdvanceSectorOrFinish();
    }
};

class Boot_Init : public EdgeStateMachine {
public:
    void entry() override {
        util::Logger::Info("FSM", "Boot_Init: running self-test");
        ctx.hardwareReady = false;
        ctx.networkReady = false;
        ctx.visionReady = false;
        ctx.bootRetryCount = 0;
        // Phase-1-only reset policy: a real limit-switch-latched E-Stop
        // should NOT be clearable by a plain FSM restart once Phase 2 adds
        // real hardware -- revisit this line then.
        g_estop_flag.store(false, std::memory_order_release);

        if (ctx.hardware) ctx.hardware->requestSelfTest();
        if (ctx.network) ctx.network->initialize();
        if (ctx.vision) ctx.vision->initialize();
    }

    void react(events::EvBootHardwareReady const&) override {
        ctx.hardwareReady = true;
        CheckAllReady();
    }

    void react(events::EvBootNetworkReady const&) override {
        ctx.networkReady = true;
        CheckAllReady();
    }

    void react(events::EvBootVisionReady const&) override {
        ctx.visionReady = true;
        CheckAllReady();
    }

    void react(events::EvBootHardwareFailed const& event) override {
        HandleBootFailure(event.reason);
    }

    void react(events::EvBootVisionFailed const& event) override {
        HandleBootFailure(event.reason);
    }

private:
    // Network is treated as best-effort at boot: once hardware and vision
    // (the two safety/decision-critical subsystems) are ready, boot
    // proceeds immediately into Idle or Offline_Idle depending on whether
    // network happened to already be up -- it does not block waiting on
    // network specifically, since Offline_Idle already exists to handle
    // "network isn't here yet."
    void CheckAllReady() {
        if (!ctx.hardwareReady || !ctx.visionReady) {
            return;
        }
        if (ctx.networkReady) {
            transit<Idle>();
        } else {
            transit<Offline_Idle>();
        }
    }

    // Vision is treated as equally safety-load-bearing as hardware: without
    // it the FSM cannot safely choose Dry vs Wet clean at all, so a vision
    // self-test failure escalates to Hardware_Fault exactly like a hardware
    // failure once retries are exhausted.
    void HandleBootFailure(const std::string& reason) {
        util::Logger::Warn("FSM", "Boot self-test failed: " + reason);
        ctx.lastFaultReason = reason;
        ctx.bootRetryCount++;
        if (ctx.bootRetryCount > ctx.bootMaxRetries) {
            util::Logger::Error("FSM", "Boot retries exhausted -- entering Hardware_Fault");
            transit<Hardware_Fault>();
            return;
        }
        util::Logger::Info("FSM", "Retrying self-test (" + std::to_string(ctx.bootRetryCount) + "/" +
                                       std::to_string(ctx.bootMaxRetries) + ")");
        if (ctx.hardware) ctx.hardware->requestSelfTest();
    }
};

class Idle : public IdleStateBase {
public:
    void entry() override {
        util::Logger::Info("FSM", "Idle: awaiting start");
        SafePark();
    }

    void react(events::EvNetworkLost const&) override {
        ctx.networkAvailable = false;
        transit<Offline_Idle>();
    }
};

class Offline_Idle : public IdleStateBase {
public:
    void entry() override {
        util::Logger::Warn("FSM", "Offline_Idle: degraded mode, telemetry caching only");
        SafePark();
    }

    void react(events::EvNetworkRestored const&) override {
        ctx.networkAvailable = true;
        transit<Idle>();
    }
};

class Inspection : public EdgeStateMachine {
public:
    void entry() override {
        util::Logger::Info("FSM", "Inspection: sector " + std::to_string(ctx.currentSectorIndex));
        if (ctx.vision) {
            ctx.vision->requestInference(ctx.currentSectorIndex);
        }
    }

    void react(events::EvDebrisDetected const& event) override {
        if (IsEstopActive()) {
            util::Logger::Warn("FSM", "discarded EvDebrisDetected -- estop active");
            return;
        }
        if (event.debrisType == events::DebrisType::BirdDropping) {
            transit<WetClean>();
        } else {
            transit<DryClean>();
        }
    }

    void react(events::EvSectorClear const&) override {
        if (IsEstopActive()) {
            util::Logger::Warn("FSM", "discarded EvSectorClear -- estop active");
            return;
        }
        AdvanceSectorOrFinish();
    }

    void react(events::EvNetworkLost const&) override {
        // An in-flight inspection decision counts as an active operation --
        // don't abort it, just note the network is down.
        ctx.networkAvailable = false;
    }

    void react(events::EvEmergencyStop const& event) override {
        ctx.lastFaultReason = event.reason;
        transit<Error>();
    }
};

class DryClean : public CleaningStateBase {
public:
    void entry() override {
        util::Logger::Info("FSM", "DryClean: sector " + std::to_string(ctx.currentSectorIndex));
        if (ctx.hardware) {
            ctx.hardware->beginDryClean(ctx.currentSectorIndex);
        }
    }
};

class WetClean : public CleaningStateBase {
public:
    void entry() override {
        util::Logger::Info("FSM", "WetClean: sector " + std::to_string(ctx.currentSectorIndex));
        if (ctx.hardware) {
            ctx.hardware->beginWetClean(ctx.currentSectorIndex);
        }
    }
};

// EvEmergencyStop fired during/around runtime -- the robot was operational,
// then had to halt. Distinct from Hardware_Fault (see below): recovery
// (EvEmergencyCleared) forces a full re-entry into Boot_Init rather than
// jumping straight back to Idle, so a fresh self-test always runs before
// resuming service.
class Error : public EdgeStateMachine {
public:
    void entry() override {
        util::Logger::Error("FSM", "Error: emergency stop latched (" + ctx.lastFaultReason + ")");
        if (ctx.hardware) {
            ctx.hardware->emergencyHalt();
        }
    }

    void react(events::EvEmergencyCleared const&) override {
        transit<Boot_Init>();
    }
};

// Boot self-test never succeeded -- the robot never became operational.
// Deliberately unrecoverable within the running process for Phase 1 (no
// overrides at all, including EvEmergencyCleared): recovery is a process
// restart until Phase 2 defines a real repair signal.
class Hardware_Fault : public EdgeStateMachine {
public:
    void entry() override {
        util::Logger::Error("FSM", "Hardware_Fault: boot self-test failed permanently (" + ctx.lastFaultReason +
                                        ")");
    }
};

} // namespace sse::core
