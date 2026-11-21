#pragma once
#include "recovered_shaker_input.hpp"
#include "recovered_shaker_constraints.hpp"
namespace study::recovered {
struct ShakerRunResult {
    int32_t status=0;
    ShakerIterationState iteration;
    size_t solves=0,pairs=0;
    size_t rotationSolves=0,cacheClears=0;
    double convolutionSeconds=0;
    size_t constraintRows=0;
};
// Vendor-free host for normalized straight-component scenes. The input scene
// owns its geometry, relations, objectives and mobility. This does not create
// the SDK normalization/technology constraints or substitute a PN2 dispatch.
ShakerRunResult runShakerFixedScene(ShakerInputScene &,const ShakerIterationOptions &);
ShakerRunResult compactShakerFixedScene(ShakerInputScene &,int32_t first,int32_t second,double seconds,int32_t geometryMode=0);
// The caller supplies mobility and normalized geometry. Rotation executes the
// recovered angular convolution, row generation, acceptance and cache lifecycle.
// Mode -1 selects the recovered default of polygon mode 1 for translation and
// source-edge mode 0 for rotation. Modes 0, 1 and 2 explicitly fix the backend.
ShakerRunResult runShakerScene(ShakerInputScene &,const ShakerIterationOptions &,int32_t geometryMode=0);
ShakerRunResult runShakerScene(ShakerInputScene &,const ShakerIterationOptions &,int32_t geometryMode,
                             std::span<const ShakerPairConstraint>);
ShakerRunResult compactShakerFixedScene(ShakerInputScene &,int32_t first,int32_t second,double seconds,
                                      int32_t geometryMode,std::span<const ShakerPairConstraint>);
ShakerRunResult shakeShakerScene(ShakerInputScene &,int32_t direction,bool rotate,double seconds,int32_t geometryMode=0);
ShakerRunResult shakeShakerScene(ShakerInputScene &,int32_t direction,bool rotate,double seconds,
    int32_t geometryMode,std::span<const ShakerPairConstraint>);
}
