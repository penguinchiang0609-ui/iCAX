#include "recovered_general_tiling_amo.hpp"
#include "clipper2/clipper.h"
#include <iostream>
using namespace study;using namespace study::recovered;
namespace {
struct SourceGeometry {
    std::vector<Polygon> rings;
    std::map<std::array<int32_t,3>,std::shared_ptr<GeneralTilingVariant>> cache;
    std::shared_ptr<const GeneralTilingVariant> variant(PlacementOrientation pose){
        const std::array key{pose.angle,pose.field4,pose.field8};auto &value=cache[key];if(value)return value;
        value=std::make_shared<GeneralTilingVariant>();value->orientation=pose;
        int32_t angle=pose.angle<0?-pose.angle:pose.angle;if(angle>=36000000)angle-=36000000;
        const double radians=double(angle)/100000.*pi/180.,c=std::cos(radians),s=std::sin(radians);
        AmoTilingShape shape;
        for(const auto &ring:rings){GeneralTilingComponent component;component.signedArea=1.;
            for(auto p:ring){if(pose.angle<0)p.x=-p.x;component.points.push_back({p.x*c-p.y*s,p.x*s+p.y*c});}
            shape.rings.push_back(component.points);value->components.push_back(std::move(component));}
        value->bounds=amoTilingShapeBounds(shape).box;return value;
    }
};
double pointSegment(Point p,Point a,Point b){const double x=b.x-a.x,y=b.y-a.y,den=x*x+y*y;const double t=den==0?0:std::clamp(((p.x-a.x)*x+(p.y-a.y)*y)/den,0.,1.);return std::hypot(p.x-(a.x+t*x),p.y-(a.y+t*y));}
double clearance(const Clipper2Lib::PathsD &a,const Clipper2Lib::PathsD &b){double result=INFINITY;for(const auto &pa:a)for(const auto &pb:b)for(size_t i=0;i<pa.size();++i)for(size_t j=0;j<pb.size();++j){const auto p=pa[i],q=pa[(i+1)%pa.size()],r=pb[j],s=pb[(j+1)%pb.size()];result=std::min(result,pointSegment({p.x,p.y},{r.x,r.y},{s.x,s.y}));result=std::min(result,pointSegment({r.x,r.y},{p.x,p.y},{q.x,q.y}));}return result;}
}
int main(){try{
    size_t scenes=0,placements=0;double maximumOverlap=0,minimumGapError=0;
    for(size_t trial=0;trial<6;++trial){SourceGeometry geometry;
        if(trial<2)geometry.rings={{{0,0},{5,0},{5,3},{0,3}}};
        else if(trial<5)geometry.rings={{{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}}};
        else geometry.rings={{{0,0},{4,0},{4,2},{0,2}},{{0,3},{2,3},{2,5},{0,5}}};
        GeneralTilingAmoSource source;source.part=trial+1;source.required=20;source.gap=trial%2?.5:0.;source.sheets={{{30,20,.125,.25,.375,.5},0,false}};
        source.orientations=trial==4?std::vector<PlacementOrientation>{{0,36000000,0}}:std::vector<PlacementOrientation>{{0,0,0},{18000000,0,0}};
        source.variant=[&](PlacementOrientation o){return geometry.variant(o);};source.moduleOrientations=[&]{return source.orientations;};
        AmoTilingShape raw;raw.rings=geometry.rings;source.originalBounds=amoTilingShapeBounds(raw).box;
        const auto second=trial==2?PlacementOrientation{18000000,0,0}:PlacementOrientation{};
        auto results=generalTilingOrientationPatterns(source,{},second);require(!results.empty(),PN2S_INTERNAL,"Actual upper Amo production returned no model");
        std::vector<TilingPattern> patterns;std::vector<TilingRepetitionCounts> repetitions;
        for(const auto &result:results){patterns.push_back(*result.pattern);const auto &d=result.descriptor;TilingRepetitionCounts count;count.commonX=d.repeatX;count.commonY=d.repeatY;for(size_t member=0;member<2;++member){count.extraX[member]=d.extra[member][0];count.extraY[member]=d.extra[member][1];}repetitions.push_back(count);}
        const auto candidate=selectGeneralTilingRepetition(patterns,repetitions);const auto layout=expandTilingCandidate(candidate);const auto b=tilingCandidateBounds(candidate);
        require(!layout.members.empty()&&layout.members.size()<=20,PN2S_INTERNAL,"Actual upper Amo quantity is outside demand");
        const auto dimensions=generalTilingAmoSheetExtents(source.sheets);require(b.x1-b.x0<=dimensions.x+.002&&b.y1-b.y0<=dimensions.y+.002,PN2S_INTERNAL,"Actual upper Amo stock envelope exceeds its actual sheet catalog");
        std::vector<Clipper2Lib::PathsD> placed;double overlap=0,minimum=INFINITY;
        for(const auto &item:layout.members){Clipper2Lib::PathsD paths;const auto &member=candidate.pattern.members[item.member];for(const auto &ring:geometry.variant(member.orientation)->components){Clipper2Lib::PathD path;for(const auto point:ring.points)path.push_back({point.x+item.position.x,point.y+item.position.y});paths.push_back(std::move(path));}
            for(const auto &prior:placed){const auto intersection=Clipper2Lib::Intersect(paths,prior,Clipper2Lib::FillRule::NonZero,8);overlap=std::max(overlap,std::fabs(Clipper2Lib::Area(intersection)));minimum=std::min(minimum,clearance(paths,prior));}placed.push_back(std::move(paths));}
        require(overlap<=.01,PN2S_INTERNAL,"Actual upper Amo has material overlap beyond original coordinate rounding");
        if(source.gap>0.)require(minimum>=source.gap-.002,PN2S_INTERNAL,"Actual upper Amo input offset did not preserve spacing");
        std::cout<<"{\"scene\":"<<trial<<",\"components\":"<<geometry.rings.size()<<",\"continuousCatalog\":"<<(trial==4)<<",\"gap\":"<<source.gap<<",\"models\":"<<results.size()<<",\"placements\":"<<layout.members.size()<<",\"envelope\":["<<b.x1-b.x0<<','<<b.y1-b.y0<<"],\"maximumRoundedOverlapArea\":"<<overlap<<",\"minimumMeasuredClearance\":"<<minimum<<"}\n";
        ++scenes;placements+=layout.members.size();maximumOverlap=std::max(maximumOverlap,overlap);minimumGapError=std::min(minimumGapError,minimum-source.gap);
    }
    std::cout<<"{\"pass\":true,\"scenes\":"<<scenes<<",\"placements\":"<<placements<<",\"maximumRoundedOverlapArea\":"<<maximumOverlap<<",\"minimumGapError\":"<<minimumGapError<<"}\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
