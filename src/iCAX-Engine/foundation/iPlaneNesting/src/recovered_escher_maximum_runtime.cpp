#include "recovered_escher_maximum_runtime.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t partialSub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
double partialUnsignedDouble(uint64_t value){return value<=INT64_MAX?double(value):double((value>>1)|(value&1))*2.;}
std::vector<EscherRecordGeometry> partialGeometries(std::span<const EscherPatternRecord> records,
    const EscherPreparedSinglePattern &pattern){
    std::vector<EscherRecordGeometry> result;result.reserve(records.size());
    for(const auto &record:records){const auto found=std::find_if(pattern.descriptor.records.begin(),pattern.descriptor.records.end(),
        [&](const auto &source){return source.geometry==record.geometry;});
        require(found!=pattern.descriptor.records.end(),PN2S_INVALID,"Escher partial record has no prepared geometry");
        result.push_back(pattern.geometries[size_t(found-pattern.descriptor.records.begin())]);}
    return result;
}
}
int64_t escherContourArea(const ConvolutionIntegerPolygon &contours){
    uint64_t result=0;for(const auto &ring:contours)result+=uint64_t(convolutionIntegerArea(ring));
    return std::bit_cast<int64_t>(result);
}
int64_t escherHierarchyArea(const ConvolutionHierarchy &hierarchy){
    return escherContourArea(escherHierarchyOuterContours(hierarchy));
}
EscherPreparedAreaMetadata escherOwnerAreaMetadata(const EscherIntegerShapeRecord &owner){
    const auto &selected=owner.alternate?*owner.alternate:owner.normal;
    return {escherContourArea(selected.outerContours()),escherHierarchyArea(owner.normal.hierarchy())};
}
EscherPreparedSinglePattern escherBindPreparedPattern(EscherEnumerationDescriptor descriptor){
    EscherPreparedSinglePattern result;result.descriptor=std::move(descriptor);
    for(const auto &record:result.descriptor.records){
        require(record.geometry!=0,PN2S_INVALID,"Prepared Escher record requires its actual shape owner");
        const auto &owner=*reinterpret_cast<const EscherIntegerShapeRecord *>(record.geometry);
        result.geometries.push_back(escherOwnerRecordGeometry(owner));
        if(!result.areas)result.areas=escherOwnerAreaMetadata(owner);
    }
    return result;
}
EscherPreparedSinglePattern escherPreparePeriodicPattern(EscherEnumerationDescriptor descriptor,
    std::vector<EscherRecordGeometry> geometries,std::span<const EscherSupportTable> supports,
    const ConvolutionIntegerPolygon &selfNfp,const EscherCandidateBuildOptions &options,int64_t &minimumCellArea){
    require(descriptor.records.size()==geometries.size()&&supports.size()==geometries.size(),
        PN2S_INVALID,"Prepared Escher descriptor/support geometry mismatch");
    auto candidates=escherBuildPeriodicCandidates(supports,selfNfp,options,minimumCellArea);
    descriptor.normalCandidates=std::move(candidates.ordinary);descriptor.specialCandidates=std::move(candidates.special);
    return {std::move(descriptor),std::move(geometries)};
}
EscherPatternLayout escherPreparedSingleMaximum(const EscherPreparedSinglePattern &pattern,
    EscherIntegerBox rectangle,bool special,uint64_t previousBest,uint64_t limit){
    const auto &descriptor=pattern.descriptor;
    return escherSingleMaximumPattern(descriptor,rectangle,special,previousBest,limit,pattern.geometries,
        [&](bool same,EscherIntegerBox box,bool catalog,EscherIntegerPoint axis,uint64_t previous,uint64_t maximum){
            const auto &candidates=catalog?descriptor.specialCandidates:descriptor.normalCandidates;
            return same?escherEqualMaximumPattern(descriptor.records,candidates,box,catalog,axis,previous,maximum,
                pattern.geometries,descriptor.restrictionDegrees):
                escherGeneralMaximumPattern(descriptor.records,candidates,box,catalog,axis,previous,maximum,
                pattern.geometries,descriptor.restrictionDegrees);
        });
}
EscherPatternLayout EscherPreparedMaximumHost::singleMaximum(size_t index,EscherIntegerBox rectangle,
    bool mode,uint64_t previousBest,uint64_t limit){
    return escherPreparedSingleMaximum(singles_[index],rectangle,mode,previousBest,limit);
}
EscherPatternLayout EscherPreparedMaximumHost::pairLayout(size_t index,EscherIntegerBox rectangle){
    const auto &pattern=pairs_[index];return escherLatticeLayout(pattern.records,pattern.candidates,rectangle);
}
EscherPatternLayout escherPreparedMaximum(std::span<const EscherPreparedSinglePattern> singles,
    std::span<const EscherPreparedLatticePattern> pairs,EscherIntegerBox rectangle,bool singleMode,
    bool primitivesOnly,uint64_t limit){
    EscherPreparedMaximumHost host(singles,pairs);return escherMaximumPattern(host,rectangle,singleMode,primitivesOnly,limit);
}
EscherPatternLayout escherPreparedSinglePartial(const EscherPreparedSinglePattern &pattern,EscherIntegerBox rectangle){
    require(pattern.areas.has_value(),PN2S_UNSUPPORTED,"Escher partial requires actual contour and hierarchy areas");
    const auto &descriptor=pattern.descriptor;
    auto result=escherPartialPattern(descriptor.records,descriptor.specialCandidates,rectangle,
        pattern.areas->outerArea,pattern.geometries,descriptor.restrictionDegrees);
    if(descriptor.restrictionDegrees!=0){
        const auto secondary=partialGeometries(result.secondary,pattern);
        bool accepted=escherLayoutRotationAccept(result.secondary,descriptor.restrictionDegrees,secondary);
        if(accepted){const auto primary=partialGeometries(result.primary,pattern);
            accepted=escherLayoutRotationAccept(result.primary,descriptor.restrictionDegrees,primary);}
        if(!accepted){result.primary.clear();result.secondary.clear();result.basis={};}
    }
    return result;
}
EscherPatternLayout escherPreparedPartial(std::span<const EscherPreparedSinglePattern> patterns,
    EscherIntegerBox rectangle,bool primitivesOnly,uint64_t limit){
    EscherPatternLayout result;double bestDensity=0;
    for(const auto &pattern:patterns){
        if(primitivesOnly&&pattern.descriptor.records.size()!=1)continue;
        auto trial=escherPreparedSinglePartial(pattern,rectangle);double density=0;
        if(!trial.primary.empty()){
            const auto bounds=escherLayoutBounds(trial.primary);
            const auto area=std::bit_cast<int64_t>(uint64_t(partialSub(bounds[2],bounds[0]))*uint64_t(partialSub(bounds[3],bounds[1])));
            density=partialUnsignedDouble(uint64_t(trial.primary.size())*uint64_t(pattern.areas->materialArea))/double(area);
        }
        if(density>bestDensity){bestDensity=density;result=std::move(trial);}
        if(result.primary.size()>=limit)break;
    }
    return result;
}
EscherPatternLayout EscherPreparedSolveHost::single(size_t index,EscherIntegerBox const &rectangle,
    bool mode,uint64_t required,std::array<int64_t,3> &key){
    const auto &pattern=singles_[index];return escherEnumerateSinglePattern(pattern.descriptor,rectangle,
        mode,required,key,pattern.geometries);
}
void EscherPreparedSolveHost::improvePair(size_t index,int64_t &length,EscherIntegerBox &rectangle,
    int32_t required,EscherPatternLayout &best){
    const auto &pattern=pairs_[index];improveEscherPatternLength(length,rectangle,required,best,pairLimit_,
        [&](const EscherIntegerBox &box,uint32_t limit){return escherLatticeLayout(pattern.records,
            pattern.candidates,box,std::bit_cast<int32_t>(limit));});
}
EscherPatternLayout escherPreparedSolve(std::span<const EscherPreparedSinglePattern> singles,
    std::span<const EscherPreparedLatticePattern> pairs,uint32_t pairLimit,EscherIntegerBox rectangle,
    bool singleMode,bool primitivesOnly,uint64_t required,int64_t &length){
    EscherPreparedSolveHost host(singles,pairs,pairLimit);return solveEscherPatterns(host,rectangle,singleMode,
        primitivesOnly,required,length);
}
}
