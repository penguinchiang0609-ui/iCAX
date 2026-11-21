#include "ordinary_general_tiling.hpp"
#include "recovered_general_tiling_connected.hpp"
#include "recovered_escher_construct.hpp"
#include "recovered_rectangle_tiling.hpp"
#include "recovered_context.hpp"
#include "recovered_escher_toroidal.hpp"
#include <bit>

namespace study {
namespace {
using namespace recovered;
// anpn2license.dll 3.10.7.204562, table fc22f0. See the pinned defaults
// extraction and the actual492c50 time-dependent provider constructor.
constexpr double seed178=27.,templateLimit678=20.,layoutTrials688=0.,angleStep988=1.;
constexpr int32_t minimumParts998=10;
int32_t magnitude(PlacementOrientation pose){
    int32_t value=pose.angle<0?std::bit_cast<int32_t>(0u-uint32_t(pose.angle)):pose.angle;
    return value>=36000000?value-36000000:value;
}
struct PartGeometry {
    const PartInput &part;
    Budget &budget;
    ShapeAxisSymmetry symmetry;
    std::vector<SdkOrientation> sdk;
    std::vector<PlacementOrientation> modules;
    Box original;
    std::map<int32_t,std::shared_ptr<GeneralTilingVariant>> variants;
    explicit PartGeometry(const PartInput &source,Budget &time):part(source),budget(time){
        std::vector<Polygon> rings{part.polygon};
        rings.insert(rings.end(),part.holes.begin(),part.holes.end());
        symmetry=shapeAxisSymmetry(rings,bounds(part.polygon));
        for(const auto &pose:part.poses)sdk.push_back({int32_t(pose.flip),pose.angle,pose.upper});
        modules=partOrientationCache(sdk,true,symmetry.horizontal,symmetry.vertical);
        original=bounds(transform(part.polygon,part.pivot,{}));
    }
    std::shared_ptr<const GeneralTilingVariant> variant(PlacementOrientation requested,bool current=false){
        budget.check(true);
        requested=canonicalOrientation(requested,symmetry.horizontal,symmetry.vertical);
        if(current){
            const auto at=std::find_if(modules.begin(),modules.end(),[&](auto pose){return pose.angle==requested.angle;});
            if(at==modules.end())return {};
            requested=*at;
        }else if(!equivalentOrientation(modules,requested,symmetry.horizontal,symmetry.vertical).found)return {};
        auto &result=variants[requested.angle];
        if(!result){
            result=std::make_shared<GeneralTilingVariant>();result->orientation=requested;
            const Pose pose{double(magnitude(requested))/100000.,requested.angle<0};
            auto polygon=transform(part.polygon,part.pivot,pose);result->bounds=bounds(polygon);
            result->components.push_back({std::move(polygon),area(part.polygon)});
            for(const auto &hole:part.holes)result->components.push_back({transform(hole,part.pivot,pose),-std::abs(area(hole))});
        }
        return result;
    }
    std::optional<Placement> placement(size_t group,const TilingMember &member){
        const auto equivalents=equivalentOrientations(member.orientation,symmetry.horizontal,symmetry.vertical);
        for(size_t index=0;index<part.poses.size();++index){
            const auto &allowed=part.poses[index];
            for(const auto candidate:equivalents){
                if((candidate.angle<0)!=allowed.flip)continue;
                const double angle=double(magnitude(candidate))/100000.;
                const double turn=std::ceil((allowed.angle-angle-1e-7)/360.);
                const double actual=angle+turn*360.;
                if(actual<allowed.angle-1e-7||actual>allowed.upper+1e-7)continue;
                const auto correction=reverseVariantOffset({original.x0,original.y0,original.x1,original.y1},member.orientation,candidate);
                Placement result{group,0,index};
                result.exactX=member.position.x+correction.x;result.exactY=member.position.y+correction.y;
                result.solvedAngle=std::clamp(actual,allowed.angle,allowed.upper);
                result.x=rounded(result.exactX*1e6);result.y=rounded(result.exactY*1000.);return result;
            }
        }
        return {};
    }
};
std::optional<RectangleTilingModel> convert(const TilingPattern &pattern,PartGeometry &geometry,size_t group){
    if(pattern.members.empty())return {};
    RectangleTilingModel result;result.u=pattern.u;result.v=pattern.v;
    result.maximumX=pattern.maximumX;result.maximumY=pattern.maximumY;
    result.maximumTotal=pattern.maximumTotal;result.style=pattern.style;
    for(const auto &member:pattern.members){
        auto placement=geometry.placement(group,member);if(!placement)return {};
        result.members.push_back(*placement);
    }
    return result;
}
}
std::vector<RectangleTilingModel> ordinaryGeneralTilingModelsForInput(
    const Input &input,Budget &budget,double contextSeconds,OrdinaryGeneralTilingTrace *trace){
    require(std::isfinite(contextSeconds)&&contextSeconds>=0.,PN2S_INVALID,"General tiling needs actual context run time");
    if(trace)*trace={};
    const uint32_t candidateLimit=contextSeconds<20.?0u:8u;
    const int32_t refinement=int32_t(std::min(std::trunc(contextSeconds/29.),3.));
    if(trace){trace->candidateLimit=candidateLimit;trace->toroidalRefinementLevel=refinement;}
    std::vector<RectangleTilingModel> result;
    if(input.parts.empty())return result;
    budget.check(true);
    const double stockArea=input.width*input.height;
    std::vector<RectangleTilingBranch> branches;
    for(const auto &part:input.parts){
        budget.check(true);const auto box=bounds(part.polygon);RectangleTilingBranchInput gate;
        gate.gap=input.gap;gate.width=box.x1-box.x0;gate.height=box.y1-box.y0;gate.area=material_area(part);
        gate.totalStockArea=gate.maximumStockArea=stockArea;gate.required=part.quantity;
        gate.minimumParts=minimumParts998;gate.protectedGeometry=part.protection>0.;branches.push_back(rectangleTilingBranch(gate));
    }
    if(std::none_of(branches.begin(),branches.end(),[](auto branch){return branch.general;}))return result;
    std::vector<std::unique_ptr<PartGeometry>> geometries;
    std::vector<EscherIntegerPartInput> integerParts;
    geometries.reserve(input.parts.size());integerParts.reserve(input.parts.size());
    for(size_t group=0;group<input.parts.size();++group){
        budget.check(true);const auto &part=input.parts[group];
        auto geometry=std::make_unique<PartGeometry>(part,budget);
        const auto grid=escherOrientationGrid(geometry->sdk);
        EscherIntegerPartInput integer;integer.shapeId=uint32_t(group+1);integer.quantity=uint64_t(part.quantity);
        integer.metadata0=grid.mirrored;integer.metadata4=grid.quantizedStep;integer.protection=part.protection;
        integer.exteriors.resize(1);
        for(const auto p:part.polygon)integer.exteriors.front().push_back({p.x-part.pivot.x,p.y-part.pivot.y});
        for(const auto &hole:part.holes){
            auto ring=hole;
            // Integer owner construction receives positive semantic hole paths
            // and reverses them when publishing its signed contour topology.
            if(area(ring)<0.)std::reverse(ring.begin(),ring.end());
            integer.holes.emplace_back();for(const auto p:ring)integer.holes.back().push_back({p.x-part.pivot.x,p.y-part.pivot.y});
        }
        integerParts.push_back(std::move(integer));geometries.push_back(std::move(geometry));
    }
    const EscherIntegerSheetInput sheet{uint32_t(input.parts.size()+1),1,{{{0.,0.},{input.width,0.},{input.width,input.height},{0.,input.height}}}};
    auto integerJob=escherConstructIntegerJob(integerParts,std::span<const EscherIntegerSheetInput>(&sheet,1));
    budget.check(true);
    for(size_t group=0;group<input.parts.size();++group){
        const auto &part=input.parts[group];auto &geometry=*geometries[group];
        if(part.quantity<=0)continue;
        const auto branch=branches[group];if(!branch.general)continue;
        if(trace)++trace->generalGroups;
        GeneralTilingAmoSource source;source.part=reinterpret_cast<uintptr_t>(&part);
        source.sheets={{{input.width,input.height},0,false}};source.orientations=geometry.modules;
        source.symmetryX=geometry.symmetry.horizontal;source.symmetryY=geometry.symmetry.vertical;
        source.required=part.quantity;source.gap=input.gap;source.angleStep988=angleStep988;source.originalBounds=geometry.original;
        source.variant=[&geometry](auto pose){return geometry.variant(pose);};
        source.currentVariant=[&geometry](auto pose){return geometry.variant(pose,true);};
        source.moduleOrientations=[&geometry]{geometry.budget.check(true);return geometry.modules;};
        source.check=[&budget]{budget.check(true);};
        GeneralTilingTemplateGeometry conversion;conversion.required=part.quantity;
        conversion.conversion.part=source.part;conversion.conversion.scale=integerJob.scale;
        conversion.conversion.originalBounds=geometry.original;conversion.conversion.symmetryX=source.symmetryX;
        conversion.conversion.symmetryY=source.symmetryY;
        const auto &owner=integerJob.parts.at(group);
        conversion.conversion.sourceReference=[&owner](uintptr_t pointer){
            require(pointer==reinterpret_cast<uintptr_t>(&owner),PN2S_INTERNAL,"General tiling integer owner identity changed");
            return Point{owner.reference[0],owner.reference[1]};
        };
        conversion.conversion.variantBounds=[&geometry](auto pose){const auto variant=geometry.variant(pose);require(bool(variant),PN2S_INTERNAL,"General tiling conversion used a disallowed pose");return variant->bounds;};
        std::unique_ptr<GeneralTilingUpperSupplemental> supplemental;
        //3879e0 is lazy, after54ec10's zero-module gate and the initial Amo
        //production; absent zero modules must not eagerly build an NFP catalog.
        const auto acquire=[&]()->GeneralTilingUpperSupplemental *{
            if(supplemental)return supplemental.get();
            budget.check(true);
            if(escherPositiveShapeContours(owner).size()!=1)return nullptr;
            if(trace)++trace->providerGroups;
            supplemental=std::make_unique<GeneralTilingUpperSupplemental>(owner,conversion,candidateLimit,part.quantity,false,seed178,source.check);
            EscherToroidalTrace toroidal;
            supplemental->improveToroidal(refinement,budget,&toroidal);
            if(trace){trace->toroidalCandidates+=toroidal.candidates;trace->toroidalSolves+=toroidal.solves;trace->toroidalImproved+=toroidal.improved;}
            budget.check(true);return supplemental.get();
        };
        budget.check(true);
        GeneralTilingConnectedControlHost host(source,nullptr,[&budget]{budget.check(true);return false;},acquire);
        GeneralTilingControlInput control;control.sheet={input.width,input.height};control.orientations=source.orientations;
        control.symmetryX=source.symmetryX;control.symmetryY=source.symmetryY;control.expand=branch.extensive;
        control.value=material_area(part);control.gap=input.gap;control.templateLimit678=templateLimit678;
        control.layoutTrials688=layoutTrials688;control.seed178=seed178;
        GeneralTilingControlState state;generalTilingProduce(state,control,host);budget.check(true);
        for(const auto &pattern:state.models)if(auto model=convert(*pattern,geometry,group)){
            result.push_back(std::move(*model));if(trace)++trace->models;
        }
        for(const auto &candidate:state.expanded){
            auto expanded=expandTilingCandidate(candidate,&budget);TilingPattern pattern;pattern.style=candidate.pattern.style;
            for(const auto &item:expanded.members){auto member=candidate.pattern.members.at(item.member);
                const Point delta{item.position.x-member.position.x,item.position.y-member.position.y};member.position=item.position;
                member.box.x0+=delta.x;member.box.x1+=delta.x;member.box.y0+=delta.y;member.box.y1+=delta.y;pattern.members.push_back(member);}
            if(auto model=convert(pattern,geometry,group)){
                const auto box=rectangleModelBounds(input,*model);model->u={box.x1-box.x0+input.gap,0.};model->v={0.,box.y1-box.y0+input.gap};
                model->maximumX=model->maximumY=1;result.push_back(std::move(*model));if(trace)++trace->expanded;
            }
        }
    }
    return result;
}
}
