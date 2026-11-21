#include "recovered_general_tiling_supplemental.hpp"
#include "recovered_escher_construct.hpp"
#include "clipper2/clipper.h"
#include <iostream>
using namespace study;using namespace study::recovered;
namespace {
Box variantBounds(const Polygon &ring,PlacementOrientation pose){
    const auto matrix=reverseOrientationMatrix(pose);Box box{INFINITY,INFINITY,-INFINITY,-INFINITY};
    for(const auto p:ring){const Point q{p.x*matrix[1]-p.y*matrix[2],(p.y*matrix[1]+p.x*matrix[2])*matrix[3]};box.x0=std::min(box.x0,q.x);box.y0=std::min(box.y0,q.y);box.x1=std::max(box.x1,q.x);box.y1=std::max(box.y1,q.y);}return box;
}
Clipper2Lib::PathsD placed(const std::vector<Polygon> &rings,const TilingMember &member){
    const auto matrix=reverseOrientationMatrix(member.orientation);Clipper2Lib::PathsD paths;
    for(const auto &ring:rings){Clipper2Lib::PathD path;for(const auto p:ring)path.push_back({(p.x*matrix[1]-p.y*matrix[2])+member.position.x,((p.y*matrix[1]+p.x*matrix[2])*matrix[3])+member.position.y});paths.push_back(std::move(path));}return paths;
}
}
int main(){try{size_t scenes=0,totalPlacements=0,totalTemplates=0;double maximumOverlap=0.;
    for(size_t trial=0;trial<4;++trial){
        const Polygon outer=trial%2?Polygon{{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}}:Polygon{{0,0},{5,0},{5,3},{0,3}};
        std::vector<Polygon> rings{outer};if(trial==2)rings.push_back({{1,1},{1,2},{2,2},{2,1}});
        EscherIntegerPartInput input;input.shapeId=uint32_t(trial+1);input.quantity=20;input.metadata4=1800000;
        for(const auto p:outer) {if(input.exteriors.empty())input.exteriors.emplace_back();input.exteriors.front().push_back({p.x,p.y});}
        for(size_t n=1;n<rings.size();++n){ConvolutionRing hole;for(const auto p:rings[n])hole.push_back({p.x,p.y});input.holes.push_back(std::move(hole));}
        const EscherIntegerSheetInput sheet{99,1,{{{0,0},{30,0},{30,20},{0,20}}}};
        auto job=escherConstructIntegerJob(std::span<const EscherIntegerPartInput>(&input,1),std::span<const EscherIntegerSheetInput>(&sheet,1));
        const auto &owner=job.parts.front();auto catalog=escherProduceGeometryCatalog(owner,2);GeneralTilingSupplementalCatalog bound(catalog);
        require(!bound.ordinary().empty(),PN2S_INTERNAL,"Actual Escher geometry catalog has no ordinary descriptor");
        EscherPreparedProviderHost host(bound.ordinary(),bound.restricted(),2);EscherProvider provider(job.scale,20);
        GeneralTilingTemplateGeometry geometry;geometry.required=20;geometry.conversion.part=trial+1;geometry.conversion.scale=job.scale;geometry.conversion.originalBounds=variantBounds(outer,{});
        geometry.conversion.sourceReference=[&](uintptr_t id){require(id==reinterpret_cast<uintptr_t>(&owner),PN2S_INTERNAL,"Actual supplemental record lost its integer owner");return Point{owner.reference[0],owner.reference[1]};};
        geometry.conversion.variantBounds=[&](PlacementOrientation pose){return variantBounds(outer,pose);};
        GeneralTilingSupplementalProvider supplemental(provider,host,geometry,17.);
        const auto templates=supplemental.templates(4,{30.,20.});require(!templates.empty(),PN2S_INTERNAL,"Actual supplemental templates returned no mono/bimodule");
        for(const auto &pattern:templates)require(pattern->members.size()==1||pattern->members.size()==2,PN2S_INTERNAL,"Supplemental template is not mono/bimodule");
        EscherProviderRequest request;request.width=30.;request.height=20.;request.useCountCache=true;const auto layout=supplemental.layout(request);
        require(!layout.empty()&&layout.size()<=20,PN2S_INTERNAL,"Actual supplemental layout violates requested quantity");
        std::vector<Clipper2Lib::PathsD> placedParts;double overlap=0.;Box envelope{INFINITY,INFINITY,-INFINITY,-INFINITY};
        for(const auto &member:layout){const auto paths=placed(rings,member);for(const auto &prior:placedParts){const auto intersection=Clipper2Lib::Intersect(paths,prior,Clipper2Lib::FillRule::NonZero,8);overlap=std::max(overlap,std::fabs(Clipper2Lib::Area(intersection)));}placedParts.push_back(paths);envelope.x0=std::min(envelope.x0,member.box.x0);envelope.y0=std::min(envelope.y0,member.box.y0);envelope.x1=std::max(envelope.x1,member.box.x1);envelope.y1=std::max(envelope.y1,member.box.y1);}
        require(overlap<=.005,PN2S_INTERNAL,"Actual supplemental layout overlap exceeds native quantization allowance");
        require(envelope.x0>=-.002&&envelope.y0>=-.002&&envelope.x1<=30.002&&envelope.y1<=20.002,PN2S_INTERNAL,"Actual supplemental layout leaves its requested stock");
        std::cout<<"{\"scene\":"<<trial<<",\"ordinary\":"<<bound.ordinary().size()<<",\"restricted\":"<<bound.restricted().size()<<",\"templates\":"<<templates.size()<<",\"placements\":"<<layout.size()<<",\"maximumRoundedOverlapArea\":"<<overlap<<",\"envelope\":["<<envelope.x1-envelope.x0<<','<<envelope.y1-envelope.y0<<"]}\n";
        ++scenes;totalPlacements+=layout.size();totalTemplates+=templates.size();maximumOverlap=std::max(maximumOverlap,overlap);
    }
    std::cout<<"{\"pass\":true,\"scenes\":"<<scenes<<",\"placements\":"<<totalPlacements<<",\"templates\":"<<totalTemplates<<",\"maximumRoundedOverlapArea\":"<<maximumOverlap<<"}\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
