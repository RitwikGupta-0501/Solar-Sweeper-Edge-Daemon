#include "solaredge/Core/States.h"

#include <variant>

namespace sse::core {

EdgeStateMachine::Context EdgeStateMachine::ctx{};

void EdgeStateMachine::SetContext(iface::IHardwareDriver* hardware, iface::INetworkClient* network,
                                   iface::IVisionEngine* vision, EventQueue* queue, int totalSectors,
                                   int bootMaxRetries) {
    ctx = Context{};
    ctx.hardware = hardware;
    ctx.network = network;
    ctx.vision = vision;
    ctx.queue = queue;
    ctx.totalSectors = totalSectors;
    ctx.bootMaxRetries = bootMaxRetries;
}

void EdgeStateMachine::ResetContextForTesting() {
    ctx = Context{};
}

void EdgeStateMachine::AdvanceSectorOrFinish() {
    ctx.currentSectorIndex++;
    if (ctx.currentSectorIndex >= ctx.totalSectors) {
        float totalDuration = 0.0f;
        if (ctx.cycleStartTime != std::chrono::steady_clock::time_point{}) {
            totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - ctx.cycleStartTime).count() / 1000.0f;
        }
        float avgInf = 0.0f;
        if (!ctx.inferenceTimesMs.empty()) {
            float sum = 0.0f;
            for (float t : ctx.inferenceTimesMs) {
                sum += t;
            }
            avgInf = sum / static_cast<float>(ctx.inferenceTimesMs.size());
        }
        if (ctx.network) {
            ctx.network->publishCycleSummary(totalDuration, ctx.pumpUptimeSeconds, ctx.totalSectors,
                                             ctx.birdDroppingCount, ctx.dustCount, ctx.leafCount, avgInf);
        }

        if (ctx.networkAvailable) {
            transit<Idle>();
        } else {
            transit<Offline_Idle>();
        }
    } else {
        transit<Inspection>();
    }
}

void DispatchToFsm(const EventVariant& event) {
    std::visit([](auto&& concreteEvent) { EdgeStateMachine::dispatch(concreteEvent); }, event);
}

} // namespace sse::core

FSM_INITIAL_STATE(sse::core::EdgeStateMachine, sse::core::Boot_Init)
