#pragma once

#include "solaredge/Vision/VisionTypes.h"

namespace sse::vision {

class SpatialCoordinateMapper {
public:
    SpatialCoordinateMapper(int cameraWidth = 1920, int cameraHeight = 1080,
                            float panelHeightM = 1.60f, float lookAheadM = 0.20f,
                            int totalNozzles = 4, float gantrySpeedMps = 0.15f);

    // Inverts network bounding box (letterbox padded) to native camera image coordinates
    BoundingBox invertLetterbox(const BoundingBox& netBox, const PreprocessingParams& prep) const;

    // Converts network detection bounding box to physical panel millimeter offsets and nozzle ID
    Detection mapToPhysical(const BoundingBox& netBox, int classId, float confidence,
                            const PreprocessingParams& prep) const;

    // Calculates which nozzle (0 to totalNozzles-1) covers the given transverse vertical coordinate
    int selectNozzle(float physicalYmm) const;

    // Calculates actuation delay (in milliseconds) from longitudinal offset and velocity
    int calculateActuationDelayMs(float physicalXmm) const;

    // Accessors
    int cameraWidth() const { return cameraWidth_; }
    int cameraHeight() const { return cameraHeight_; }
    float panelHeightM() const { return panelHeightM_; }
    float lookAheadM() const { return lookAheadM_; }
    int totalNozzles() const { return totalNozzles_; }
    float gantrySpeedMps() const { return gantrySpeedMps_; }

private:
    int cameraWidth_;
    int cameraHeight_;
    float panelHeightM_;
    float lookAheadM_;
    int totalNozzles_;
    float gantrySpeedMps_;
};

} // namespace sse::vision
