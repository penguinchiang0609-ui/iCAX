#include "ordinary_unplaced.hpp"
#include "ordinary_pair_map.hpp"

namespace study {
namespace {
using ModuleKey=std::pair<size_t,size_t>;
ModuleKey key(const ProximityEndpoint &endpoint){return {endpoint.group,endpoint.ordinal};}
Point placedPoint(const Input &input,const ProximityEndpoint &endpoint,const Placement &placement){
    const auto &part=input.parts.at(endpoint.group);auto pose=part.poses.at(placement.poseIndex);
    if(std::isfinite(placement.solvedAngle))pose.angle=placement.solvedAngle;
    auto point=transform(Polygon{endpoint.point},part.pivot,pose).front();
    point.x+=std::isfinite(placement.exactX)?placement.exactX:double(placement.x)/1e6;
    point.y+=std::isfinite(placement.exactY)?placement.exactY:double(placement.y)/1000.;
    return point;
}
const Placement *findPlacement(const OrdinaryState &state,ModuleKey module){
    for(const auto &p:state.output.placements)if(ModuleKey{p.group,p.ordinal}==module)return &p;return nullptr;
}
void validateGroups(const Input &input,std::span<const PointsProximityGroup> groups){
    for(const auto &group:groups){
        require(group.axis==ProximityAxis::X||group.axis==ProximityAxis::Y,PN2S_INVALID,"Point-proximity axis");
        require(std::isfinite(group.tolerance)&&group.tolerance>=0.,PN2S_INVALID,"Point-proximity tolerance");
        require(key(group.endpoints[0])!=key(group.endpoints[1]),PN2S_INVALID,"Point-proximity requires two concrete modules");
        for(const auto &endpoint:group.endpoints)
            require(endpoint.group<input.parts.size()&&input.parts[endpoint.group].quantity>0&&endpoint.ordinal<size_t(input.parts[endpoint.group].quantity)&&
                    std::isfinite(endpoint.point.x)&&std::isfinite(endpoint.point.y),PN2S_INVALID,"Point-proximity module or point");
    }
}
void constrainRows(const Input &input,const OrdinaryState &state,std::span<const PointsProximityGroup> groups,
                   size_t part,size_t pose,std::optional<size_t> ordinal,Rows &rows,int64_t pitch,int64_t ox,int64_t oy){
    if(!ordinal)return;
    for(const auto &group:groups)for(size_t side=0;side<2;++side){
        const auto &endpoint=group.endpoints[side];if(key(endpoint)!=ModuleKey{part,*ordinal})continue;
        const auto &anchor=group.endpoints[1-side];const auto placed=findPlacement(state,key(anchor));if(!placed)continue;
        const auto location=placedPoint(input,anchor,*placed);
        const auto &source=input.parts[part];const auto local=transform(Polygon{endpoint.point},source.pivot,source.poses.at(pose)).front();
        if(group.axis==ProximityAxis::X){
            const auto band=recovered::proximityTargetBand(recovered::proximityAnchorBand(location.x,group.tolerance,1e6),double(ox)/1e6,local.x,1e6);
            for(size_t strip=0;strip<rows.size();++strip){const auto coordinate=int64_t(strip)*pitch;if(coordinate<band[0]||coordinate>band[1])rows[strip].clear();}
        }else{
            const auto band=recovered::proximityTargetBand(recovered::proximityAnchorBand(location.y,group.tolerance,1000.),double(oy)/1000.,local.y,1000.);
            const Row allowed{{band[0],band[1]}};for(auto &row:rows)row=intersection(row,allowed);
        }
    }
}
class OrdinaryUnplacedHost final:public recovered::UnplacedPenaltyHost {
    const Input &input;const UnplacedProgram &program;Budget &budget;OrdinaryState &state;UnplacedTrace &trace;
    Placement candidate;double score;size_t baseline;
public:
    OrdinaryUnplacedHost(const Input &i,const UnplacedProgram &p,Budget &b,OrdinaryState &s,UnplacedTrace &t,Placement placement,double base)
        :input(i),program(p),budget(b),state(s),trace(t),candidate(placement),score(base),baseline(s.output.placements.size()){}
    double baseScore()override{return score;}
    void commitCandidate()override{ordinaryCommit(state,candidate);++trace.evaluations;}
    uint64_t blockedModules()override{
        std::map<ModuleKey,ProximityEndpoint> watched;
        for(const auto &group:program.groups)for(size_t side=0;side<2;++side)
            if(group.watched[side])watched.try_emplace(key(group.endpoints[side]),group.endpoints[side]);
        uint64_t count=0;
        for(const auto &[module,endpoint]:watched){
            if(findPlacement(state,module))continue;
            OrdinaryControl probeControl;probeControl.placementLimit=0;for(const auto &part:input.parts)probeControl.values.push_back(material_area(part));
            ReferenceProbe probe;probe.wantFirstPositions=true;probe.firstPositionOrdinals[module.first]=module.second;
            ordinaryRun(input,budget,state,&probeControl,&probe);++trace.moduleProbes;
            if(!probe.firstRightStrip.at(module.first)){++count;++trace.blocked;}
        }
        return count;
    }
    void restoreSnapshot()override{ordinaryRestore(state,baseline);++trace.restores;}
};
}
void filterPointsProximityRows(const Input &input,const OrdinaryState &state,size_t part,size_t pose,std::optional<size_t> ordinal,
                              Rows &rows,int64_t pitch,int64_t ox,int64_t oy){
    constrainRows(input,state,input.pointGroups,part,pose,ordinal,rows,pitch,ox,oy);
}
void validatePointsProximityInput(const Input &input){validateGroups(input,input.pointGroups);}
bool validPointsProximity(const Input &input,const Output &output,std::span<const PointsProximityGroup> groups){
    try{validateGroups(input,groups);}catch(const Failure &){return false;}
    for(const auto &group:groups){
        std::array<const Placement *,2> pair{};
        for(size_t side=0;side<2;++side)for(const auto &placement:output.placements)
            if(ModuleKey{placement.group,placement.ordinal}==key(group.endpoints[side])){if(pair[side])return false;pair[side]=&placement;}
        if(!pair[0]||!pair[1])continue;
        if(pair[0]->poseIndex>=input.parts[group.endpoints[0].group].poses.size()||
           pair[1]->poseIndex>=input.parts[group.endpoints[1].group].poses.size())return false;
        for(size_t side=0;side<2;++side){
            const auto &placement=*pair[side];const auto &endpoint=group.endpoints[side];const auto &part=input.parts[endpoint.group];
            if(placement.poseIndex>=part.poses.size())return false;auto pose=part.poses[placement.poseIndex];
            if(std::isfinite(placement.solvedAngle))pose.angle=placement.solvedAngle;
            const auto point=transform(Polygon{endpoint.point},part.pivot,pose).front();
            const auto box=bounds(transform(part.polygon,part.pivot,pose));const auto anchor=placedPoint(input,group.endpoints[1-side],*pair[1-side]);
            const double scale=group.axis==ProximityAxis::X?1e6:1000.;
            const int64_t origin=rounded((group.axis==ProximityAxis::X?box.x0:box.y0)*scale);
            const auto band=recovered::proximityTargetBand(recovered::proximityAnchorBand(group.axis==ProximityAxis::X?anchor.x:anchor.y,group.tolerance,scale),double(origin)/scale,group.axis==ProximityAxis::X?point.x:point.y,scale);
            const double exact=group.axis==ProximityAxis::X?placement.exactX:placement.exactY;
            const int64_t coordinate=(std::isfinite(exact)?rounded(exact*scale):(group.axis==ProximityAxis::X?placement.x:placement.y))+origin;
            if(coordinate<band[0]||coordinate>band[1])return false;
        }
    }
    return true;
}
UnplacedProgram unplacedProgram(const Input &input,std::span<const PointsProximityGroup> groups,
                               recovered::OffsetFactoryInput settings,const recovered::Random *initial){
    validateGroups(input,groups);double totalArea=0;int32_t population=0;const auto parts=ordinaryValueParts(input,totalArea,population);
    settings.population88=population;std::vector<recovered::OffsetSparsePart> sparseParts;
    for(const auto &part:parts)sparseParts.push_back({part.quantity,0,part.optional,part.area});
    recovered::OffsetSparseInput sparse;sparse.parts=sparseParts;sparse.sheetWidth=input.width;sparse.sheetHeight=input.height;sparse.specialSheet=settings.candidater.specialSheet;
    settings.candidater.sparseLayout=recovered::offsetSparseLayout(sparse);
    UnplacedProgram result;result.random=initial?*initial:recovered::Random(input.randomSeed);
    result.child=recovered::unplacedFactory(result.random,settings,parts,totalArea);result.groups.assign(groups.begin(),groups.end());return result;
}
int ordinaryUnplacedContinue(const Input &input,const UnplacedProgram &program,Budget &budget,OrdinaryState &state,UnplacedTrace *record){
    require(ordinaryPreparedDomain(input),PN2S_UNSUPPORTED,"Unplaced Offset requires prepared finite-grid geometry");
    validateGroups(input,program.groups);Input active=input;active.pointGroups.insert(active.pointGroups.end(),program.groups.begin(),program.groups.end());
    UnplacedTrace local;auto &trace=record?*record:local;auto random=program.random;
    const auto &child=program.child;OrdinaryControl control;control.values=child.evaluation.values;control.reference=child.evaluation.reference;
    control.referenceScale=child.evaluationReferenceScale;control.auxiliaryReference=true;control.auxiliaryReferenceScale=1;
    control.extremaCount=child.candidater.extremaCount;control.extremaMaxSteps=child.candidater.maximumForwardStrips;
    control.directions={child.candidater.field90,child.candidater.field92,child.candidater.rightToLeft,child.candidater.field93};
    auto decreasing=child.evaluation.decreasing;if(decreasing){control.prepareEvaluation=[&](int32_t placed){recovered::decreasingRandomSetPlaced(*decreasing,placed);};control.dynamicValue=[&](double value){return recovered::decreasingRandomValue(*decreasing,random,[&]{return value;});};}
    control.evaluatePlacement=[&](const Placement &placement,double score){OrdinaryUnplacedHost host(active,program,budget,state,trace,placement,score);return recovered::evaluateUnplacedCandidate(host);};
    try{const int count=ordinaryRun(active,budget,state,&control);trace.committed+=size_t(count);if(state.placementHost)state.placementHost->nesting.random=random;return count;}
    catch(...){if(state.placementHost)state.placementHost->nesting.random=random;throw;}
}
Output ordinaryUnplaced(const Input &input,const UnplacedProgram &program,Budget &budget,UnplacedTrace *trace){OrdinaryState state;ordinaryUnplacedContinue(input,program,budget,state,trace);return std::move(state.output);}
Output ordinaryReverseUnplaced(const Input &input,std::span<const PointsProximityGroup> groups,Budget &budget,recovered::OffsetFactoryInput settings,OrdinaryState *existing,ReverseTrace *trace){
    std::vector<PointsProximityGroup> owned(groups.begin(),groups.end());return ordinaryReverse(input,[owned,settings](const Input &reversed){const auto program=unplacedProgram(reversed,owned,settings);return ReverseRunner([program](const Input &source,Budget &time,OrdinaryState &state){return ordinaryUnplacedContinue(source,program,time,state);});},budget,existing,trace);
}
}
