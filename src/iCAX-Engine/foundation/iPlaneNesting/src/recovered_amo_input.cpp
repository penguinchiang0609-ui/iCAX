#include "recovered_amo_input.hpp"
#include "recovered_sdk_points.hpp"
#include <bit>
namespace study::recovered {
AmoTilingTransformedShape amoTilingCreateInputShape(std::span<const Point> ring,double halfDistance,bool field58){
    require(!ring.empty(),PN2S_INVALID,"Amo input component requires its first point");
    AmoTilingTransformedShape result;result.geometry.rings.emplace_back(ring.begin(),ring.end());
    result.properties.field50=std::bit_cast<uint64_t>(halfDistance);result.properties.field58=field58;
    std::vector<ShakerPoint> points;points.reserve(ring.size());for(const auto p:ring)points.push_back({p.x,p.y});
    const auto intersections=sdkPointSelfIntersections(points);
    if(!intersections.empty()){
        const auto point=intersections.front().point;result.properties.flaggedPoint={point[0],point[1]};result.properties.flag5a=1;return result;
    }
    if(halfDistance!=0.)result.geometry.rings.front()=amoTilingOffsetInputRing(ring,halfDistance,field58);
    const auto &finalized=result.geometry.rings.front();result.properties.flag59=finalized.empty();
    for(const auto point:finalized){auto &bounds=result.geometry.bounds;
        if(bounds.empty){bounds.empty=false;bounds.box={point.x,point.y,point.x,point.y};}
        else {if(point.x<bounds.box.x0)bounds.box.x0=point.x;if(point.y<bounds.box.y0)bounds.box.y0=point.y;
            if(bounds.box.x1<point.x)bounds.box.x1=point.x;if(bounds.box.y1<point.y)bounds.box.y1=point.y;}}
    return result;
}
}
