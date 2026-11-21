#include "ordinary_escher.hpp"
#include "ordinary_unplaced.hpp"
#include "ordinary_pair_map.hpp"
#include "recovered_escher_toroidal.hpp"
#include "recovered_escher_construct.hpp"
#include "recovered_escher_conversion.hpp"
#include "recovered_escher_fuzzy_bridge.hpp"
#include "recovered_general_tiling_supplemental.hpp"
#include <bit>
namespace study {
namespace {
using namespace recovered;
int32_t magnitude(PlacementOrientation pose){auto angle=pose.angle<0?std::bit_cast<int32_t>(0u-uint32_t(pose.angle)):pose.angle;return angle>=36000000?angle-36000000:angle;}
int64_t round64(double value){value+=value<0.?-.5:.5;return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int64_t spacing(const EscherGeometryCatalog &catalog){int64_t sum=0,count=0;for(const auto &descriptor:catalog.ordinary)if(descriptor.records.size()!=1)for(size_t n=0;n<std::min<size_t>(5,descriptor.normalCandidates.size());++n){sum=std::bit_cast<int64_t>(uint64_t(sum)+uint64_t(descriptor.normalCandidates[n].step[1]));++count;}return round64(double(sum)*(1./double(count)));}
struct ShapeGeometry {
    const PartInput &part;
    std::vector<Polygon> contours;
    ShapeAxisSymmetry symmetry;
    Box original;
    double materialArea=0.;
    explicit ShapeGeometry(const PartInput &source):part(source),contours(ordinaryPreparedContours(source,{})),
        symmetry(shapeAxisSymmetry(contours,bounds(contours.front()))),original(bounds(contours.front())) {
        for(const auto &ring:contours)materialArea+=area(ring);
        require(materialArea>0.,PN2S_INVALID,"Escher prepared shape has no material");
    }
    Box variantBounds(PlacementOrientation orientation)const {
        const auto rings=ordinaryPreparedContours(part,{double(magnitude(orientation))/100000.,orientation.angle<0});
        return bounds(rings.front());
    }
    std::optional<Placement> placement(size_t group,const TilingMember &member)const{
        for(const auto equivalent:equivalentOrientations(member.orientation,symmetry.horizontal,symmetry.vertical))for(size_t index=0;index<part.poses.size();++index){const auto &allowed=part.poses[index];if(allowed.flip!=(equivalent.angle<0))continue;const double angle=double(magnitude(equivalent))/100000.,actual=angle+std::ceil((allowed.angle-angle-1e-7)/360.)*360.;if(actual<allowed.angle-1e-7||actual>allowed.upper+1e-7)continue;
            const auto correction=reverseVariantOffset({original.x0,original.y0,original.x1,original.y1},member.orientation,equivalent);Placement result{group,0,index};result.x=rounded((member.position.x+correction.x)*1e6);result.y=rounded((member.position.y+correction.y)*1000.);return result;}
        return {};
    }
};
struct PatternProvider {
    int64_t cachedSpacing=0;EscherToroidalTrace toroidal;
    EscherGeometryCatalog catalog;GeneralTilingSupplementalCatalog prepared;EscherProvider provider;
    EscherPlacementMapCache placementCache;EscherBoundaryLengthServices boundary;
    PatternProvider(const EscherIntegerShapeRecord &owner,uint32_t limit,double scale,int32_t quantity,double seconds,Budget &budget)
        :catalog([&]{auto value=escherProduceGeometryCatalog(owner,limit);cachedSpacing=spacing(value);
            escherImproveToroidalCatalog(value,int32_t(std::min(std::trunc(seconds/29.),3.)),&budget,&toroidal);return value;}()),
         prepared(catalog),provider(scale,quantity){escherBindBoundaryGeometry(boundary,placementCache);}
};
class OrdinaryEscherHost final:public EscherNesterHost {
    const Input &input;const EscherProgram &program;Budget &budget;OrdinaryState &state;OrdinaryEscherTrace &trace;Random random;
    std::vector<ShapeGeometry> geometries;EscherIntegerJob integerJob;std::vector<std::unique_ptr<PatternProvider>> providers;
    std::vector<EscherNesterPart> parts;std::vector<size_t> order;int64_t fixedRight=0;
    size_t group(uintptr_t key)const{require(key>=1&&key<=input.parts.size(),PN2S_INTERNAL,"Escher nester source Part key changed");return size_t(key-1);}
    int64_t maximumRight()const{int64_t result=0;for(const auto &placement:state.output.placements){const auto &part=input.parts.at(placement.group);const auto box=bounds(transform(part.polygon,part.pivot,part.poses.at(placement.poseIndex)));result=std::max(result,placement.x+rounded(box.x1*1e6));}return result;}
    class Pattern final:public EscherPatternHost {
        OrdinaryEscherHost &host;size_t group;
    public:
        Pattern(OrdinaryEscherHost &source,size_t index):host(source),group(index){}
        int32_t available()override{return host.available(group+1);}
        std::optional<EscherPatternProvider> provider()override{
            auto &stored=host.providers[group];if(!stored){host.budget.check(true);const auto &owner=host.integerJob.parts[group];if(escherPositiveShapeContours(owner).size()!=1)return {};
                stored=std::make_unique<PatternProvider>(owner,host.program.contextSeconds<20.?0u:8u,host.integerJob.scale,host.input.parts[group].quantity,host.program.contextSeconds,host.budget);++host.trace.providers;
                // Both the NFP provider and the bound boundary placement-map
                // cache retain this complete normal owner, including holes.
                if(std::any_of(owner.normal.normalContours().begin(),owner.normal.normalContours().end(),
                    [](const auto &ring){return convolutionIntegerArea(ring)<0;}))++host.trace.boundaryHoleOwners;
                host.trace.toroidalCandidates+=stored->toroidal.candidates;host.trace.toroidalSolves+=stored->toroidal.solves;host.trace.toroidalImproved+=stored->toroidal.improved;host.budget.check(true);}
            return EscherPatternProvider{host.integerJob.scale,stored->cachedSpacing};
        }
        std::vector<Placement> build(const EscherPatternRequest &request)override{
            host.budget.check(true);++host.trace.patternRequests;auto &stored=*host.providers[group];EscherPreparedProviderHost prepared(stored.prepared.ordinary(),stored.prepared.restricted(),host.program.contextSeconds<20.?0u:8u,0,escherBoundaryLengthProvider(stored.boundary));
            EscherProviderRequest query;query.maximumCount=request.count;query.discard=request.discard;query.width=request.width;query.height=request.height;query.useCountCache=request.atLeftEdge;const auto layout=stored.provider.prepare(query,prepared);
            const auto &geometry=host.geometries[group];const auto &owner=host.integerJob.parts[group];EscherProviderConversionGeometry conversion;conversion.part=group+1;conversion.scale=host.integerJob.scale;conversion.originalBounds=geometry.original;conversion.symmetryX=geometry.symmetry.horizontal;conversion.symmetryY=geometry.symmetry.vertical;conversion.sourceReference=[&](uintptr_t identity){require(identity==uintptr_t(&owner),PN2S_INTERNAL,"Escher provider lost its stable integer source owner");return Point{owner.reference[0],owner.reference[1]};};conversion.variantBounds=[&](auto pose){return geometry.variantBounds(pose);};
            const auto members=escherConvertProviderLayout(layout.primary,conversion);std::vector<Placement> result;for(const auto &member:members){const auto placement=geometry.placement(group,member);if(!placement){++host.trace.rejectedPatterns;return {};}result.push_back(*placement);}host.trace.rawPatterns+=result.size();host.budget.check(true);return result;
        }
        int32_t commit(std::span<const Placement> placements,int64_t offset)override{
            host.budget.check(true);Output candidate=host.state.output;std::vector<size_t> availableInstances;const auto &catalog=*host.state.catalog;for(const auto instance:catalog.records.at(catalog.groups.at(group).front()).instances)if(!catalog.instances.at(instance).placed)availableInstances.push_back(catalog.instances.at(instance).ordinal);
            if(placements.size()>availableInstances.size()){++host.trace.rejectedPatterns;return 0;}for(size_t n=0;n<placements.size();++n){auto placement=placements[n];placement.ordinal=availableInstances[n];placement.x+=offset;candidate.placements.push_back(placement);}
            if(!valid_layout(host.input,candidate)){++host.trace.rejectedPatterns;return 0;}
            if(!validPointsProximity(host.input,candidate,host.input.pointGroups)){
                host.trace.missingProviders.insert("escher.pointProximityPatternSearch");
                ++host.trace.rejectedPatterns;return 0;
            }
            const auto first=host.state.output.placements.size();for(size_t n=first;n<candidate.placements.size();++n)ordinaryCommit(host.state,candidate.placements[n]);host.trace.committed+=placements.size();return int32_t(placements.size());
        }
    };
public:
    OrdinaryEscherHost(const Input &source,const EscherProgram &settings,Budget &time,OrdinaryState &target,OrdinaryEscherTrace &record)
        :input(source),program(settings),budget(time),state(target),trace(record),random(settings.random){
        require(ordinaryPreparedDomain(input),PN2S_UNSUPPORTED,"Escher ordinary host requires finite grid-aligned clean contours and zero spacing");require(std::isfinite(program.contextSeconds)&&program.contextSeconds>=0.,PN2S_INVALID,"Escher requires actual context run time");
        OrdinaryControl initial;initial.placementLimit=0;initial.values=program.evaluation.values;ordinaryRun(input,budget,state,&initial);std::vector<EscherIntegerPartInput> integerParts;geometries.reserve(input.parts.size());
        for(size_t index=0;index<input.parts.size();++index) {
            budget.check(true);const auto &part=input.parts[index];geometries.emplace_back(part);
            std::vector<SdkOrientation> orientations;for(const auto &pose:part.poses)orientations.push_back({int32_t(pose.flip),pose.angle,pose.upper});
            const auto grid=escherOrientationGrid(orientations);EscherIntegerPartInput integer;
            integer.shapeId=uint32_t(index+1);integer.quantity=uint64_t(part.quantity);integer.metadata0=grid.mirrored;integer.metadata4=grid.quantizedStep;
            for(const auto &ring:geometries.back().contours) {
                ConvolutionRing points;for(const auto point:ring)points.push_back({point.x,point.y});
                if(area(ring)<0.) {
                    // 568170 supplies positive hole paths to 7fa900; 7f8490
                    // reverses them only when publishing the signed owner.
                    std::reverse(points.begin(),points.end());integer.holes.push_back(std::move(points));
                } else integer.exteriors.push_back(std::move(points));
            }
            integerParts.push_back(std::move(integer));
            parts.push_back({index+1,part.quantity,false,geometries.back().materialArea,-1.,input.gap});
        }
        const EscherIntegerSheetInput sheet{uint32_t(input.parts.size()+1),1,{{{0,0},{input.width,0},{input.width,input.height},{0,input.height}}}};
        integerJob=escherConstructIntegerJob(integerParts,std::span<const EscherIntegerSheetInput>(&sheet,1));
        for(const auto &owner:integerJob.parts)for(const auto &ring:owner.normal.normalContours()) {
            if(convolutionIntegerArea(ring)<0)++trace.integerHoleContours;else ++trace.integerOuterContours;
        }
        providers.resize(input.parts.size());budget.check(true);
    }
    ~OrdinaryEscherHost()override{if(state.placementHost)state.placementHost->nesting.random=random;}
    int32_t available(uintptr_t key)override{const auto &catalog=*state.catalog;const auto &record=catalog.records.at(catalog.groups.at(group(key)).front());return int32_t(record.instances.size())-record.nestedCount;}
    void prepareGreedy(std::span<const uintptr_t> keys,int64_t front)override{order.clear();for(auto key:keys)order.push_back(group(key));fixedRight=front;trace.orderedParts+=keys.size();trace.partOrder=order;}
    double remainingTime()override{return std::chrono::duration<double>(budget.deadline-std::chrono::steady_clock::now()).count();}
    bool stopped()override{return false;}
    int32_t greedy()override{++trace.greedyCalls;OrdinaryControl control;control.values=program.evaluation.values;control.reference=program.evaluation.reference;control.keys=order;control.keysExplicit=true;control.fixedFront=fixedRight;control.extremaCount=3;control.extremaMaxSteps=100;control.directions={true,true,false,false};return ordinaryRun(input,budget,state,&control);}
    int32_t pattern(uintptr_t key,double gap)override{const auto index=group(key);EscherPatternInput request;request.sheetWidth=input.width;request.sheetHeight=input.height;request.gap=gap;request.geometryArea=geometries[index].materialArea;request.maxPlacedX=maximumRight();request.placedCount13d8=int32_t(state.output.placements.size());request.trial13bc=program.trial13bc;Pattern host(*this,index);return buildEscherPattern(request,random,host);}
    bool hasRemaining()override{for(size_t index=0;index<input.parts.size();++index)if(available(index+1)>0)return true;return false;}
    int32_t fallback()override{++trace.fallbackCalls;auto settings=program.fallback;settings.random=random;const auto placed=ordinaryOffsetContinue(input,settings,budget,state);random=state.placementHost->nesting.random;return placed;}
    int32_t run(){EscherNesterInput request{parts,program.trial13bc,maximumRight(),0.,program.fallback90};return escherOrdinaryNester(request,random,*this);}
};
}
EscherProgram escherProgram(const Input &input,double contextSeconds,recovered::OffsetFactoryInput factory,const recovered::Random *source){
    double totalArea=0;int32_t population=0;const auto parts=ordinaryValueParts(input,totalArea,population);factory.population88=population;factory.count=1;EscherProgram result;result.contextSeconds=contextSeconds;result.random=source?*source:recovered::Random(input.randomSeed);
    // 44f540 always creates a static primary evaluator. 44c8a0 separately
    // creates its one fallback evaluator using the live trial-dependent flag.
    auto primary=factory;primary.dynamicEvaluation=false;
    result.evaluation=recovered::offsetValue(result.random,primary,parts,totalArea);result.fallback=offsetProgram(input,factory,&result.random);result.random=result.fallback.random;return result;
}
int ordinaryEscherContinue(const Input &input,const EscherProgram &program,Budget &budget,OrdinaryState &state,OrdinaryEscherTrace *record){if(input.parts.empty())return 0;OrdinaryEscherTrace local;OrdinaryEscherHost host(input,program,budget,state,record?*record:local);return host.run();}
Output ordinaryEscher(const Input &input,const EscherProgram &program,Budget &budget,OrdinaryEscherTrace *trace){OrdinaryState state;ordinaryEscherContinue(input,program,budget,state,trace);return std::move(state.output);}
}
