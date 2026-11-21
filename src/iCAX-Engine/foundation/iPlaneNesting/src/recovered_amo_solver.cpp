#include "recovered_amo_solver.hpp"
#include "recovered_amo_geometry.hpp"
namespace study::recovered {
namespace {
std::vector<AmoTilingCandidatePtr> solve(AmoTilingJob &job,const AmoTilingSolveRequest &request,AmoTilingMapRuntime &runtime,const std::array<std::vector<double>,2> &edges) {
    AmoTilingFamilyPreparation preparation{&runtime.maps,request.fixedTranslation,request.translation};
    const auto sources=amoTilingMemberSources(job);const std::array<uintptr_t,2> identities{uintptr_t(sources[0]),uintptr_t(sources[1])};
    const auto check=[&]{if(request.check)request.check();};
    const auto finalize=[&](const AmoTilingCandidatePtr &candidate){check();amoTilingFamilyRanges(request.criterion,identities,amoTilingMemberBounds(job,*candidate),*candidate);};
    AmoTilingSeedCallbacks seed;
    seed.periodY=[&](const AmoTilingCandidatePtr &candidate){check();return amoTilingPeriodY(runtime,job,*candidate);};
    seed.periodX=[&](const AmoTilingCandidatePtr &candidate){check();return amoTilingPeriodX(runtime,job,*candidate);};
    seed.finalize=finalize;
    AmoTilingCriterionCallbacks callbacks;
    callbacks.prepare=[&](){check();return amoTilingPrepareAngles(request.angles,edges,[&](double angle){check();return amoTilingPrepareFamilies(preparation,runtime.sources[0],runtime.sources[1],angle,runtime.geometry);});};
    callbacks.family.midpoint=amoTilingFamilyMidpoint;
    callbacks.family.evaluate=[&](const AmoTilingCandidatePtr &candidate){check();amoTilingFamilySeeds(seed,candidate);};
    callbacks.finalize=finalize;
    AmoTilingSearchState state;std::vector<AmoTilingCandidatePtr> result;
    amoTilingCriterionSearch(state,job,callbacks,result);return result;
}
std::array<std::vector<double>,2> edgeAngles(AmoTilingJob &job,const AmoTilingSolveRequest &request){const auto sources=amoTilingMemberSources(job);std::array<std::vector<double>,2> edges;if(request.angles.shapeEdges)for(size_t member=0;member<2;++member)edges[member]=amoTilingShapeEdgeAngles(*sources[member]);return edges;}
AmoTilingMapRuntime makeRuntime(AmoTilingJob &job,const AmoTilingSolveRequest &request,const AmoTilingMapGeometry &geometry){const auto sources=amoTilingMemberSources(job);AmoTilingMapRuntime runtime;for(size_t member=0;member<2;++member)runtime.sources[member]={sources[member],request.sourceFlags[member][0],request.sourceFlags[member][1]};runtime.geometry=geometry;return runtime;}
}
std::vector<AmoTilingCandidatePtr> amoTilingSolve(AmoTilingJob &job,const AmoTilingSolveRequest &request,const AmoTilingMapGeometry &geometry) {
    auto runtime=makeRuntime(job,request,geometry);return solve(job,request,runtime,edgeAngles(job,request));
}
std::vector<AmoTilingDescriptor> amoTilingProduceDescriptors(AmoTilingJob &job,AmoTilingSolveRequest request,int32_t required,const AmoTilingMapGeometry &geometry) {
    auto runtime=makeRuntime(job,request,geometry);const auto edges=edgeAngles(job,request);
    const auto sources=amoTilingMemberSources(job);std::vector<AmoTilingDescriptor> descriptors;const std::array<uintptr_t,2> identities{uintptr_t(sources[0]),uintptr_t(sources[1])};
    for(const auto quantity:{int32_t(0),required/2}) {
        request.criterion.firstDemand=request.criterion.secondDemand=quantity;request.criterion.partialWeight=0.;job.angleBounds.clear();
        const auto results=solve(job,request,runtime,edges);if(!results.empty())descriptors.push_back(amoTilingResultDescriptor(*results.front(),identities));
    }
    return descriptors;
}
std::vector<AmoTilingCandidatePtr> amoTilingSolve(AmoTilingJob &job,const AmoTilingSolveRequest &request){return amoTilingSolve(job,request,amoTilingFloatingMapGeometry());}
std::vector<AmoTilingDescriptor> amoTilingProduceDescriptors(AmoTilingJob &job,AmoTilingSolveRequest request,int32_t required){return amoTilingProduceDescriptors(job,request,required,amoTilingFloatingMapGeometry());}
}
