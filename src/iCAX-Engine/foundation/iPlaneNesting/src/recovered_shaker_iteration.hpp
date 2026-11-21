#pragma once
#include "recovered_shaker_finalize.hpp"
#include <cstring>
namespace study::recovered {
// The constructor copies all 56 policy bytes. Optional payloads are meaningful
// only when their corresponding presence flags are set.
struct ShakerPolicy {
    std::array<uint8_t,56> bytes{};
    template<class T> T get(size_t offset) const {T value;std::memcpy(&value,bytes.data()+offset,sizeof value);return value;}
    template<class T> void put(size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof value);}
};
struct ShakerModelState {
    ShakerPolicy policy;
    ShakerProblem problem;
    ShakerReportState report;
    ShakerFinalizeState finalize;
    double toleranceLimit=0,broadRadius=0,solveSeconds=0,querySeconds=0;
};
// 7247b0. C++ owns vectors/maps/solver lifetime; reset is the constructor's
// mandatory solver call. The 6000 option limits simplex and IPM iterations
// (6e6040), as independently verified against the original option setters.
ShakerModelState shakerCreateModel(const ShakerPolicy &,double window,double angularWindow,
    bool rotation,double broadRadius,const std::function<void()> &resetSolver);
struct ShakerSavedPose {ShakerPoint position{};double angle=0;};
struct ShakerIterationState {
    uint8_t policy0=0,policy1=0;
    int32_t iterations=0;
    std::array<double,3> seconds{};
    double objective=0;
    bool success=false,stopped=false;
    std::vector<ShakerSavedPose> saved;
};
struct ShakerIterationOptions {
    int32_t maximumIterations=1,stallLimit=1;
    double seconds=-1,improvementThreshold=0,translationWindow=0,rotationWindow=0,angularWindow=0;
    bool rotate=false,retryStatus2=false;
};
struct ShakerIterationRequest {
    ShakerPolicy policy;
    double window=0,angularWindow=0,broadRadius=0;
    bool rotation=false;
};
struct ShakerIterationResult {int32_t status=0;double objective=0;std::array<double,3> seconds{};};
struct ShakerIterationRuntime {
    std::function<void()> initializeEnvironment;
    std::function<int64_t()> clockNanoseconds;
    std::function<double()> sceneSpan;
    // Creates a fresh model, solves it, and returns its status/objective/times.
    // finishStep releases the model AFTER visible scene counters are updated.
    std::function<ShakerIterationResult(const ShakerIterationRequest &,std::optional<double>,double)> solve;
    std::function<void()> finishStep;
    std::function<int32_t(double,double)> checkPlacement;
    std::function<void(size_t)> refreshPart;
    // Must clear the convolution cache under the host's real lock; failures
    // propagate, never become a successful optimization step.
    std::function<void()> clearConvolutionCache;
    std::function<int32_t(double)> callback;
    std::function<void(double,const std::array<double,3> &,int32_t)> statistics;
};
// 719920. Returns status independently of state.success: later unsuccessful
// attempts may leave a valid saved solution and the initial success status.
int32_t shakerIterate(std::span<ShakerPart>,ShakerIterationState &,
    const ShakerIterationOptions &,const ShakerIterationRuntime &);
}
