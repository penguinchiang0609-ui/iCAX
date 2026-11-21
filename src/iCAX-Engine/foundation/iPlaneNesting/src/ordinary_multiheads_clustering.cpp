#include "ordinary_multiheads_clustering.hpp"
#include "ordinary_runtime.hpp"
#include "recovered_convolution_floating.hpp"
#include "recovered_extremal.hpp"
#include "clipper2/clipper.h"
#include <bit>
namespace study {
namespace {
using namespace recovered;
struct ClusterGeometry {std::vector<Polygon> polygons;Box box{};size_t group=0,pose=0;std::vector<TilingMember> members;};
bool overlap(const Polygon &a,const Polygon &b){Clipper2Lib::PathD first,second;for(auto p:a)first.push_back({p.x,p.y});for(auto p:b)second.push_back({p.x,p.y});return std::abs(Clipper2Lib::Area(Clipper2Lib::Intersect(Clipper2Lib::PathsD{first},Clipper2Lib::PathsD{second},Clipper2Lib::FillRule::NonZero,6)))>1e-8;}
class ClusteringGeometry final:public MultiheadsGreedyHost,public MultiheadsGreedyNextHost,
    public MultiheadsCountEvaluationHost,public MultiheadsInstantiationHost,public MultiheadsMemberAdmissionHost {
    const Input &input;Budget &budget;const MultiheadsClusteringProgram &program;MultiheadsClusteringTrace &trace;Random random;
    OffsetValuePlan evaluation;std::vector<ValuePart> valueParts;double totalArea=0;int32_t population=0;
    std::vector<ClusterGeometry> geometry;std::vector<MultiheadsMemberModule> modules;std::vector<MultiheadsModuleDescriptor> descriptors;
    std::vector<std::vector<size_t>> singles;std::vector<size_t> starts;std::vector<uint8_t> occupied;std::vector<int32_t> placedCounts;
    struct Owned {Placement placement;WarmPlacementPtr owner;};std::vector<Owned> owners;std::vector<size_t> active,prepared;
    struct Candidate {size_t module=0;MultiheadsReplicaPosition origin;double score=0;};std::vector<Candidate> candidates;std::vector<size_t> cached;
    std::vector<size_t> currentModules;bool plain=true;size_t primary=0,secondary=0;int64_t pitch=1;double step=1;
    std::map<std::tuple<size_t,size_t,int64_t>,Raster> pairCache;
    std::optional<size_t> lookupGroup(size_t instance)const{for(size_t g=0;g<starts.size();++g)if(instance>=starts[g]&&instance<starts[g]+size_t(input.parts[g].quantity))return g;return {};}
    size_t addGeometry(ClusterGeometry item){item.box={INFINITY,INFINITY,-INFINITY,-INFINITY};for(const auto &polygon:item.polygons){const auto b=bounds(polygon);item.box.x0=std::min(item.box.x0,b.x0);item.box.y0=std::min(item.box.y0,b.y0);item.box.x1=std::max(item.box.x1,b.x1);item.box.y1=std::max(item.box.y1,b.y1);}geometry.push_back(std::move(item));return geometry.size()-1;}
    const Raster &pair(size_t a,size_t b,int64_t phase=0){const auto key=std::tuple{a,b,phase};if(const auto found=pairCache.find(key);found!=pairCache.end())return found->second;
        std::vector<Polygon> rings;for(const auto &pa:geometry[a].polygons)for(const auto &pb:geometry[b].polygons){Polygon first,minus;for(auto p:pa)first.push_back({p.x-geometry[a].box.x0,p.y-geometry[a].box.y0});for(auto p:pb)minus.push_back({geometry[b].box.x0-p.x,geometry[b].box.y0-p.y});
            auto result=convolution(first,minus,budget);for(auto &ring:result){for(auto &p:ring)p.x+=double(phase)/1e6;rings.push_back(std::move(ring));}}
        return pairCache.emplace(key,raster(rings,pitch,budget)).first->second;}
    Rows freeMap(size_t module,bool extended=false){const auto &g=geometry[module];const double width=g.box.x1-g.box.x0,height=g.box.y1-g.box.y0;
        const auto count=int64_t(std::floor(((extended?input.width+2*width:input.width-width)/step)+1));const auto high=int64_t(std::floor((input.height-height+1e-7)*1000.));if(count<1||high<0)return Rows(1);
        require(count<=20000,PN2S_RESOURCE_LIMIT,"Multiheads compound map strip limit");Rows rows(size_t(count),Row{{0,high}});
        for(const auto id:active){const auto &p=owners[id].placement;const auto source=singles[p.group][p.poseIndex];const auto &shape=geometry[source];
            const int64_t x=p.x+rounded(shape.box.x0*1e6),y=p.y+rounded(shape.box.y0*1000.);auto strip=x/pitch,phase=x%pitch;if(phase<0){--strip;phase+=pitch;}
            const auto &forbidden=pair(source,module,phase);const auto sx=strip-forbidden.offsetStrip,sy=y-forbidden.offsetY;
            for(size_t n=0;n<forbidden.rows.size();++n){const auto at=sx+int64_t(n);if(at<0||uint64_t(at)>=rows.size())continue;Row shifted;for(const auto interval:forbidden.rows[n])shifted.push_back({interval[0]+sy,interval[1]+sy});rows[size_t(at)]=difference(rows[size_t(at)],shifted);}}
        return rows;}
    double integral(const Rows &free,const Raster &map,int64_t strip,int64_t y){const auto sx=strip-map.offsetStrip,sy=y-map.offsetY;double length=0;
        for(size_t n=0;n<map.rows.size();++n){const auto row=sx+int64_t(n);if(row<0||uint64_t(row)>=free.size())continue;for(const auto a:free[size_t(row)])for(const auto b:map.rows[n]){const auto low=std::max(a[0],b[0]+sy),high=std::min(a[1],b[1]+sy);if(high>low)length+=double(high-low);}}
        return step*(length/1000.);}
    std::vector<size_t> generate(std::span<const size_t> selected){std::vector<size_t> result;const auto primaryFree=freeMap(primary,evaluation.reference==ReferenceKind::Normal);std::optional<Rows> secondaryFree;
        int64_t maximum=0;for(const auto id:active){const auto &p=owners[id].placement;maximum=std::max(maximum,p.x+rounded(geometry[singles[p.group][p.poseIndex]].box.x1*1e6));}
        if(evaluation.decreasing)decreasingRandomSetPlaced(*evaluation.decreasing,int32_t(active.size()));
        for(const auto module:selected){budget.check();const auto &g=geometry[module];auto free=freeMap(module);ExtremalInput scan;scan.end=int32_t(free.size());while(scan.first<scan.end&&free[size_t(scan.first)].empty())++scan.first;scan.count=4;scan.maxSteps=std::min(scan.end-scan.first,100);scan.supplementEnd=std::clamp(int32_t((maximum+pitch-1)/pitch),scan.first,scan.end);
            std::vector<StripPoint> points;for(const auto direction:{std::pair{true,true},std::pair{false,true}}){scan.lower=direction.first;scan.fromLow=direction.second;const auto next=extremalPoints(free,scan);points.insert(points.end(),next.begin(),next.end());}
            if(points.empty()&&active.empty()){FirstPlacementInput first;first.width=input.width;first.height=input.height;first.partBounds=g.box;first.pitch=pitch;if(auto p=firstPlacement(first))points.push_back(*p);}
            for(const auto point:points){ScoreInput scoreInput;scoreInput.primaryCost=integral(primaryFree,pair(module,primary),point[0],point[1]);scoreInput.value=evaluation.values[g.group]*double(g.members.size());
                if(evaluation.decreasing)scoreInput.value=decreasingRandomValue(*evaluation.decreasing,random,[&]{return scoreInput.value;});
                scoreInput.candidateRightX=point[0]*pitch+rounded((g.box.x1-g.box.x0)*1e6);scoreInput.candidateWidth=g.box.x1-g.box.x0;scoreInput.sheetHeight=input.height;scoreInput.maxPlacedX=maximum;ScoreState state;auto score=recovered::score(scoreInput,state);
                if(std::abs(score)<1e-9){if(!secondaryFree)secondaryFree=freeMap(secondary);scoreInput.secondaryCost=integral(*secondaryFree,pair(module,secondary),point[0],point[1]);state.mix=.001;score=recovered::score(scoreInput,state);}
                if(!std::isfinite(score))continue;const auto id=candidates.size();candidates.push_back({module,{point[0]*pitch-rounded(g.box.x0*1e6),int32_t(point[1]-rounded(g.box.y0*1000.))},score});result.push_back(id);++trace.candidates;}}
        return result;}
    MultiheadsSelection best(std::span<const size_t> values){std::optional<size_t> selected;for(const auto n:values)if(!selected||(std::abs(candidates[n].score-candidates[*selected].score)>=1e-5&&candidates[n].score<candidates[*selected].score))selected=n;
        return {selected,selected?std::bit_cast<uint64_t>(candidates[*selected].score):0,0};}
    size_t makeOwner(size_t instance,MultiheadsReplicaPosition position,bool offGrid,int32_t kind,size_t group,size_t pose){auto owner=std::make_shared<WarmPlacement>();const auto &part=input.parts[group];const auto &g=geometry[singles[group][pose]];
        owner->instance40=instance;owner->instance18=int32_t(instance);owner->group08=group+1;owner->x48=position.x;owner->y50=position.y;owner->marker28=kind;owner->rounded2c=offGrid;owner->area58=area(part.polygon);owner->geometryOwner=std::make_shared<const Polygon>(g.polygons.front());
        owner->leftE0=rounded(g.box.x0*1e6);owner->rightF0=rounded(g.box.x1*1e6);owner->bottomE8=int32_t(rounded(g.box.y0*1000.));owner->topF8=int32_t(rounded(g.box.y1*1000.));
        const auto &polygon=*owner->geometryOwner;for(size_t n=0;n<polygon.size();++n)owner->geometry60+=std::hypot(polygon[(n+1)%polygon.size()].x-polygon[n].x,polygon[(n+1)%polygon.size()].y-polygon[n].y);
        owners.push_back({{group,instance-starts[group],pose,position.x,position.y},std::move(owner)});return owners.size()-1;}
    std::map<size_t,std::pair<size_t,size_t>> allocatedPoses;size_t currentCandidate=0;
public:
    ClusteringGeometry(const Input &source,Budget &time,const MultiheadsClusteringProgram &settings,MultiheadsClusteringTrace &record)
        :input(source),budget(time),program(settings),trace(record),random(settings.random),singles(source.parts.size()),starts(source.parts.size()),placedCounts(source.parts.size()){
        trace.cardinalOnly=random.unit()<.5;valueParts=ordinaryValueParts(input,totalArea,population);auto factory=program.valueFactory;factory.population88=population;factory.dynamicEvaluation=program.trial>0;evaluation=offsetValue(random,factory,valueParts,totalArea);
        pitch=input.pitch?rounded(input.pitch*1e6):1000000;require(pitch>0,PN2S_INVALID,"Multiheads clustering grid pitch is zero");step=double(pitch)/1e6;size_t total=0;
        for(size_t group=0;group<input.parts.size();++group){starts[group]=total;total+=size_t(input.parts[group].quantity);for(size_t pose=0;pose<input.parts[group].poses.size();++pose){const auto polygon=transform(input.parts[group].polygon,input.parts[group].pivot,input.parts[group].poses[pose]);
            auto orientation=quantizeSdkOrientation({input.parts[group].poses[pose].flip?1:0,input.parts[group].poses[pose].angle,input.parts[group].poses[pose].angle},true);const auto box=bounds(polygon);TilingMember member{group+1,orientation,{},box,-1};const auto id=addGeometry({{polygon},{},group,pose,{member}});singles[group].push_back(id);}}
        occupied.resize(total);
        if(program.warm){const auto &warm=*program.warm;require(warm.placements.size()==warm.output.placements.size(),PN2S_INVALID,"Multiheads warm layout lost its placement owners");multiheadsShakerConstraints(warm.placements);
            std::map<uintptr_t,std::shared_ptr<ProcessPlacement>> copies;for(size_t n=0;n<warm.placements.size();++n){const auto &p=warm.output.placements[n];const auto instance=starts.at(p.group)+p.ordinal;require(instance<occupied.size()&&!occupied[instance],PN2S_INVALID,"Multiheads warm instance identity is invalid or repeated");require((!std::isfinite(p.solvedAngle)||std::abs(p.solvedAngle-input.parts.at(p.group).poses.at(p.poseIndex).angle)<1e-9)&&(!std::isfinite(p.exactX)||std::abs(p.exactX-double(p.x)/1e6)<1e-9)&&(!std::isfinite(p.exactY)||std::abs(p.exactY-double(p.y)/1000.)<1e-9),PN2S_UNSUPPORTED,"Multiheads warm free-map requires concrete off-grid or refined-angle geometry providers");auto clone=std::make_shared<WarmPlacement>(*warm.placements[n]);clone->edges.clear();clone->group58.reset();clone->groupFirst.reset();require(clone->instance40==instance,PN2S_INVALID,"Multiheads warm catalog identity differs from its output ordinal");copies[instance]=clone;owners.push_back({p,std::move(clone)});active.push_back(owners.size()-1);occupied[instance]=1;++placedCounts[p.group];}
            const PlacementCopyLookup lookup=[&](uintptr_t id)->std::optional<std::shared_ptr<ProcessPlacement>>{const auto found=copies.find(id);return found==copies.end()?std::optional<std::shared_ptr<ProcessPlacement>>{}:found->second;};
            for(size_t n=0;n<warm.placements.size();++n){copyPlacementRelations(*warm.placements[n],owners[n].owner,lookup);copyPlacementProcesses(*warm.placements[n],owners[n].owner,lookup);}}
        std::map<size_t,std::vector<MultiheadsClusterPitchSource>> sources;
        for(size_t group=0;group<input.parts.size();++group){ConvolutionRing ring,negative;for(const auto p:input.parts[group].polygon){ring.push_back({p.x,p.y});negative.push_back({-p.x,-p.y});}const auto nfp=convolutionFloatingMinkowski(ring,negative);std::vector<ShakerPoint> directions;std::vector<double> heights;
            for(const auto id:singles[group]){directions.push_back(multiheadsCachedOrientationBasis(geometry[id].members.front().orientation));heights.push_back(geometry[id].box.y1-geometry[id].box.y0);}sources[group]=multiheadsProducePitchSources(nfp,directions,heights,input.height,&budget);}
        trace.keys=multiheadsClusterKeys(program.child.clusterTrials,program.child.halfClusters,[&](auto &work){std::vector<int32_t> available;std::vector<size_t> eligible;for(size_t group=0;group<input.parts.size();++group)available.push_back(input.parts[group].quantity-placedCounts[group]);size_t choices=std::count_if(available.begin(),available.end(),[](int32_t q){return q>=2;});for(const auto raw:work){if(choices==0||--choices<1)break;if(raw>0&&raw<=available.size())available[size_t(raw-1)]=0;}
            const auto minimum=program.child.parameters.fixedDivisions>1?program.child.parameters.fixedDivisions:2;for(size_t group=0;group<available.size();++group)if(available[group]>=minimum)eligible.push_back(group);if(eligible.empty())return MultiheadsClusterKey{};
            std::stable_sort(eligible.begin(),eligible.end(),[&](size_t a,size_t b){const auto x=bounds(input.parts[a].polygon),y=bounds(input.parts[b].polygon);return x.y1-x.y0>y.y1-y.y0;});const auto maximum=truncateInt32(double(eligible.size()-1)*program.child.clusterVariation);const auto selected=eligible[size_t(std::min<int32_t>(random.integer(0,maximum),int32_t(eligible.size()-1)))];if(random.unit()+0.<=.5)work.push_back(selected+1);
            return multiheadsChooseClusterConfig(sources[selected],available[selected],program.child.parameters,input.height,program.child.randomCluster,random);});
        int32_t identity=-1;for(const auto key:trace.keys)for(size_t group=0;group<input.parts.size();++group)if(input.parts[group].quantity>=key.heads)for(size_t pose=0;pose<singles[group].size();++pose){const auto &sourceGeometry=geometry[singles[group][pose]];const auto orientation=sourceGeometry.members.front().orientation;auto angle=orientation.angle<0?-int64_t(orientation.angle):int64_t(orientation.angle);if(angle>=36000000)angle-=36000000;if(trace.cardinalOnly&&(angle/100000)%90!=0)continue;
            const auto compound=multiheadsClusterGeometry(key,group+1,orientation,sourceGeometry.box,identity);std::vector<Polygon> polygons;bool valid=true;for(const auto &member:compound.members){auto polygon=sourceGeometry.polygons.front();for(auto &p:polygon){p.x+=member.position.x;p.y+=member.position.y;}for(const auto &prior:polygons)if(overlap(prior,polygon))valid=false;polygons.push_back(std::move(polygon));}if(!valid)continue;
            const auto id=addGeometry({std::move(polygons),{},group,pose,compound.members});MultiheadsMemberModule module;module.descriptor={id+1,key.heads,group,input.parts[group].quantity,false};module.members=compound.members;modules.push_back(std::move(module));descriptors.push_back(modules.back().descriptor);}
        trace.modules=modules.size();const auto mean=effectiveAverage(valueParts,totalArea,{1,1});primary=addGeometry({{referenceShape(mean,input.height,evaluation.reference)}});secondary=addGeometry({{Polygon{{0,0},{4*step,0},{4*step,step},{0,step}}}});}
    int32_t prepareNext()override{return prepareMultiheadsGreedyNext({program.child.parameters.fixedDivisions>1,false,true},*this);}
    void commitPrepared()override{for(const auto id:prepared){const auto &p=owners[id].placement;require(!occupied[starts[p.group]+p.ordinal],PN2S_INTERNAL,"Multiheads duplicate committed instance");occupied[starts[p.group]+p.ordinal]=1;active.push_back(id);++placedCounts[p.group];}if(prepared.size()>1)++trace.compoundCommits;else trace.singleCommits+=prepared.size();}
    double remainingTime()override{return std::chrono::duration<double>(budget.deadline-std::chrono::steady_clock::now()).count();}
    bool stopRequested()override{return false;}
    void clearPrepared()override{prepared.clear();candidates.clear();}
    void clearCandidateCache()override{cached.clear();plain=true;}
    MultiheadsSelection cachedCandidate()override{if(!plain)return best(cached);std::vector<size_t> selected;for(size_t group=0;group<input.parts.size();++group)if(placedCounts[group]<input.parts[group].quantity)selected.insert(selected.end(),singles[group].begin(),singles[group].end());return best(generate(selected));}
    void clearCachedDetail(size_t)override{}
    void select(MultiheadsSelection &selection)override{selectMultiheadsModuleCandidate(descriptors,selection,*this);}
    void setMetadata(uint64_t)override{}
    void notifySingle(MultiheadsPlacement &selection)override{if(!selection)return;const auto &candidate=candidates[*selection];const auto &g=geometry[candidate.module];size_t ordinal=0;while(ordinal<size_t(input.parts[g.group].quantity)&&occupied[starts[g.group]+ordinal])++ordinal;selection=makeOwner(starts[g.group]+ordinal,candidate.origin,false,0,g.group,g.pose);}
    std::vector<MultiheadsPlacement> materialize(size_t selection)override{currentCandidate=selection;const auto &candidate=candidates[selection];allocatedPoses.clear();const auto created=materializeMultiheadsModule(modules,candidate.module+1,{candidate.origin,pitch,0,false,true},*this,*this,[&](bool multiple){require(multiple,PN2S_INTERNAL,"Multiheads selected compound could not retain multiple members");});std::vector<MultiheadsPlacement> result;for(const auto id:created)result.push_back(id);return result;}
    void notifyCompound(MultiheadsPlacement &,std::vector<MultiheadsPlacement> &)override{}
    bool acceptCompound(std::vector<MultiheadsPlacement> &members)override{std::vector<size_t> ids;for(const auto member:members){if(!member)return false;ids.push_back(*member);}return multiheadsAdmitMembers(ids,*this);}
    void appendPrepared(std::span<const MultiheadsPlacement> members)override{for(const auto member:members){require(bool(member),PN2S_INTERNAL,"Multiheads observer returned an empty placement");prepared.push_back(*member);}}
    void nullSelectionDiagnostic()override{throw Failure(PN2S_INTERNAL,"Multiheads observer cleared a selected compound");}
    void prepareCount(int32_t count)override{std::vector<size_t> activeGroups;for(size_t n=0;n<input.parts.size();++n)if(placedCounts[n]<input.parts[n].quantity)activeGroups.push_back(n);const auto eligible=multiheadsEligibleModules(descriptors,input.parts.size(),activeGroups,placedCounts,count);currentModules.clear();for(const auto n:eligible)currentModules.push_back(size_t(descriptors[n].geometry-1));}
    std::vector<size_t> produceCandidates()override{return generate(currentModules);}
    void cacheCandidates(std::span<const size_t> values)override{cached.assign(values.begin(),values.end());plain=false;}
    std::optional<size_t> resolveVariant(uintptr_t part,PlacementOrientation orientation)override{const auto group=size_t(part-1);for(const auto id:singles.at(group))if(geometry[id].members.front().orientation==orientation)return id;return {};}
    std::vector<std::optional<MultiheadsAllocatedInstance>> allocateInstances(std::span<const size_t> variants)override{auto reserved=occupied;std::vector<std::optional<MultiheadsAllocatedInstance>> result;for(const auto variant:variants){const auto &g=geometry[variant];size_t ordinal=0;while(ordinal<size_t(input.parts[g.group].quantity)&&reserved[starts[g.group]+ordinal])++ordinal;if(ordinal==size_t(input.parts[g.group].quantity)){result.push_back({});continue;}const auto instance=starts[g.group]+ordinal;reserved[instance]=1;allocatedPoses[instance]={g.group,g.pose};result.push_back(MultiheadsAllocatedInstance{instance,rounded(g.box.x0*1e6)});}return result;}
    size_t createPlacement(size_t instance,MultiheadsReplicaPosition position,bool offGrid,int32_t kind)override{const auto [group,pose]=allocatedPoses.at(instance);return makeOwner(instance,position,offGrid,kind,group,pose);}
    void commonCut(size_t first,size_t second,double value)override{linkCommonCut(owners[first].owner,owners[second].owner,value);}
    void createSisterGroup(std::span<const size_t> ids)override{auto group=std::make_shared<PlacementGroup>();for(const auto id:ids)group->members.push_back(owners[id].owner);for(const auto id:ids){owners[id].owner->group58=group;if(!ids.empty())owners[id].owner->groupFirst=owners[ids.front()].owner;}}
    int32_t state(size_t id)override{return occupied[owners[id].owner->instance40];}
    void insertTemporary(size_t id)override{occupied[owners[id].owner->instance40]=1;active.push_back(id);}
    bool validateTemporary(size_t)override{++trace.temporaryValidations;return valid_layout(input,result().output);}
    void removeTemporary(size_t id)override{++trace.temporaryRemovals;occupied[owners[id].owner->instance40]=0;const auto at=std::find(active.begin(),active.end(),id);if(at!=active.end())active.erase(at);}
    MultiheadsGeometryResult result()const{MultiheadsGeometryResult result;result.output.pitchX=pitch;for(const auto id:active){result.output.placements.push_back(owners[id].placement);result.placements.push_back(owners[id].owner);}return result;}
};
}
MultiheadsGeometryResult ordinaryMultiheadsClusteringResult(const Input &input,Budget &budget,const MultiheadsClusteringProgram &program,MultiheadsClusteringTrace *trace){
    require(program.child.kind==recovered::MultiheadsChildKind::Clustering&&program.child.parameters.heads>=2,PN2S_INVALID,"Multiheads clustering requires its actual child and hardware parameters");
    require(input.gap==0&&input.pointGroups.empty(),PN2S_UNSUPPORTED,"Multiheads compound geometry requires normalized spacing and concrete instance-constraint providers");
    for(const auto &part:input.parts){require(part.holes.empty()&&part.protection==0,PN2S_UNSUPPORTED,"Multiheads compound protection/hole producers are not connected");for(const auto pose:part.poses)require(pose.angle==pose.upper,PN2S_UNSUPPORTED,"Multiheads cached continuous-angle variant bounds are not connected");}
    MultiheadsClusteringTrace local;auto &record=trace?*trace:local;record={};ClusteringGeometry host(input,budget,program,record);recovered::runMultiheadsGreedy(host);auto result=host.result();require(valid_layout(input,result.output),PN2S_INTERNAL,"Multiheads clustering failed strict material validation");multiheadsShakerConstraints(result.placements);return result;
}
} // namespace study
