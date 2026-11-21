#include "recovered_part_geometry.hpp"
#include "recovered_engine.hpp"
#include <bit>
namespace study::recovered {
Polygon partRemoveAdjacentPoints(std::span<const Point> ring){
    Polygon result;if(ring.empty())return result;result.push_back(ring.front());
    for(const auto point:ring)if(point.x!=result.back().x||point.y!=result.back().y)result.push_back(point);
    if(result.size()>1&&result.back().x==result.front().x&&result.back().y==result.front().y)result.pop_back();return result;
}
int32_t partRingTurnChanges(std::span<const Point> ring){
    if(ring.size()<4)return 0;int first=0,previous=0;uint32_t changes=0;
    for(size_t n=0;n<ring.size();++n){const auto a=ring[n],b=ring[(n+1)%ring.size()],c=ring[(n+2)%ring.size()];
        const double cross=(c.y-a.y)*(b.x-a.x)-(b.y-a.y)*(c.x-a.x);const int sign=(0.<cross)-(cross<0.);
        if(sign){if(!first)first=sign;else if(previous!=sign)++changes;previous=sign;}
    }
    if(previous!=first)++changes;return std::bit_cast<int32_t>(changes);
}
PartGeometryStatistics partGeometryStatistics(std::span<const Polygon> rings){
    PartGeometryStatistics result;uint32_t total=0,exteriors=0,holes=0;
    for(const auto &ring:rings){const auto changes=partRingTurnChanges(ring);result.points+=ring.size();total+=uint32_t(changes);result.weightedTurnChanges+=uint64_t(ring.size())*uint64_t(int64_t(changes));
        double signedArea=0.;if(!ring.empty()){auto previous=ring.back();for(const auto point:ring){const double term=(point.y*previous.x-point.x*previous.y)*.5;signedArea=term+signedArea;previous=point;}}
        if(signedArea<0.)++holes;else ++exteriors;
    }
    result.turnChanges=std::bit_cast<int32_t>(total);result.exteriors=std::bit_cast<int32_t>(exteriors);result.holes=std::bit_cast<int32_t>(holes);return result;
}
bool partGeometryTooComplex(const PartGeometryStatistics &input,double maximum){return input.weightedTurnChanges>uint64_t(int64_t(truncateInt32(maximum)));}
std::vector<Polygon> partNormalizeSingleRing(std::span<const Polygon> rings){
    require(rings.size()<=1,PN2S_UNSUPPORTED,"Multiple-ring Part normalization requires the original juxtaposition provider");
    std::vector<Polygon> result;for(const auto &ring:rings)if(ring.size()>2)result.push_back(partRemoveAdjacentPoints(ring));return result;
}
}
