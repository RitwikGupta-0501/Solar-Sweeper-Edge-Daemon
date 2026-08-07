#pragma once

#include "solaredge/Core/Events.h"

namespace sse::iface {

struct InferenceResult {
    events::DebrisType debrisType = events::DebrisType::None;
    float confidence = 0.0f;
    bool sectorClear = true;
};

// Boundary between the FSM and the AI pipeline. Phase 1 talks only to
// MockVisionEngine (canned results); Phase 4's OnnxVisionEngine implements
// the same shape against a real camera + ONNX model. The FSM only ever
// sees InferenceResult / the events derived from it, never a cv::Mat or
// model-specific type — that's what lets the model change without the FSM
// changing.
class IVisionEngine {
public:
    virtual ~IVisionEngine() = default;

    // Async: begins loading/warming up the model. Result is delivered later
    // via EvBootVisionReady or EvBootVisionFailed on the shared queue.
    virtual void initialize() = 0;

    // Async: begins inference for the given sector. Result is delivered
    // later via EvDebrisDetected or EvSectorClear on the shared queue.
    virtual void requestInference(int sectorId) = 0;

    virtual void shutdown() = 0;
};

} // namespace sse::iface
