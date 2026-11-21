#pragma once
#include "recovered_escher_maximum_runtime.hpp"
namespace study::recovered {
struct EscherBoundaryPhaseResult {
    EscherPatternLayout layout;
    bool active=false;
    bool operator==(const EscherBoundaryPhaseResult &)const=default;
};
using EscherBoundaryPhaseProvider=std::function<EscherBoundaryPhaseResult(
    const EscherPreparedSinglePattern &,const ConvolutionIntegerPolygon &,bool specialCatalog,
    uint32_t previousBest,double rotationDegrees,uintptr_t settings,EscherProviderLengthLimits &,
    int32_t candidateIndex)>; //7ac840
using EscherLengthDiagnostic=std::function<void(int32_t)>;
EscherBoundaryPhaseResult escherPreparedBoundaryPattern(const EscherPreparedSinglePattern &,
    const ConvolutionIntegerPolygon &,bool specialCatalog,uint32_t previousBest,uintptr_t settings,
    EscherProviderLengthLimits &,int32_t candidateIndex,const EscherBoundaryPhaseProvider &); //7a2bb0
EscherProviderLengthResult escherPreparedLength(std::span<const EscherPreparedSinglePattern>,
    const ConvolutionIntegerPolygon &,bool specialCatalog,bool primitivesOnly,
    const EscherProviderLengthLimits &,int32_t previousAttempts,uintptr_t settings,
    const EscherBoundaryPhaseProvider &,const EscherLengthDiagnostic & = {}); //788970
class EscherPreparedProviderHost final:public EscherProviderHost {
    std::span<const EscherPreparedSinglePattern> singles_;
    std::span<const EscherPreparedLatticePattern> pairs_;
    uint32_t pairLimit_;
    uintptr_t boundarySettings_;
    EscherBoundaryPhaseProvider boundaryPhase_;
    EscherLengthDiagnostic diagnostic_;
public:
    EscherPreparedProviderHost(std::span<const EscherPreparedSinglePattern> singles,
        std::span<const EscherPreparedLatticePattern> pairs={},uint32_t pairLimit=UINT32_MAX,
        uintptr_t boundarySettings=0,EscherBoundaryPhaseProvider boundaryPhase={},EscherLengthDiagnostic diagnostic={})
        :singles_(singles),pairs_(pairs),pairLimit_(pairLimit),boundarySettings_(boundarySettings),
        boundaryPhase_(std::move(boundaryPhase)),diagnostic_(std::move(diagnostic)){}
    EscherProviderLengthResult length(const ConvolutionIntegerPolygon &,bool,bool,
        const EscherProviderLengthLimits &,int32_t)override;
    EscherPatternLayout maximum(EscherIntegerBox,bool,bool,uint64_t)override;
    EscherPatternLayout partial(EscherIntegerBox,bool,uint64_t)override;
    EscherPatternLayout solve(EscherIntegerBox,bool,bool,uint64_t,int64_t &)override;
};
}
