#pragma once
#include "recovered_shaker_geometry.hpp"
#include <optional>
namespace study::recovered {
struct ShakerStepReport {
    int32_t status=0;
    std::optional<std::array<uint64_t,2>> special;
    std::optional<double> slack,objective;
    std::optional<ShakerPoint> firstDirection,secondDirection;
    std::optional<bool> constrained;
};
struct ShakerReportState {
    double objective=0;
    std::array<std::optional<int32_t>,2> directionalRows;
    double buildSeconds=0;
    int32_t solverOption1B8=0; // simplex and IPM iteration limit, 6e6040
};
struct ShakerSolutionQueries {
    std::function<std::vector<double>()> primal,dual;
};
// 72a840 reports LP state/directional limits. It does not perform nonlinear
// placement acceptance (71a8e0 is a different function).
ShakerStepReport shakerStepReport(const ShakerProblem &,const ShakerReportState &,
    ShakerStepReport &,const ShakerSolutionQueries &);
struct ShakerStepRuntime {
    std::function<void()> prepareScene,resetSolver;
    std::function<void(int32_t)> solverOption;
    std::function<void(double)> solverTolerance;
    std::function<int64_t()> clockNanoseconds;
    std::function<double()> variables;
    std::function<int32_t(std::optional<double>,double,double &)> buildRows;
    std::function<int32_t()> columns,rows;
    std::function<int32_t(double,double &)> optimize;
    ShakerSolutionQueries solution;
};
// 72a4c0. Runtime boundaries name the real scene, model, solve and query
// dependencies; this controller does not substitute a made-up scene host.
ShakerStepReport shakerSolveStep(const ShakerProblem &,ShakerReportState &,
    std::optional<double> admission,double seconds,double &initialObjective,
    double &solvedObjective,double &maximumViolation,const ShakerStepRuntime &);
using ShakerPairIndices=std::array<size_t,2>;
struct ShakerBuildRuntime {
    std::function<int64_t()> clockNanoseconds;
    std::function<std::vector<ShakerPairIndices>(double)> broadphase;
    std::vector<std::function<void(bool)>> constraints;
    std::function<double()> sceneSpan;
    std::function<int32_t(ShakerPairIndices,int32_t,std::optional<double>,double,double &,double)> pairRows;
    std::function<void()> finalize;
};
// 728e50. Constraint objects and 725a30 are required host dependencies;
// clearing, traversal, timing and early-exit semantics belong to this function.
int32_t shakerBuildStep(ShakerProblem &,std::span<ShakerPart>,double broadRadius,
    std::optional<double> admission,double seconds,double &maximumViolation,const ShakerBuildRuntime &);
struct ShakerCheckItem {size_t part=0;uintptr_t geometry=0;};
struct ShakerConvolutionPose {
    uintptr_t geometry=0;
    double angle=0;
    std::array<uint8_t,2> mirror{};
    int32_t side=0;
};
struct ShakerConvolutionKey {
    std::array<ShakerConvolutionPose,2> poses;
    double expansion=0;
};
struct ShakerCheckRuntime {
    std::function<void()> prepareScene;
    std::function<std::vector<ShakerPairIndices>(double)> broadphase;
    std::function<int64_t()> clockNanoseconds;
    std::function<bool(bool,ShakerPairIndices)> excluded;
    std::function<std::vector<ShakerBoundary>(const ShakerConvolutionKey &,int32_t)> convolution;
    bool geometryModeEnabled=true,hasGeometryMode=false;
    int32_t geometryMode=0;
};
// 71a8e0: 0=overlap, 1=accepted, 2=time exhausted. A strict time comparison
// differs deliberately from the inclusive comparison in 728e50.
int32_t shakerCheckPlacement(std::span<const ShakerPart>,std::span<const ShakerCheckItem>,
    double seconds,double tolerance,const ShakerCheckRuntime &);
}
