#include "recovered_escher_boundary.hpp"
#include <bit>
namespace study::recovered {
namespace {
bool exhausted(const EscherProviderLengthLimits &limits){return (uint32_t(limits.flags30)&255U)!=0&&limits.target<0.;}
int64_t difference(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t add32Margin(int64_t value){return std::bit_cast<int64_t>(uint64_t(value)+32U);}
}
EscherBoundaryPhaseResult escherPreparedBoundaryPattern(const EscherPreparedSinglePattern &pattern,
    const ConvolutionIntegerPolygon &boundary,bool special,uint32_t previousBest,uintptr_t settings,
    EscherProviderLengthLimits &limits,int32_t candidateIndex,const EscherBoundaryPhaseProvider &phase){
    if(pattern.descriptor.restrictionDegrees!=0||exhausted(limits))return {};
    require(bool(phase),PN2S_UNSUPPORTED,"Escher boundary phase requires its prepared geometry provider");
    // The original rotation list is exactly {0.0}; it does not enumerate angles.
    return phase(pattern,boundary,special,previousBest,0.,settings,limits,candidateIndex);
}
EscherProviderLengthResult escherPreparedLength(std::span<const EscherPreparedSinglePattern> patterns,
    const ConvolutionIntegerPolygon &boundary,bool special,bool primitivesOnly,
    const EscherProviderLengthLimits &inputLimits,int32_t previousAttempts,uintptr_t settings,
    const EscherBoundaryPhaseProvider &phase,const EscherLengthDiagnostic &diagnostic){
    require(!patterns.empty()&&patterns.size()<=INT32_MAX,PN2S_INVALID,"Escher length requires a nonempty descriptor catalog");
    require(!boundary.empty()&&!boundary.front().empty(),PN2S_INVALID,"Escher length requires a nonempty boundary");
    const int32_t descriptors=int32_t(patterns.size()),round=previousAttempts/descriptors;
    int32_t skip=previousAttempts%descriptors;EscherProviderLengthResult result;auto limits=inputLimits;
    for(const auto &pattern:patterns){
        if(skip>0){--skip;continue;}
        if(primitivesOnly&&pattern.descriptor.records.size()!=1)continue;
        if(exhausted(limits))break;
        auto candidate=escherPreparedBoundaryPattern(pattern,boundary,special,uint32_t(result.layout.primary.size()),settings,limits,round,phase);
        if(candidate.active){
            result.attempts=std::bit_cast<int32_t>(uint32_t(result.attempts)+1U);
            if(candidate.layout.primary.size()>result.layout.primary.size())result.layout=std::move(candidate.layout);
        }
    }
    if(diagnostic){
        EscherIntegerBox stock{boundary.front().front()[0],boundary.front().front()[1],boundary.front().front()[0],boundary.front().front()[1]};
        for(const auto &ring:boundary)for(const auto &point:ring)for(size_t axis=0;axis<2;++axis){stock[axis]=std::min(stock[axis],point[axis]);stock[axis+2]=std::max(stock[axis+2],point[axis]);}
        const auto &placed=result.layout.bounds;
        for(size_t axis=0;axis<2;++axis)if(difference(std::max(stock[axis+2],placed[axis+2]),std::min(stock[axis],placed[axis]))>add32Margin(difference(stock[axis+2],stock[axis])))diagnostic(9+int32_t(axis));
    }
    return result;
}
EscherProviderLengthResult EscherPreparedProviderHost::length(const ConvolutionIntegerPolygon &boundary,
    bool mode,bool primitive,const EscherProviderLengthLimits &limits,int32_t attempts){
    return escherPreparedLength(singles_,boundary,mode,primitive,limits,attempts,boundarySettings_,boundaryPhase_,diagnostic_);
}
EscherPatternLayout EscherPreparedProviderHost::maximum(EscherIntegerBox rectangle,bool mode,bool primitive,uint64_t limit){
    return escherPreparedMaximum(singles_,pairs_,rectangle,mode,primitive,limit);
}
EscherPatternLayout EscherPreparedProviderHost::partial(EscherIntegerBox rectangle,bool primitive,uint64_t limit){
    return escherPreparedPartial(singles_,rectangle,primitive,limit);
}
EscherPatternLayout EscherPreparedProviderHost::solve(EscherIntegerBox rectangle,bool mode,bool primitive,uint64_t required,int64_t &length){
    return escherPreparedSolve(singles_,pairs_,pairLimit_,rectangle,mode,primitive,required,length);
}
}
