#include "ordinary_predefined.hpp"
#include "ordinary_unplaced.hpp"

namespace study {
namespace {
class PredefinedHost final : public recovered::PredefinedNesterHost {
    const Input &input;
    OrdinaryState &state;
    OrdinaryPredefinedTrace &trace;
    const OrdinaryPredefinedProgram &program;
    std::map<uintptr_t,std::vector<recovered::PredefinedConstraintRecord>> constraints;
    std::map<uintptr_t,std::pair<size_t,size_t>> identities;
    std::optional<int64_t> target(uintptr_t instance,bool x) {
        const auto found=constraints.find(instance);if(found==constraints.end()||found->second.empty())return {};
        const int64_t first=x?found->second.front().x:found->second.front().y;
        for(const auto &record:found->second)if(first!=(x?record.x:record.y)){++trace.conflicting;return {};}
        return first;
    }
    bool accepts(uintptr_t instance,uintptr_t variant) {
        const auto [group,ordinal]=identities.at(instance);const auto &pose=input.parts[group].poses.at(variant);
        const auto actual=recovered::quantizeSdkOrientation({int32_t(pose.flip),pose.angle,pose.angle},false);
        for(const auto &record:constraints.at(instance)) {
            const std::array<recovered::PlacementOrientation,1> required{{record.orientation}};
            if(!recovered::mandatoryOrientationContains(actual,required,false,false,true))return false;
        }
        return true;
    }
public:
    PredefinedHost(const Input &source,OrdinaryState &layout,const OrdinaryPredefinedProgram &settings,
                   OrdinaryPredefinedTrace &record):input(source),state(layout),trace(record),program(settings) {
        require(state.catalog&&state.placementHost,PN2S_INTERNAL,"Predefined instance catalog must be initialized");
        for(size_t group=0;group<input.parts.size();++group){const auto first=state.catalog->groups.at(group).at(0);
            for(size_t ordinal=0;ordinal<state.catalog->records[first].instances.size();++ordinal)
                identities.emplace(state.catalog->records[first].instances[ordinal],std::pair{group,ordinal});}
        for(const auto &entry:program.records){
            require(entry.group<input.parts.size()&&entry.ordinal<size_t(input.parts[entry.group].quantity),PN2S_INVALID,"Predefined concrete part identity");
            require(recovered::predefinedOrientationValid(entry.sdk.orientation),PN2S_INVALID,"Predefined SDK orientation range");
            require(std::isfinite(entry.sdk.position.x)&&std::isfinite(entry.sdk.position.y)&&std::abs(entry.sdk.position.x)<=1e6&&std::abs(entry.sdk.position.y)<=1e6,
                    PN2S_UNSUPPORTED,"Predefined ordinary position exceeds finite native coordinate domain");
            const auto first=state.catalog->groups.at(entry.group).at(0);
            const auto instance=state.catalog->records[first].instances.at(entry.ordinal);
            constraints[instance].push_back(recovered::predefinedConstraints(entry.sdk,program.sheetOrigin,program.moveDefault));
        }
    }
    std::vector<uintptr_t> instances(uintptr_t group)override {
        const auto first=state.catalog->groups.at(group).at(0);const auto &source=state.catalog->records[first].instances;
        return {source.begin(),source.end()};
    }
    int32_t difficulty(uintptr_t instance)override {
        ++trace.difficultyQueries;if(!constraints.contains(instance))return 0;
        recovered::PredefinedDifficulty value;value.cached={100,100,100};
        return recovered::predefinedDifficulty(value,{program.xWeight778,program.yWeight780,2.},false);
    }
    bool placed(uintptr_t instance)override {
        const bool value=state.catalog->instances.at(instance).placed;if(value)++trace.unavailable;return value;
    }
    std::vector<recovered::PredefinedVariant> variants(uintptr_t instance)override {
        ++trace.variantQueries;std::vector<recovered::PredefinedVariant> result;
        if(!constraints.contains(instance))return result;const auto [group,ordinal]=identities.at(instance);
        const auto &modules=state.placementHost->modules.at(group);
        for(size_t pose=0;pose<modules.size();++pose)if(accepts(instance,pose))result.push_back({pose,modules[pose].leftE0});
        return result;
    }
    std::optional<int64_t> solveX(uintptr_t instance,const recovered::PredefinedVariant &,bool,bool)override {++trace.xQueries;return target(instance,true);}
    std::optional<int32_t> solveY(uintptr_t instance,const recovered::PredefinedVariant &variant,bool,bool)override {
        ++trace.yQueries;const auto result=target(instance,false);if(!result)return {};
        if(!input.pointGroups.empty()){
            const auto x=target(instance,true);if(!x)return {};
            const auto [group,ordinal]=identities.at(instance);auto trial=state.output;
            trial.placements.push_back({group,ordinal,variant.identity,*x,int32_t(*result)});
            if(!validPointsProximity(input,trial,input.pointGroups))return {};
        }
        return int32_t(*result);
    }
    void commit(uintptr_t instance,const recovered::PredefinedVariant &variant,int64_t x,int32_t y,bool unaligned)override {
        const auto [group,ordinal]=identities.at(instance);const Placement placement{group,ordinal,variant.identity,x,y};
        ordinaryCommit(state,placement);
        require(state.placementHost->nesting.placements.back()->rounded2c==unaligned,PN2S_INTERNAL,"Predefined original grid alignment disagrees with lifecycle");
        trace.committed.push_back(placement);
    }
};
}
uint32_t ordinaryApplyPredefined(const Input &input,const OrdinaryPredefinedProgram &program,Budget &budget,
                                OrdinaryState &state,OrdinaryPredefinedTrace *record) {
    require(input.gap==0&&!requires_general(input),PN2S_UNSUPPORTED,"Predefined ordinary host requires finite grid-aligned single rings and zero spacing");
    OrdinaryControl initialize;initialize.placementLimit=0;for(const auto &part:input.parts)initialize.values.push_back(area(part.polygon));
    ordinaryRun(input,budget,state,&initialize);
    OrdinaryPredefinedTrace local;auto &trace=record?*record:local;
    PredefinedHost host(input,state,program,trace);std::vector<uintptr_t> parts(input.parts.size());std::iota(parts.begin(),parts.end(),uintptr_t(0));
    recovered::PredefinedNesterOptions options;options.minimumDifficulty=recovered::predefinedMinimumDifficulty(program.xWeight778,program.yWeight780);
    options.checkConstraints4e0=program.checkConstraints4e0;options.pitch17f0=state.output.pitchX;
    return recovered::runPredefinedNester(host,parts,options);
}
Output ordinaryPreNest(const Input &input,const OrdinaryPredefinedProgram &program,Budget &budget,OrdinaryPredefinedTrace *trace) {
    require(!program.records.empty(),PN2S_INVALID,"PreNest requires a positive number of parts");
    OrdinaryState state;ordinaryApplyPredefined(input,program,budget,state,trace);return std::move(state.output);
}
Output ordinaryNestMore(const Input &input,const OrdinaryPredefinedProgram &program,Budget &budget,
                        const ReverseRunner &continueLayout,OrdinaryPredefinedTrace *trace) {
    require(bool(continueLayout),PN2S_INVALID,"NestMore requires its concrete continuation strategy");
    OrdinaryState state;ordinaryApplyPredefined(input,program,budget,state,trace);continueLayout(input,budget,state);return std::move(state.output);
}
std::vector<OrdinaryPredefinedLock> ordinaryPredefinedLocks(const Output &output,const OrdinaryPredefinedProgram &program,bool moveDefault) {
    std::vector<OrdinaryPredefinedLock> result;
    for(size_t index=0;index<output.placements.size();++index){const auto &p=output.placements[index];bool predefined=false,fixed=false;
        for(const auto &record:program.records)if(record.group==p.group&&record.ordinal==p.ordinal){predefined=true;fixed|=!recovered::predefinedMovable(record.sdk.glueMode,moveDefault);}
        if(predefined)result.push_back({index,fixed});}
    return result;
}
}
