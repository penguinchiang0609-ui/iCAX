#include "recovered_multiheads.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t nativeRound64(double value){value+=value<0?-.5:.5;return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
}
std::vector<MultiheadsPitchInterval> multiheadsPitchGaps(std::span<const MultiheadsPitchInterval> intervals){
    require(!intervals.empty(),PN2S_INVALID,"Multiheads pitch complement requires a nonempty native interval list");
    std::vector<MultiheadsPitchInterval> result;for(size_t n=1;n<intervals.size();++n)result.push_back({intervals[n-1][1],intervals[n][0]});
    const auto last=intervals.back()[1];result.push_back({last,last+last});return result;
}
std::vector<MultiheadsPitchInterval> multiheadsClipPitchIntervals(std::span<const MultiheadsPitchInterval> source,MultiheadsPitchInterval limit){
    std::vector<MultiheadsPitchInterval> result;for(const auto &interval:source){if(limit[0]>interval[1])continue;
        if(interval[0]>limit[1])return result;
        const double first=interval[0]>limit[0]?interval[0]:limit[0];
        const double last=limit[1]>interval[1]?interval[1]:limit[1];result.push_back({first,last});}
    return result;
}
std::vector<std::array<int64_t,4>> multiheadsIntegerPitchSegments(std::array<int64_t,2> direction,std::span<const MultiheadsPitchInterval> intervals){
    const double x=double(direction[0]),y=double(direction[1]),norm=std::sqrt(x*x+y*y);std::vector<std::array<int64_t,4>> result;
    for(const auto &interval:intervals){const double lower=interval[0]/norm,upper=interval[1]/norm;
        result.push_back({nativeRound64(lower*x),nativeRound64(lower*y),nativeRound64(upper*x),nativeRound64(upper*y)});}
    return result;
}
MultiheadsClusterKey multiheadsChooseClusterPitch(int32_t heads,std::span<const MultiheadsPitchInterval> input,bool randomized,Random &random){
    require(!input.empty(),PN2S_INVALID,"Multiheads pitch selection requires a nonempty interval list");
    std::vector<MultiheadsPitchInterval> intervals(input.begin(),input.end());for(auto &interval:intervals){const auto margin=(interval[1]-interval[0])*.00001;interval[0]=margin+interval[0];interval[1]-=margin;}
    const auto mode=random.integer(0,7)*int32_t(randomized);double pitch;
    if(mode<4)pitch=intervals.front()[0];else if(mode!=7)pitch=intervals.back()[1];
    else{const auto unit=random.unit();pitch=(intervals.back()[1]-intervals.front()[0])*(unit+0.)+intervals.front()[0];bool found=false;
        for(const auto &interval:intervals)if((interval[0]<=pitch&&pitch<=interval[1])||pitch<=interval[0]){found=true;break;}
        if(!found)pitch=intervals.front()[0];}
    return !(pitch<0)?MultiheadsClusterKey{heads,pitch}:MultiheadsClusterKey{};
}
MultiheadsClusterGeometry multiheadsClusterGeometry(MultiheadsClusterKey key,uintptr_t part,PlacementOrientation orientation,Box bounds,int32_t &nextIdentity){
    MultiheadsClusterGeometry result;result.identity=nextIdentity;nextIdentity=int32_t(uint32_t(nextIdentity)-1);
    for(int32_t n=0;n<key.heads;++n){const double y=double(n)*key.pitch,scaled=y*1000.;
        const double actualY=double(truncateInt32(scaled<0?scaled-.5:scaled+.5))/1000.;
        result.members.push_back({part,orientation,{0,actualY},{bounds.x0,bounds.y0+actualY,bounds.x1,bounds.y1+actualY},-1});}
    return result;
}
MultiheadsClusterKey multiheadsChooseClusterConfig(std::span<const MultiheadsClusterPitchSource> sources,int32_t available,
    const MultiheadsParameters &parameters,double height,bool randomized,Random &random){
    struct Choice{std::vector<MultiheadsPitchInterval> intervals;double shapeHeight=0;int32_t heads=0;};
    std::vector<Choice> choices;
    for(const auto &source:sources){auto intervals=multiheadsClipPitchIntervals(source.intervals,{parameters.minimumGap,parameters.maximumGap});if(intervals.empty())continue;
        const auto fit=truncateInt32(((height-source.shapeHeight)+intervals.front()[0])/intervals.front()[0]);
        int32_t heads=std::min(std::min(available,fit),parameters.heads);
        if(parameters.fixedDivisions>1){if(parameters.fixedDivisions>heads)return {};heads=parameters.fixedDivisions;}
        intervals=multiheadsClipPitchIntervals(intervals,{0,(height-source.shapeHeight)/double(heads-1)});
        if(!intervals.empty())choices.push_back({std::move(intervals),source.shapeHeight,heads});
    }
    if(choices.empty())return {};
    const auto mode=random.integer(0,2);
    const auto key=[](const Choice &choice){return MultiheadsClusterKey{choice.heads,-(double(choice.heads-1)*choice.intervals.front()[0]+double(choice.heads)*choice.shapeHeight)};};
    if(mode==0){auto best=key(choices.front());for(size_t n=1;n<choices.size();++n){const auto next=key(choices[n]);if(multiheadsClusterLess(best,next))best=next;}
        std::erase_if(choices,[&](const auto &choice){const auto candidate=key(choice);return candidate.heads!=best.heads||candidate.pitch!=best.pitch;});}
    else if(mode==1){int32_t maximum=choices.front().heads;for(const auto &choice:choices)maximum=std::max(maximum,choice.heads);
        std::erase_if(choices,[&](const auto &choice){return choice.heads!=maximum;});}
    const auto &selected=choices[size_t(random.integer(0,int32_t(choices.size()-1)))];
    return multiheadsChooseClusterPitch(selected.heads,selected.intervals,randomized,random);
}
uint32_t runMultiheadsGreedy(MultiheadsGreedyHost &host){
    uint32_t total=0;int32_t previous=-1,next;
    do{next=host.prepareNext();if(next>0)host.commitPrepared();total+=uint32_t(next);
        const auto bucket=std::bit_cast<int32_t>(total)/5;if(previous<bucket){if(host.remainingTime()<=0)return total;
            previous=bucket;if(host.stopRequested())return total;}
    }while(next!=0);
    return total;
}
int32_t prepareMultiheadsGreedyNext(const MultiheadsGreedyNextInput &input,MultiheadsGreedyNextHost &host){
    host.clearPrepared();host.clearCandidateCache();MultiheadsSelection selection;
    if(!input.predictionDisabledC0){const auto cached=host.cachedCandidate();if(cached.placement){selection=cached;host.clearCachedDetail(*selection.placement);selection.kind=0;}}
    host.select(selection);host.setMetadata(selection.metadata);
    if(!selection.placement)return 0;
    if(selection.kind!=1){std::array<MultiheadsPlacement,1> one{selection.placement};host.notifySingle(one[0]);host.appendPrepared(one);return 1;}
    auto members=host.materialize(*selection.placement);host.notifyCompound(selection.placement,members);
    if(!selection.placement){host.nullSelectionDiagnostic();members.clear();}
    else if((!input.hasPreNest||input.preNestCompatible)&&!host.acceptCompound(members))members.clear();
    host.appendPrepared(members);return int32_t(uint32_t(members.size()));
}
std::vector<int32_t> multiheadsModuleCounts(std::span<const MultiheadsModuleDescriptor> modules){
    std::set<int32_t> values;for(const auto &module:modules)values.insert(module.memberCount);return {values.begin(),values.end()};
}
std::optional<size_t> multiheadsFindModule(std::span<const MultiheadsModuleDescriptor> modules,uintptr_t geometry){
    for(size_t n=0;n<modules.size();++n)if(modules[n].geometry==geometry)return n;return {};
}
std::vector<size_t> multiheadsEligibleModules(std::span<const MultiheadsModuleDescriptor> modules,size_t groupCount,
    std::span<const size_t> activeGroups,std::span<const int32_t> placed,int32_t count){
    require(placed.size()>=groupCount,PN2S_INVALID,"Multiheads module demand table is incomplete");
    std::vector<bool> active(groupCount);for(const auto group:activeGroups){require(group<groupCount,PN2S_INVALID,"Multiheads active group index is invalid");active[group]=true;}
    std::vector<size_t> result;for(size_t n=0;n<modules.size();++n){const auto &module=modules[n];if(module.memberCount!=count)continue;
        require(module.catalogGroup<groupCount,PN2S_INVALID,"Multiheads module group index is invalid");if(!active[module.catalogGroup])continue;
        const auto available=int32_t(uint32_t(module.auxiliary?0:module.quantity)-uint32_t(placed[module.catalogGroup]));
        if(count<=available)result.push_back(n);}return result;
}
void evaluateMultiheadsCount(int32_t count,MultiheadsSelection &selection,MultiheadsCountEvaluationHost &host){
    host.prepareCount(count);const auto candidates=host.produceCandidates();if(candidates.empty())return;
    host.cacheCandidates(candidates);auto candidate=host.cachedCandidate();if(!candidate.placement)return;
    host.clearCachedDetail(*candidate.placement);candidate.kind=1;
    const double proposed=std::bit_cast<double>(candidate.metadata),current=std::bit_cast<double>(selection.metadata);
    if(!selection.placement||(!(std::abs(proposed-current)<1e-9)&&current>proposed))selection=candidate;
}
void selectMultiheadsModuleCandidate(std::span<const MultiheadsModuleDescriptor> modules,MultiheadsSelection &selection,MultiheadsCountEvaluationHost &host){
    for(const auto count:multiheadsModuleCounts(modules))evaluateMultiheadsCount(count,selection,host);
}
bool multiheadsAdmitMembers(std::span<const size_t> members,MultiheadsMemberAdmissionHost &host){
    bool accepted=true;size_t attempted=0;for(const auto member:members){accepted=host.state(member)==0;
        if(accepted){host.insertTemporary(member);accepted=host.validateTemporary(member);}++attempted;if(!accepted)break;}
    while(attempted)host.removeTemporary(members[--attempted]);return accepted;
}
} // namespace study::recovered
