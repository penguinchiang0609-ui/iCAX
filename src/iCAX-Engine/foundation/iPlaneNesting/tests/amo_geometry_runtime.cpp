#include "recovered_amo_producer.hpp"
#include "clipper2/clipper.h"
#include <iostream>
using namespace study;using namespace study::recovered;
namespace {
Clipper2Lib::PathD transformed(const Polygon &source,double degrees,Point translation){
    const double radians=degrees*pi/180.,c=std::cos(radians),s=std::sin(radians);Clipper2Lib::PathD path;
    for(const auto p:source)path.push_back({p.x*c-p.y*s+translation.x,p.x*s+p.y*c+translation.y});return path;
}
Clipper2Lib::PathD placed(const Polygon &source,const TilingPattern &pattern,const TilingExpandedMember &member){
    const auto orientation=pattern.members.at(member.member).orientation;
    require(orientation.field4==0&&orientation.field8==0,PN2S_INTERNAL,"Amo geometric acceptance expects unreflected input variants");
    const double radians=(double(orientation.angle)/100000.)*pi/180.,c=std::cos(radians),s=std::sin(radians);
    Clipper2Lib::PathD path;for(const auto p:source)path.push_back({p.x*c-p.y*s+member.position.x,p.x*s+p.y*c+member.position.y});return path;
}
}
int main(){try{
    const std::array<Polygon,3> polygons{{{{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}},{{0,0},{6,0},{0,4}},{{0,0},{6,0},{6,1.5},{2,1.5},{2,4.5},{6,4.5},{6,6},{0,6}}}};
    size_t scenes=0,total=0;double maximumOverlap=0,maximumRawOverlap=0;
    for(size_t shape=0;shape<polygons.size();++shape)for(size_t mode=0;mode<3;++mode){
        AmoTilingJob job;for(auto &member:job.members){member.rings={polygons[shape]};member.bounds=amoTilingShapeBounds(member);}
        AmoTilingSolveRequest request;request.criterion={24,24,0,0,0};request.angles=mode==0?AmoTilingAngleRequest{0,0,0,false}:mode==1?AmoTilingAngleRequest{13.375,13.375,0,false}:AmoTilingAngleRequest{0,45,0,true};
        AmoTilingOrientationGeometry geometry;geometry.part=shape+1;geometry.required=20;geometry.originalBounds=bounds(polygons[shape]);
        geometry.variantBounds=[&](PlacementOrientation orientation){return amoTilingShapeBounds(job.members[0],double(orientation.angle)/100000.).box;};
        auto results=amoTilingPrepareOrientationPatterns(job,request,20,geometry);
        require(!results.empty(),PN2S_INTERNAL,"Actual Amo geometry must produce polygon pattern results");
        double rawOverlap=0;
        for(const auto &result:results){const auto &d=result.descriptor;std::vector<Clipper2Lib::PathD> raw;
            for(int x=0;x<4;++x)for(int y=0;y<4;++y)for(int member=0;member<2;++member){
                const Point p{std::fabs(d.period.x)*x+(member?d.translation.x:0.),std::fabs(d.period.y)*y+(member?d.translation.y:0.)};
                auto path=transformed(polygons[shape],d.angle,p);for(const auto &other:raw){auto intersection=Clipper2Lib::Intersect(Clipper2Lib::PathsD{path},Clipper2Lib::PathsD{other},Clipper2Lib::FillRule::NonZero,8);rawOverlap=std::max(rawOverlap,std::fabs(Clipper2Lib::Area(intersection)));}raw.push_back(std::move(path));
            }
        }
        require(rawOverlap<=1e-6,PN2S_INTERNAL,"Unquantized Amo periodic geometry has interior overlap");
        std::vector<TilingPattern> patterns;std::vector<TilingRepetitionCounts> repetitions;
        for(const auto &result:results){patterns.push_back(*result.pattern);const auto &d=result.descriptor;TilingRepetitionCounts r;r.commonX=d.repeatX;r.commonY=d.repeatY;for(size_t n=0;n<2;++n){r.extraX[n]=d.extra[n][0];r.extraY[n]=d.extra[n][1];}repetitions.push_back(r);}
        const auto candidate=selectGeneralTilingRepetition(patterns,repetitions);const auto expanded=expandTilingCandidate(candidate);const auto box=tilingCandidateBounds(candidate);
        require(!expanded.members.empty()&&expanded.members.size()<=20,PN2S_INTERNAL,"Amo polygon output respects its quantity limit");
        require(box.x1-box.x0<=24.+.001&&box.y1-box.y0<=24.+.001,PN2S_INTERNAL,"Amo polygon pattern respects stock dimensions");
        std::vector<Clipper2Lib::PathD> paths;double overlap=0;
        for(const auto &member:expanded.members){auto path=placed(polygons[shape],candidate.pattern,member);for(const auto &other:paths){auto intersection=Clipper2Lib::Intersect(Clipper2Lib::PathsD{path},Clipper2Lib::PathsD{other},Clipper2Lib::FillRule::NonZero,8);overlap=std::max(overlap,std::fabs(Clipper2Lib::Area(intersection)));}paths.push_back(std::move(path));}
        std::cout<<"{\"shape\":"<<shape<<",\"angleMode\":"<<mode<<",\"placements\":"<<expanded.members.size()<<",\"width\":"<<box.x1-box.x0<<",\"height\":"<<box.y1-box.y0<<",\"unquantizedOverlapArea\":"<<rawOverlap<<",\"providerRoundedOverlapArea\":"<<overlap<<"}\n";
        maximumOverlap=std::max(maximumOverlap,overlap);maximumRawOverlap=std::max(maximumRawOverlap,rawOverlap);++scenes;total+=expanded.members.size();
        // Provider coordinates have the original X 1e-6 / Y 1e-3 precision.
        // Report any material intersection rather than accepting pair count alone.
        require(overlap<=.01,PN2S_INTERNAL,"Amo polygon output has material interior overlap beyond coordinate rounding");
    }
    std::cout<<"{\"pass\":true,\"scenes\":"<<scenes<<",\"placements\":"<<total<<",\"unquantizedOverlapArea\":"<<maximumRawOverlap<<",\"providerRoundedOverlapArea\":"<<maximumOverlap<<",\"providerCoordinateTolerance\":0.001}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
