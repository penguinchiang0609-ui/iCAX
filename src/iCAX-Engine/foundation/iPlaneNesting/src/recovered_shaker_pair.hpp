#pragma once
#include "recovered_shaker_geometry.hpp"
namespace study::recovered {
struct ShakerPairOptions {
    double radius=0,toleranceLimit=1e-6,sceneSpan=1;
    double admissionTolerance=0;
    bool hasAdmissionTolerance=false,overrideWindow=false;
    bool geometryModeEnabled=true,hasGeometryMode=true;
    int32_t geometryMode=0;
    uint8_t policy18=0,policy21=0;
    std::array<ShakerPoint,2> specialReference90{};
};
struct ShakerPairSource {
    // Explicit geometry boundary: 71a730 pair eligibility and 7217e0 sourced
    // convolution are not replaced with a coordinate-only Boolean NFP.
    std::function<bool(bool rotation)> excluded;
    std::function<std::vector<ShakerBoundary>(double angularExpansion,int32_t mode)> convolution;
};
struct ShakerPairResult {
    int32_t status=0; // 2: admitted-tolerance overlap with no active boundary
    ShakerCell cell;
    bool inside=false;
    size_t retries=0;
};
// Entire 7273e0 cell/row path, including recursion. The host supplies the two
// explicitly named geometry dependencies above. No full SDK parity claim.
ShakerPairResult shakerPairRows(ShakerProblem &,LinearModel &,ShakerPart &,ShakerPart &,
    ShakerPairOptions,const ShakerPairSource &,double &maximumViolation);
}
