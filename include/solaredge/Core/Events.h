#pragma once

#include <string>

#include <tinyfsm.hpp>

namespace sse::events {

enum class DebrisType { None, Dust, Leaf, BirdDropping, Unknown };

// Sector-sequence bookkeeping (currentSectorIndex vs totalSectors) is kept
// in the FSM's shared Context, not here — real hardware/vision drivers in
// later phases won't know the mission-level sector plan either, so keeping
// it out of the event payloads keeps this contract stable across all phases.

struct EvStart : tinyfsm::Event {};

struct EvDebrisDetected : tinyfsm::Event {
    int sectorId = 0;
    DebrisType debrisType = DebrisType::Unknown;
    float confidence = 0.0f;
};

struct EvSectorClear : tinyfsm::Event {
    int sectorId = 0;
};

struct EvCleanComplete : tinyfsm::Event {
    int sectorId = 0;
};

struct EvEmergencyStop : tinyfsm::Event {
    std::string reason;
};

struct EvEmergencyCleared : tinyfsm::Event {};

struct EvNetworkLost : tinyfsm::Event {};
struct EvNetworkRestored : tinyfsm::Event {};

struct EvBootHardwareReady : tinyfsm::Event {};
struct EvBootNetworkReady : tinyfsm::Event {};
struct EvBootVisionReady : tinyfsm::Event {};

struct EvBootHardwareFailed : tinyfsm::Event {
    std::string reason;
};

struct EvBootVisionFailed : tinyfsm::Event {
    std::string reason;
};

} // namespace sse::events
