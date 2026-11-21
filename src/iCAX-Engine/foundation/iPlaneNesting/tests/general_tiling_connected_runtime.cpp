#include "recovered_general_tiling_connected.hpp"
#include "recovered_escher_construct.hpp"
#include "clipper2/clipper.h"
#include <iostream>
using namespace study;using namespace study::recovered;
namespace {
struct Geometry {
    Polygon ring;
    std::map<std::array<int32_t,3>,std::shared_ptr<GeneralTilingVariant>> cache;
    std::shared_ptr<const GeneralTilingVariant> variant(PlacementOrientation pose){const std::array key{pose.angle,pose.field4,pose.field8};auto &result=cache[key];if(result)return result;
        result=std::make_shared<GeneralTilingVariant>();result->orientation=pose;const auto matrix=reverseOrientationMatrix(pose);GeneralTilingComponent component;component.signedArea=1.;result->bounds={INFINITY,INFINITY,-INFINITY,-INFINITY};
        for(const auto p:ring){const Point point{p.x*matrix[1]-p.y*matrix[2],(p.y*matrix[1]+p.x*matrix[2])*matrix[3]};component.points.push_back(point);auto &b=result->bounds;b.x0=std::min(b.x0,point.x);b.y0=std::min(b.y0,point.y);b.x1=std::max(b.x1,point.x);b.y1=std::max(b.y1,point.y);}result->components.push_back(std::move(component));return result;
    }
};
double overlap(const TilingPattern &pattern,Geometry &geometry){std::vector<Clipper2Lib::PathD> placed;double area=0.;for(const auto &member:pattern.members){Clipper2Lib::PathD path;for(const auto p:geometry.variant(member.orientation)->components.front().points)path.push_back({p.x+member.position.x,p.y+member.position.y});for(const auto &other:placed)area=std::max(area,std::fabs(Clipper2Lib::Area(Clipper2Lib::Intersect({path},{other},Clipper2Lib::FillRule::NonZero,8))));placed.push_back(std::move(path));}return area;}
}
int main(){try{size_t scenes=0,models=0,supplementalAdded=0;double maximumOverlap=0.;
    for(size_t trial=0;trial<2;++trial){Geometry geometry;geometry.ring=trial?Polygon{{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}}:Polygon{{0,0},{5,0},{5,3},{0,3}};
        EscherIntegerPartInput part;part.shapeId=uint32_t(trial+1);part.quantity=20;part.metadata4=1800000;part.exteriors.resize(1);for(const auto p:geometry.ring)part.exteriors.front().push_back({p.x,p.y});const EscherIntegerSheetInput sheet{99,1,{{{0,0},{30,0},{30,20},{0,20}}}};
        auto job=escherConstructIntegerJob(std::span<const EscherIntegerPartInput>(&part,1),std::span<const EscherIntegerSheetInput>(&sheet,1));const auto &owner=job.parts.front();
        GeneralTilingAmoSource source;source.part=trial+1;source.sheets={{{30,20},0,false}};source.required=20;source.orientations={{0,0,0},{18000000,0,0}};source.variant=[&](PlacementOrientation pose){return geometry.variant(pose);};source.moduleOrientations=[&]{return source.orientations;};source.originalBounds=geometry.variant({})->bounds;
        GeneralTilingTemplateGeometry conversion;conversion.required=20;conversion.conversion.part=source.part;conversion.conversion.scale=job.scale;conversion.conversion.originalBounds=source.originalBounds;conversion.conversion.variantBounds=[&](PlacementOrientation pose){return geometry.variant(pose)->bounds;};conversion.conversion.sourceReference=[&](uintptr_t pointer){require(pointer==reinterpret_cast<uintptr_t>(&owner),PN2S_INTERNAL,"Connected provider changed integer owner");return Point{owner.reference[0],owner.reference[1]};};
        GeneralTilingUpperSupplemental supplemental(owner,conversion,2,20,false,17.);const auto before=supplemental.catalog().ordinary.size();GeneralTilingConnectedControlHost host(source,&supplemental,[]{return false;});
        GeneralTilingControlInput input;input.sheet={30,20};input.orientations=source.orientations;input.expand=true;input.value=1.;input.templateLimit678=4.;input.layoutTrials688=2.;input.seed178=17.;GeneralTilingControlState state;generalTilingProduce(state,input,host);
        require(!state.models.empty(),PN2S_INTERNAL,"Connected4a0800 produced no models");const auto added=supplemental.catalog().ordinary.size()-before;require(added>0,PN2S_INTERNAL,"Connected494a20 did not append an eligible real Amo pattern");
        size_t explicitLayouts=0;for(const auto &pattern:state.models){require(bool(pattern)&&!pattern->members.empty(),PN2S_INTERNAL,"Connected controller retained an empty model");if(pattern->members.size()>2){++explicitLayouts;const auto area=overlap(*pattern,geometry);maximumOverlap=std::max(maximumOverlap,area);require(area<=.005,PN2S_INTERNAL,"Connected actual provider layout overlaps beyond native rounding");}}
        require(explicitLayouts>0,PN2S_INTERNAL,"Connected controller did not retain an actual provider layout");
        std::cout<<"{\"scene\":"<<trial<<",\"models\":"<<state.models.size()<<",\"addedOrdinaryDescriptors\":"<<added<<",\"explicitLayouts\":"<<explicitLayouts<<",\"expandedCandidates\":"<<state.expanded.size()<<"}\n";
        ++scenes;models+=state.models.size();supplementalAdded+=added;
    }
    std::cout<<"{\"pass\":true,\"scenes\":"<<scenes<<",\"models\":"<<models<<",\"addedOrdinaryDescriptors\":"<<supplementalAdded<<",\"maximumRoundedOverlapArea\":"<<maximumOverlap<<"}\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
