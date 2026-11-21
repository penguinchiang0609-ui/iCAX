#include "ordinary_multiheads.hpp"
namespace study {
namespace {
using namespace recovered;
class GeometryDivision final:public MultiheadsDivisionHost,public MultiheadsReplicationHost {
    const Input &source;Budget &budget;const MultiheadsGeometryProgram &program;MultiheadsGeometryTrace &trace;
    Random random;Input lane;bool narrowed=false;
    struct Item {Placement placement;std::optional<size_t> headGroup;};
    std::vector<Item> items;std::vector<size_t> placed;std::vector<std::vector<size_t>> groups;
    std::vector<int32_t> reserved;
    Output current()const{Output result;result.pitchX=rounded(source.pitch*1e6);for(const auto n:placed)result.placements.push_back(items[n].placement);return result;}
    bool used(size_t group,size_t ordinal)const{for(const auto n:placed)if(items[n].placement.group==group&&items[n].placement.ordinal==ordinal)return true;return false;}
public:
    GeometryDivision(const Input &input,Budget &time,const MultiheadsGeometryProgram &settings,MultiheadsGeometryTrace &record,const Random &initialRandom)
        :source(input),budget(time),program(settings),trace(record),random(initialRandom),lane(input),reserved(input.parts.size()){}
    bool hasPlacements()const override{return !placed.empty();}
    void assertEmpty(bool empty)override{require(empty,PN2S_INVALID,"Multiheads division worker requires an empty source layout");}
    double usableHeight()const override{return source.height;}
    double usableWidth()const override{return source.width;}
    bool irregularSheet()const override{return false;}
    void useBoundsSheet()override{throw Failure(PN2S_UNSUPPORTED,"Rectangular input has no irregular-sheet producer");}
    int32_t quantity(size_t group)const override{return source.parts.at(group).quantity;}
    size_t groupCount()const override{return source.parts.size();}
    bool setQuantity(size_t group,int32_t count)override{
        // 36e240 reserves instances from the back of a live job group. The
        // narrow child sees only the unreserved prefix, not `count` instances.
        if(count<1)return true;
        if(count>source.parts[group].quantity-reserved[group])return false;
        reserved[group]+=count;return true;
    }
    void restoreQuantities()override{std::fill(reserved.begin(),reserved.end(),0);}
    void useLane(double width,double height)override{lane=source;lane.width=width;lane.height=height;narrowed=true;
        for(size_t n=0;n<lane.parts.size();++n)lane.parts[n].quantity=source.parts[n].quantity-reserved[n];}
    void restoreSheet()override{narrowed=false;}
    int32_t chooseMethod()override{return multiheadsDivisionMethod(program.forcedMethod,program.methodGroups,lane.height,program.hasPatternProvider,random);}
    void runChild(int32_t method)override{budget.check(true);trace.childMethods.push_back(method);const auto old=current();
        const auto output=program.geometry(narrowed?lane:source,old,method,budget);
        require(valid_layout(narrowed?lane:source,output),PN2S_INTERNAL,"Multiheads geometric child returned an invalid material layout");
        require(output.placements.size()>=old.placements.size(),PN2S_INTERNAL,"Multiheads child removed an existing warm placement");
        for(size_t n=0;n<old.placements.size();++n){const auto &a=old.placements[n],&b=output.placements[n];
            require(a.group==b.group&&a.ordinal==b.ordinal&&a.poseIndex==b.poseIndex&&a.x==b.x&&a.y==b.y,PN2S_INTERNAL,"Multiheads child changed a warm placement identity or grid position");}
        for(size_t n=old.placements.size();n<output.placements.size();++n){items.push_back({output.placements[n],{}});placed.push_back(items.size()-1);}
        if(narrowed)trace.seedPlacements+=output.placements.size()-old.placements.size();}
    void postChild()override{}
    void repeat(const MultiheadsDivisionPlan &plan)override{trace.plan=plan;multiheadsReplicate(plan,0,0,program.child.parameters.fixedDivisions>1,*this);}
    size_t placementCount()const override{return placed.size();}
    std::vector<size_t> placements()const override{return placed;}
    MultiheadsReplicaPosition position(size_t n)const override{return {items.at(n).placement.x,int32_t(items.at(n).placement.y)};}
    void move(size_t n,MultiheadsReplicaPosition position)override{auto &p=items.at(n).placement;p.x=position.x;p.y=position.y;p.exactX=p.exactY=NAN;}
    size_t createHeadGroup(size_t n)override{groups.push_back({n});return groups.size()-1;}
    void appendHeadGroup(size_t group,size_t n)override{groups.at(group).push_back(n);}
    void attachHeadGroup(size_t n,std::optional<size_t> group)override{items.at(n).headGroup=group;}
    std::optional<size_t> cloneAvailable(size_t sourceId,MultiheadsReplicaPosition position)override{budget.check();auto placement=items.at(sourceId).placement;const auto quantity=source.parts.at(placement.group).quantity;
        size_t ordinal=0;while(ordinal<size_t(quantity)&&used(placement.group,ordinal))++ordinal;if(ordinal>=size_t(quantity))return {};
        placement.ordinal=ordinal;placement.x=position.x;placement.y=position.y;placement.exactX=placement.exactY=NAN;
        items.push_back({placement,{}});return items.size()-1;}
    void commit(size_t n)override{placed.push_back(n);++trace.replicaPlacements;}
    bool remove(size_t n)override{const auto at=std::find(placed.begin(),placed.end(),n);if(at==placed.end())return false;placed.erase(at);++trace.rolledBack;return true;}
    void copyRelationships(size_t,size_t,int32_t,int32_t,const std::map<std::tuple<int32_t,int32_t,size_t>,size_t> &)override{}
    MultiheadsGeometryResult finish(){MultiheadsGeometryResult result;result.output=current();std::map<size_t,size_t> outputIndexes;
        std::vector<size_t> starts(source.parts.size());size_t total=0;for(size_t n=0;n<source.parts.size();++n){starts[n]=total;total+=size_t(source.parts[n].quantity);}
        for(size_t n=0;n<placed.size();++n){const auto &p=items[placed[n]].placement;const auto &part=source.parts.at(p.group);
            auto warm=std::make_shared<WarmPlacement>();warm->group08=p.group+1;warm->instance40=starts[p.group]+p.ordinal;
            warm->instance18=int32_t(warm->instance40);warm->x48=p.x;warm->y50=int32_t(p.y);warm->area58=material_area(part);
            auto pose=part.poses.at(p.poseIndex);if(std::isfinite(p.solvedAngle))pose.angle=p.solvedAngle;
            warm->geometryOwner=std::make_shared<const Polygon>(transform(part.polygon,part.pivot,pose));const auto &polygon=*warm->geometryOwner;
            for(size_t edge=0;edge<polygon.size();++edge){const auto a=polygon[edge],b=polygon[(edge+1)%polygon.size()];warm->geometry60+=std::hypot(b.x-a.x,b.y-a.y);}
            const auto box=bounds(polygon);warm->leftE0=rounded(box.x0*1e6);warm->rightF0=rounded(box.x1*1e6);
            warm->bottomE8=int32_t(rounded(box.y0*1e3));warm->topF8=int32_t(rounded(box.y1*1e3));
            outputIndexes[placed[n]]=n;result.placements.push_back(std::move(warm));}
        std::map<size_t,std::shared_ptr<PlacementGroup>> groupOwners;
        for(size_t n=0;n<placed.size();++n)if(const auto group=items[placed[n]].headGroup){auto &owner=groupOwners[*group];if(!owner){owner=std::make_shared<PlacementGroup>();for(const auto member:groups[*group])if(const auto at=outputIndexes.find(member);at!=outputIndexes.end())owner->members.push_back(result.placements[at->second]);}
            auto &warm=result.placements[n];warm->group58=owner;if(!owner->members.empty())warm->groupFirst=owner->members.front();}
        for(const auto item:placed){std::vector<size_t> members;if(items[item].headGroup)for(const auto linked:groups[*items[item].headGroup]){
            const auto at=std::find(placed.begin(),placed.end(),linked);if(at!=placed.end())members.push_back(size_t(at-placed.begin()));}
        trace.simultaneousParts.push_back(std::move(members));}return result;}
};
}
std::vector<recovered::ShakerPairConstraint> multiheadsShakerConstraints(
    std::span<const recovered::WarmPlacementPtr> placements,std::span<const size_t> shakerIndexes){
    using namespace recovered;
    require(shakerIndexes.empty()||shakerIndexes.size()==placements.size(),PN2S_INVALID,"Multiheads Shaker index map size differs from the placement owners");
    std::map<const ProcessPlacement *,size_t> indexes;std::set<size_t> mapped;
    for(size_t n=0;n<placements.size();++n){require(bool(placements[n]),PN2S_INVALID,"Multiheads Shaker placement owner is null");
        const auto index=shakerIndexes.empty()?n:shakerIndexes[n];
        require(indexes.emplace(placements[n].get(),index).second&&mapped.insert(index).second,PN2S_INVALID,"Multiheads Shaker placement/index map is not unique");}
    std::vector<ShakerPairConstraint> constraints;
    for(const auto &placement:placements){const auto &group=placement->group58;if(!group)continue;
        require(!group->members.empty(),PN2S_INVALID,"Multiheads sister group is empty");
        const auto first=group->members.front().lock();require(bool(first),PN2S_INVALID,"Multiheads sister group lost its first owner");
        std::set<const ProcessPlacement *> seen;bool ownsSlot=false;
        for(const auto &slot:group->members){const auto member=slot.lock();
            require(bool(member)&&indexes.contains(member.get())&&member->group58==group&&seen.insert(member.get()).second,PN2S_INVALID,"Multiheads sister group is incomplete or inconsistent");
            ownsSlot|=member==placement;}
        require(ownsSlot,PN2S_INVALID,"Multiheads placement is absent from its sister group");
        if(first!=placement)continue;
        for(const auto &slot:group->members){const auto sister=slot.lock();if(sister==first)continue;
            constraints.push_back({indexes.at(first.get()),indexes.at(sister.get()),0,0,{}});
            constraints.push_back({indexes.at(first.get()),indexes.at(sister.get()),4,0,{}});}}
    return constraints;
}
MultiheadsGeometryResult ordinaryMultiheadsDivisionResult(const Input &input,Budget &budget,const MultiheadsGeometryProgram &program,MultiheadsGeometryTrace *trace){
    require(bool(program.geometry),PN2S_INVALID,"Multiheads requires its actual geometric child factories");
    require(program.child.kind==recovered::MultiheadsChildKind::Division,PN2S_INVALID,"Division runtime cannot execute a clustering worker");
    require(program.child.parameters.heads>=2,PN2S_UNSUPPORTED,"Multiheads requires at least two configured cutting heads");
    require(input.pointGroups.empty(),PN2S_UNSUPPORTED,"Multiheads geometry host needs a concrete relationship and per-instance eligibility provider");
    MultiheadsGeometryTrace local;auto &record=trace?*trace:local;record={};
    std::vector<double> heights;std::vector<int32_t> quantities;for(const auto &part:input.parts){const auto box=bounds(part.polygon);heights.push_back(box.y1-box.y0);quantities.push_back(part.quantity);}
    auto random=program.random;recovered::MultiheadsDivisionInput request{program.child.parameters,input.height,0,0,program.maximumPartGap,heights,quantities};
    record.divisions=recovered::multiheadsDivisionCount(request,random,program.child.randomizeDivisions);GeometryDivision host(input,budget,program,record,random);
    recovered::runMultiheadsDivision(program.child,record.divisions,program.maximumPartGap,host);auto result=host.finish();
    require(valid_layout(input,result.output),PN2S_INTERNAL,"Multiheads replicated layout failed material validation");return result;
}
Output ordinaryMultiheadsDivision(const Input &input,Budget &budget,const MultiheadsGeometryProgram &program,MultiheadsGeometryTrace *trace){
    return ordinaryMultiheadsDivisionResult(input,budget,program,trace).output;
}
} // namespace study
