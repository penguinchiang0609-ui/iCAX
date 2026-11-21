#include "recovered_escher_toroidal.hpp"
#include "recovered_shaker_pair.hpp"
#include "recovered_shaker_geometry_input.hpp"
#include "recovered_convolution_cache.hpp"
#include <bit>
#include <chrono>
#include <numeric>
namespace study::recovered {
namespace {
int64_t clockNs(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
double area(ToroidalBasis basis){return basis[0][0]*basis[1][1]-basis[0][1]*basis[1][0];}
double length(ShakerPoint p){return std::sqrt(p[1]*p[1]+p[0]*p[0]);}
int64_t round64(double value){value=value<0.?value-.5:value+.5;return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int64_t determinant(EscherLatticeBasis basis){return std::bit_cast<int64_t>(uint64_t(basis[0][0])*uint64_t(basis[1][1])-uint64_t(basis[1][0])*uint64_t(basis[0][1]));}
uint64_t unsignedArea(EscherLatticeBasis basis){const auto signedArea=determinant(basis);return signedArea<0?uint64_t(0)-uint64_t(signedArea):uint64_t(signedArea);}
double unsignedDouble(uint64_t value){return value>uint64_t(INT64_MAX)?double((value>>1)|(value&1))*2.:double(int64_t(value));}
struct SavedPart {ShakerPoint position{};double angle=0.;};
std::vector<SavedPart> save(const ShakerInputScene &scene){std::vector<SavedPart> result;for(const auto &part:scene.parts)result.push_back({part.position,part.angle});return result;}
void restore(ShakerInputScene &scene,std::span<const SavedPart> saved){require(saved.size()==scene.parts.size(),PN2S_INTERNAL,"Toroidal saved scene size changed");
    for(size_t n=0;n<saved.size();++n){auto &part=scene.parts[n];part.position=saved[n].position;part.angle=saved[n].angle;part.geometryValid30=0;shakerRefreshPart(scene.parts,scene.geometry,n);}}
std::vector<size_t> imageGeometry(ShakerInputScene &scene,size_t originals){
    std::set<int32_t> keys;for(size_t n=0;n<originals;++n)for(const auto &component:scene.shapes.at(*scene.inputParts[n].shape).components)keys.insert(component.type);
    int32_t next=0;for(const auto key:keys)next=std::max(next,key);next+=100;std::map<int32_t,int32_t> remap;for(const auto key:keys)remap[key]=next++;
    const auto originalRelations=scene.relations.types;
    for(const auto a:keys)for(const auto b:keys){const auto ai=originalRelations.types.find(a),bi=originalRelations.types.find(b);
        if(ai!=originalRelations.types.end()&&bi!=originalRelations.types.end()&&originalRelations.related(ai->second,bi->second)){
            const auto na=scene.relations.types.index(remap[a]),ob=scene.relations.types.index(b);scene.relations.types.set(na,ob,true);
            const auto oa=scene.relations.types.index(a),nb=scene.relations.types.index(remap[b]);scene.relations.types.set(oa,nb,true);
        }}
    std::map<size_t,size_t> cloned;std::vector<size_t> output;
    for(size_t n=0;n<originals;++n){const auto shape=*scene.inputParts[n].shape;auto at=cloned.find(shape);
        if(at==cloned.end()){auto source=scene.shapes.at(shape);const auto target=shakerCreateGeometry(scene,source.reference);
            for(auto &component:source.components){component.type=remap.at(component.type);component.sequence=int32_t(scene.nextComponent++);}scene.shapes[target].components=std::move(source.components);scene.shapes[target].area=source.area;at=cloned.emplace(shape,target).first;}
        output.push_back(at->second);
    }
    return output;
}
void removeImages(ShakerInputScene &scene,size_t originals){
    size_t components=0;for(size_t n=0;n<originals;++n)components+=scene.geometry.parts[n].components.size();
    scene.parts.resize(originals);scene.inputParts.resize(originals);scene.relationParts.resize(originals);scene.geometry.parts.resize(originals);scene.geometry.components.resize(components);
    // The actual images are temporary; preserve real source identities while
    // clearing only their now-dead movement entries before the next round.
    for(auto *movement:{&scene.relations.x,&scene.relations.y}){
        for(auto at=movement->indices.begin();at!=movement->indices.end();)if(at->second>=int32_t(originals))at=movement->indices.erase(at);else ++at;
        movement->parents.resize(originals);movement->ranks.resize(originals);
    }
}
class ToroidalStep {
    ShakerInputScene &scene_;
    size_t originals_;
    std::span<const ToroidalDuplicate> duplicates_;
    std::function<void()> check_;
    EscherToroidalTrace *trace_;
    ConvolutionCacheAccounting accounting_;
    ConvolutionStraightRuntime convolution_;
    std::vector<std::shared_ptr<ConvolutionGeometry>> sources_;
    std::vector<int32_t> types_;
    std::vector<ShakerCheckItem> checkItems_;
    double convolutionSeconds_=0.;
    std::vector<ShakerPairIndices> broadphase(double radius){
        if(check_)check_();std::vector<ShakerBroadItem> items;for(const auto &component:scene_.geometry.components){const auto &box=component.bounds.value;items.push_back({{box[0],box[1]},{box[2],box[3]},component.identity});}return shakerBroadphase(items,radius);
    }
    bool excluded(bool rotation,ShakerPairIndices pair){const auto a=scene_.geometry.components.at(pair[0]).part,b=scene_.geometry.components.at(pair[1]).part;
        auto &first=scene_.relationParts[a],&second=scene_.relationParts[b];first.locked=scene_.parts[a].locked;second.locked=scene_.parts[b].locked;
        return scene_.relations.excluded(rotation,first,second,types_[pair[0]],types_[pair[1]]);
    }
    std::vector<ShakerBoundary> convolution(const ShakerConvolutionKey &key,int32_t mode){
        if(check_)check_();ConvolutionKey request;request.expansion=key.expansion;for(size_t side=0;side<2;++side){const auto &pose=key.poses[side];request.poses[side]={sources_.at(pose.geometry-1),pose.angle,pose.mirror,pose.side};}
        return convolution_.get(request,convolutionSeconds_,static_cast<ConvolutionBackend>(mode));
    }
public:
    ToroidalStep(ShakerInputScene &scene,size_t originals,std::span<const ToroidalDuplicate> duplicates,
        std::function<void()> check,EscherToroidalTrace *trace):scene_(scene),originals_(originals),duplicates_(duplicates),check_(std::move(check)),trace_(trace),
        convolution_(accounting_,[]{return clockNs()/100;},clockNs){
        for(size_t n=0;n<scene.geometry.components.size();++n){const auto &component=scene.geometry.components[n];const auto &part=scene.geometry.parts[component.part];const auto shape=scene.inputParts[component.part].shape;
            require(shape.has_value(),PN2S_INVALID,"Toroidal convolution needs its actual source geometry");
            const auto at=std::find(part.components.begin(),part.components.end(),n);types_.push_back(scene.shapes.at(*shape).components.at(size_t(at-part.components.begin())).type);
            auto source=std::make_shared<ConvolutionGeometry>();source->key=int32_t(n);source->points=component.points;source->reference=part.reference;sources_.push_back(std::move(source));checkItems_.push_back({component.part,n+1});
        }
    }
    ShakerStepReport solve(ToroidalBasis basis,std::array<int32_t,2> layers,double window,double seconds){
        if(check_)check_();auto &parts=scene_.parts;auto &geometry=scene_.geometry;
        ShakerPolicy policy;policy.put<double>(0x28,1e-6);ShakerModelState state=shakerCreateModel(policy,window,0.,false,window*1.001,[]{});
        auto &problem=state.problem;problem.enabled58=problem.enabled59=problem.free5A=1;problem.vector60={basis[0],true};problem.vector78={basis[1],true};
        state.finalize.freeParts.resize(originals_);std::iota(state.finalize.freeParts.begin(),state.finalize.freeParts.end(),0);state.finalize.directionMultiplicities=layers;
        std::vector<size_t> originalIds(originals_);std::iota(originalIds.begin(),originalIds.end(),0);const auto projected=toroidalSceneProjection(scene_,originalIds,basis).value;
        state.finalize.ranges={projected[1],projected[3],projected[0],projected[2]};
        state.finalize.allowances={length(basis[0])*double(layers[0]+1)-(projected[2]-projected[0]),length(basis[1])*double(layers[1]+2)-(projected[3]-projected[1])};
        LinearModel model;ShakerSolution solution;double tolerance=1e-5;int32_t iterationLimit=6000;
        ShakerBuildRuntime build;build.clockNanoseconds=clockNs;build.broadphase=[&](double radius){return broadphase(radius);};build.sceneSpan=[&]{return shakerSceneSpan(parts,geometry);};
        for(const auto duplicate:duplicates_)build.constraints.push_back([&,duplicate](bool rotation){toroidalDuplicateConstraint(duplicate,rotation,problem,parts,model);});
        build.pairRows=[&](ShakerPairIndices pair,int32_t,std::optional<double> admission,double,double &violation,double span){
            if(check_)check_();const auto a=geometry.components[pair[0]].part,b=geometry.components[pair[1]].part;ShakerPairOptions options;
            options.radius=state.broadRadius;options.toleranceLimit=state.toleranceLimit;options.sceneSpan=span;options.hasAdmissionTolerance=admission.has_value();options.admissionTolerance=admission.value_or(0.);
            ShakerConvolutionKey key;key.poses[0]={pair[0]+1,parts[a].angle,parts[a].mirror,1};key.poses[1]={pair[1]+1,parts[b].angle,{uint8_t(!parts[b].mirror[0]),uint8_t(!parts[b].mirror[1])},2};
            ShakerPairSource source{[&](bool rotation){return excluded(rotation,pair);},[&](double expansion,int32_t mode){key.expansion=expansion;return convolution(key,mode);}};
            return shakerPairRows(problem,model,parts[a],parts[b],options,source,violation).status;
        };
        build.finalize=[&]{shakerFinalizeModel(problem,parts,model,state.finalize,state.report,geometry);};
        ShakerStepRuntime step;step.prepareScene=[&]{shakerRefreshScene(parts,geometry);};step.resetSolver=[&]{model={};solution={};};step.solverOption=[&](int32_t value){iterationLimit=value;};step.solverTolerance=[&](double value){tolerance=value;};step.clockNanoseconds=clockNs;
        step.variables=[&]{return shakerVariables(problem,parts,model);};step.buildRows=[&](auto admission,double limit,double &violation){return shakerBuildStep(problem,parts,state.broadRadius,admission,limit,violation,build);};step.columns=[&]{return int32_t(model.columns.size());};step.rows=[&]{return int32_t(model.rows.size());};
        step.optimize=[&](double limit,double &objective){if(check_)check_();solution=solveShakerHighs(model,limit<0.?std::numeric_limits<double>::infinity():limit,tolerance,iterationLimit);return shakerApplySolution(problem,parts,solution,objective,[&](auto &part){shakerRefreshPart(parts,geometry,size_t(&part-parts.data()));});};
        step.solution={[&]{return solution.primal;},[&]{return solution.dual;}};double initial=0.,solved=0.,violation=0.;
        if(trace_)++trace_->solves;const double admission=shakerSceneSpan(parts,geometry)*1e-6;
        return shakerSolveStep(problem,state.report,admission,seconds,initial,solved,violation,step);
    }
    bool accepts(double seconds){
        ShakerCheckRuntime check;check.prepareScene=[&]{shakerRefreshScene(scene_.parts,scene_.geometry);};check.broadphase=[&](double radius){return broadphase(radius);};check.clockNanoseconds=clockNs;
        check.excluded=[&](bool rotation,ShakerPairIndices pair){return excluded(rotation,pair);};check.convolution=[&](const auto &key,int32_t mode){return convolution(key,mode);};
        return shakerCheckPlacement(scene_.parts,checkItems_,seconds,shakerSceneSpan(scene_.parts,scene_.geometry)*1e-6,check)==1;
    }
};
struct ToroidalRunResult {ToroidalBasis basis{};bool again=true,success=false;};
ToroidalRunResult runToroidal(ShakerInputScene &scene,size_t originals,std::span<const ToroidalDuplicate> duplicates,
    ToroidalBasis basis,std::array<int32_t,2> layers,double seconds,double window,const std::function<void()> &check,EscherToroidalTrace *trace){
    ToroidalRunResult result;result.basis=basis;const auto start=clockNs();auto current=toroidalCanonicalDirections(basis);double best=area(current);
    if(best<=0.)return result;const auto initial=save(scene);auto accepted=initial;ToroidalStep solver(scene,originals,duplicates,check,trace);
    for(int32_t iteration=1;iteration<=10;++iteration){if(check)check();const double elapsed=double((clockNs()/1000000)-(start/1000000))/1000.;if(seconds<=elapsed)break;
        const auto report=solver.solve(current,layers,window,seconds);if(report.status){if(trace)++trace->failedSolves;result.again=false;break;}
        require(report.firstDirection&&report.secondDirection&&report.constrained.has_value(),PN2S_INTERNAL,"Toroidal LP omitted its directional report");
        const ToroidalBasis proposed{*report.firstDirection,*report.secondDirection};const double lambda=toroidalAreaLineSearch(current,proposed);
        const double movement=length({proposed[0][0]-current[0][0],proposed[0][1]-current[0][1]})/length(current[0])+length({proposed[1][0]-current[1][0],proposed[1][1]-current[1][1]})/length(current[1]);
        if(lambda<1./144.||movement<.001){result.again=*report.constrained&&iteration>=2;result.basis=current;break;}
        ToroidalBasis next;const double remaining=1.-lambda;for(size_t side=0;side<2;++side)for(size_t axis=0;axis<2;++axis)next[side][axis]=current[side][axis]*remaining+proposed[side][axis]*lambda;
        const double objective=area(next);require(!(objective<0.)&&objective/best<=std::bit_cast<double>(uint64_t(0x3ff000000000000c)),PN2S_INTERNAL,"Native Toroidal line search increased/inverted its accepted area");
        if(.9999<objective/best){result.again=*report.constrained&&iteration>=2;break;}
        result.basis=next;best=objective;const auto reduced=toroidalCanonicalDirections(next);const bool changed=reduced!=next;
        for(size_t n=0;n<scene.parts.size();++n){auto &part=scene.parts[n];part.position[0]=part.position[0]*lambda+accepted[n].position[0]*remaining;part.position[1]=accepted[n].position[1]*remaining+lambda*part.position[1];
            // Rotation is forbidden; the native716b70 interpolation therefore
            // retains the same source angle on this actual execution path.
            part.angle=accepted[n].angle;part.geometryValid30=0;shakerRefreshPart(scene.parts,scene.geometry,n);}
        accepted=save(scene);result.success=true;if(trace)++trace->acceptedSteps;current=reduced;if(changed){result.basis=current;result.again=true;break;}
    }
    restore(scene,result.success?std::span<const SavedPart>(accepted):std::span<const SavedPart>(initial));
    if(result.success&&!solver.accepts(seconds))result.success=false;
    return result;
}
ShakerInputScene createScene(const EscherIntegerShapeRecord &owner,std::span<const EscherPatternRecord> records){
    ShakerInputScene scene;const auto shape=shakerCreateGeometry(scene,{});const auto &selected=owner.alternate?*owner.alternate:owner.normal;
    const auto &contours=selected.normalContours();require(!contours.empty(),PN2S_INVALID,"Toroidal source has no actual integer contours");
    //799690 calls70cc50 once for EVERY selected integer ring. That API passes
    //outer=true to70ca10, including an integer-owner hole. Preserve this
    //conservative original provider behavior instead of inventing regrouping.
    for(const auto &ring:contours){std::vector<ShakerPoint> points;for(const auto p:ring)points.push_back({double(p[0]),double(p[1])});points=shakerCoordinatePolygon(points,true);
        shakerAppendVisitedComponent(scene,shape,1,points,std::fabs(double(convolutionIntegerArea(ring))),{});}
    const auto type=scene.relations.types.index(1);scene.relations.types.set(type,type,true);
    for(const auto &record:records){require(record.geometry==reinterpret_cast<uintptr_t>(&owner),PN2S_INVALID,"Toroidal template has a foreign source owner");
        shakerCreatePart(scene,shape,{double(record.x),double(record.y)},(double(record.angle)/10000.)*.017453292519943295,{uint8_t(record.reflected),0},0);}
    return scene;
}
std::optional<EscherEnumerationDescriptor> shake(const EscherEnumerationDescriptor &source,const EscherPeriodCandidate &candidate,
    int64_t &minimum,Budget *budget,EscherToroidalTrace *trace){
    if(source.records.empty())return {};const auto &owner=*reinterpret_cast<const EscherIntegerShapeRecord *>(source.records.front().geometry);
    auto scene=createScene(owner,source.records);const auto originals=scene.parts.size();const auto clones=imageGeometry(scene,originals);
    ToroidalBasis basis;for(size_t side=0;side<2;++side)for(size_t axis=0;axis<2;++axis)basis[side][axis]=double(candidate.basis[side][axis]);
    const auto check=[&]{if(budget)budget->check(true);};const double window=shakerSceneSpan(scene.parts,scene.geometry)*(1./30.);
    const auto start=clockNs();double remaining=5.;
    for(bool again=true;again&&remaining>0.;){check();basis=toroidalCanonicalDirections(basis);toroidalWrapParts(scene,basis,check);const auto layers=toroidalLayerCounts(scene,basis);
        auto duplicates=toroidalDuplicateParts(scene,originals,basis,layers,clones,check);
        const double allowed=budget?std::min(remaining,std::chrono::duration<double>(budget->deadline-std::chrono::steady_clock::now()).count()):remaining;
        const auto result=runToroidal(scene,originals,duplicates,basis,layers,allowed,window,check,trace);removeImages(scene,originals);basis=result.basis;again=result.again;
        remaining=5.-double((clockNs()/1000000)-(start/1000000))/1000.;
    }
    std::vector<EscherPatternRecord> records;records.reserve(originals);
    for(size_t n=0;n<originals;++n){const auto &part=scene.parts[n];require(!part.mirror[1],PN2S_UNSUPPORTED,"Toroidal result contains forbidden second reflection");
        auto angle=int32_t(round64(part.angle*57.29577951308232*10000.))%3600000;if(angle<0)angle+=3600000;
        auto record=escherOwnerPatternRecord(owner,part.mirror[0]!=0,angle);record.x=round64(part.position[0]);record.y=round64(part.position[1]);records.push_back(record);}
    auto built=escherSourceDescriptor(records);built.descriptor.sameWidth=source.sameWidth;
    EscherLatticeBasis rounded;for(size_t side=0;side<2;++side)for(size_t axis=0;axis<2;++axis)rounded[side][axis]=round64(basis[side][axis]);
    auto output=escherToroidalDescriptor(records,rounded,source.restrictionDegrees,source.orientationChoices,minimum);output.sameWidth=source.sameWidth;return output;
}
}
std::optional<EscherEnumerationDescriptor> escherToroidalShakeModel(const EscherEnumerationDescriptor &source,
    const EscherPeriodCandidate &candidate,int64_t &minimum,Budget *budget,EscherToroidalTrace *trace){return shake(source,candidate,minimum,budget,trace);}
void escherImproveToroidalCatalog(EscherGeometryCatalog &catalog,int32_t level,Budget *budget,EscherToroidalTrace *trace,const EscherToroidalShakeProvider &provider){
    if(trace)*trace={};const auto count=catalog.ordinary.size();
    for(size_t index=0;index<count;++index){if(budget)budget->check(true);if(trace)++trace->descriptors;
        const auto source=catalog.ordinary[index];if(source.restrictionDegrees>=1)continue;require(!source.records.empty(),PN2S_INVALID,"Toroidal catalog has an empty source descriptor");
        const int32_t candidates=std::min<int32_t>(int32_t(source.normalCandidates.size()),level);
        for(int32_t n=0;n<candidates;++n){if(budget)budget->check(true);if(trace)++trace->candidates;
            const auto &candidate=source.normalCandidates[size_t(n)];const double previous=unsignedDouble(unsignedArea(candidate.basis)/source.records.size());
            if(previous<double(catalog.minimumCellArea)*.99999||double(catalog.minimumCellArea)*1.03<previous)continue;
            auto improved=provider?provider(source,candidate,catalog.minimumCellArea):shake(source,candidate,catalog.minimumCellArea,budget,trace);if(!improved)continue;
            int64_t best=INT64_MAX;for(const auto *list:{&improved->normalCandidates,&improved->specialCandidates})for(const auto &item:*list){const auto signedArea=determinant(item.basis);const auto absolute=signedArea<0?std::bit_cast<int64_t>(uint64_t(0)-uint64_t(signedArea)):signedArea;best=std::min(best,absolute);}
            if(unsignedDouble(uint64_t(best)/improved->records.size())<previous*.9999){catalog.ordinary.push_back(std::move(*improved));if(trace)++trace->improved;}
        }
    }
}
}
