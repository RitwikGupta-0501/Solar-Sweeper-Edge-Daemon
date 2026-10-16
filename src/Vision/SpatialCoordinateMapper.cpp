#include "solaredge/Vision/SpatialCoordinateMapper.h"

#include <algorithm>
#include <cmath>

namespace sse::vision {

SpatialCoordinateMapper::SpatialCoordinateMapper(int cameraWidth, int cameraHeight,
                                                 float panelHeightM, float lookAheadM,
                                                 int totalNozzles, float gantrySpeedMps)
    : cameraWidth_(cameraWidth > 0 ? cameraWidth : 1920),
      cameraHeight_(cameraHeight > 0 ? cameraHeight : 1080),
      panelHeightM_(panelHeightM > 0.0f ? panelHeightM : 1.60f),
      lookAheadM_(lookAheadM > 0.0f ? lookAheadM : 0.20f),
      totalNozzles_(totalNozzles > 0 ? totalNozzles : 4),
      gantrySpeedMps_(gantrySpeedMps > 0.0f ? gantrySpeedMps : 0.15f) {}

BoundingBox SpatialCoordinateMapper::invertLetterbox(const BoundingBox& netBox,
                                                     const PreprocessingParams& prep) const {
    if (prep.scale <= 0.0f) {
        return netBox;
    }

    float x1 = (netBox.x1 - prep.padX) / prep.scale;
    float y1 = (netBox.y1 - prep.padY) / prep.scale;
    float x2 = (netBox.x2 - prep.padX) / prep.scale;
    float y2 = (netBox.y2 - prep.padY) / prep.scale;

    // Boundary clamping to camera sensor dimensions
    x1 = std::max(0.0f, std::min(x1, static_cast<float>(cameraWidth_ - 1)));
    y1 = std::max(0.0f, std::min(y1, static_cast<float>(cameraHeight_ - 1)));
    x2 = std::max(0.0f, std::min(x2, static_cast<float>(cameraWidth_ - 1)));
    y2 = std::max(0.0f, std::min(y2, static_cast<float>(cameraHeight_ - 1)));

    return BoundingBox{x1, y1, x2, y2};
}

Detection SpatialCoordinateMapper::mapToPhysical(const BoundingBox& netBox,
                                                 int classId,
                                                 float confidence,
                                                 const PreprocessingParams& prep) const {
    Detection det;
    det.classId = classId;
    det.confidence = confidence;

    switch (classId) {
        case 0: det.debrisType = events::DebrisType::Dust; break;
        case 1: det.debrisType = events::DebrisType::Leaf; break;
        case 2: det.debrisType = events::DebrisType::BirdDropping; break;
        default: det.debrisType = events::DebrisType::Unknown; break;
    }

    det.box = invertLetterbox(netBox, prep);

    float centerX = (det.box.x1 + det.box.x2) * 0.5f;
    float centerY = (det.box.y1 + det.box.y2) * 0.5f;

    // Transverse Y coordinate across panel height (in mm)
    float panelHeightMm = panelHeightM_ * 1000.0f;
    det.physicalYmm = (centerX / static_cast<float>(cameraWidth_)) * panelHeightMm;

    // Longitudinal X coordinate along look-ahead axis (in mm)
    // 0 = at leading bumper / trailing tool plane; lookAheadM_ * 1000 = optical horizon
    float lookAheadMm = lookAheadM_ * 1000.0f;
    float ratioX = 1.0f - (centerY / static_cast<float>(cameraHeight_));
    det.physicalXmm = std::max(0.0f, std::min(ratioX * lookAheadMm, lookAheadMm));

    det.targetNozzleId = selectNozzle(det.physicalYmm);

    return det;
}

int SpatialCoordinateMapper::selectNozzle(float physicalYmm) const {
    if (totalNozzles_ <= 1) {
        return 0;
    }

    float panelHeightMm = panelHeightM_ * 1000.0f;
    float zoneHeight = panelHeightMm / static_cast<float>(totalNozzles_);
    if (zoneHeight <= 0.0f) {
        return 0;
    }

    int nozzleId = static_cast<int>(physicalYmm / zoneHeight);
    if (nozzleId < 0) {
        nozzleId = 0;
    } else if (nozzleId >= totalNozzles_) {
        nozzleId = totalNozzles_ - 1;
    }
    return nozzleId;
}

int SpatialCoordinateMapper::calculateActuationDelayMs(float physicalXmm) const {
    if (gantrySpeedMps_ <= 0.0f) {
        return 0;
    }

    float lookAheadMm = lookAheadM_ * 1000.0f;
    float remainingMm = lookAheadMm - physicalXmm;
    if (remainingMm <= 0.0f) {
        return 0;
    }

    float speedMmPerSec = gantrySpeedMps_ * 1000.0f;
    float delaySec = remainingMm / speedMmPerSec;
    return static_cast<int>(std::round(delaySec * 1000.0f));
}

} // namespace sse::vision
