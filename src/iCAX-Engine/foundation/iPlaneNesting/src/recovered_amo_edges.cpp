#include "recovered_amo_tiling.hpp"
#include "recovered_rich_traversal.hpp"
namespace study::recovered {
std::vector<double> amoTilingShapeEdgeAngles(const AmoTilingShape &shape) {
    std::vector<std::array<double,2>> edges;
    for(const auto &ring:shape.rings)if(!ring.empty())for(size_t i=0;i<ring.size();++i) {
        const auto first=ring[i],last=ring[(i+1)%ring.size()];
        const auto segment=richStraightSegment({first.x,first.y},{last.x,last.y});
        const auto d=segment.direction;const double norm=std::sqrt(d[1]*d[1]+d[0]*d[0]);
        const double x=norm!=0?d[0]/norm:1.,y=norm!=0?d[1]/norm:0.,sign=segment.length>0?1.:-1.;
        double angle=(std::atan2(y*sign,x*sign)*180.)/3.141592653589793;
        if(180.<angle)angle+=-180.;
        edges.push_back({segment.length,angle});
    }
    std::sort(edges.begin(),edges.end(),[](const auto &a,const auto &b){return b<a;});
    std::set<double> seen;std::vector<double> angles;
    for(const auto &edge:edges) {
        double angle=edge[1];for(;angle<0.;angle+=360.){}for(;360.<angle;angle+=-360.){}
        const auto found=seen.lower_bound(angle-.001);if(found!=seen.end()&&*found<=angle+.001)continue;
        seen.insert(angle);if(angle<.001)seen.insert(angle+360.);
        angles.push_back(angle);if(angles.size()>4)break;
    }
    return angles;
}
}
