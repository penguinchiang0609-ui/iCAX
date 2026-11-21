#pragma once
#include "recovered_shaker_iteration.hpp"
namespace study::recovered {
struct ShakerBoundaryConfiguration {
    // Original +1b0,+1b8,+1c0,+1c8; indexes may alias the same part.
    std::array<std::optional<size_t>,4> parts;
    double allowance=0; // +1a8, subtracted only by the two-boundary scale path
};
void shakerResetBoundaryObjectives(std::span<ShakerPart>,const ShakerBoundaryConfiguration &); // 71b160
int32_t shakerConfigureDefault(std::span<ShakerPart>,const ShakerBoundaryConfiguration &,int32_t direction); // 71b220
int32_t shakerConfigureCompaction(std::span<ShakerPart>,const ShakerBoundaryConfiguration &,int32_t first,int32_t second); // 71b3b0
int32_t shakerConfigureNeutral(std::span<ShakerPart>,const ShakerBoundaryConfiguration &); // 70b6c0
double shakerSceneScale(std::span<ShakerPart>,ShakerSceneGeometry &,const ShakerBoundaryConfiguration &); // 71b650
// These are writable globals in the original, not immutable geometric laws.
struct ShakerCommonSettings {double angularWindow=.01,rotationScale=.01,translationScale=1./30;};
struct ShakerCommonRuntime {
    std::function<double()> sceneScale,thresholdFactor;
    std::function<bool()> rotationEnabled;
    std::function<bool(const ShakerIterationOptions &)> iterate; // scene.success, not the status code
};
int32_t shakerCommonRun(double seconds,const ShakerCommonSettings &,const ShakerCommonRuntime &); // 71b760
}
