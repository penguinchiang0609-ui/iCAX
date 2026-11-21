#include "recovered_module_geometry.hpp"
#include <immintrin.h>
namespace study::recovered {
std::vector<Polygon> moduleTransformContours(std::span<const Polygon> source,PlacementOrientation pose,
    Point reference,bool reverseMirrored){
    const auto matrix=reverseOrientationMatrix(pose,reference);std::vector<Polygon> result(source.begin(),source.end());
    for(auto &ring:result){for(auto &point:ring){
        const double u=point.x-matrix[4],v=point.y-matrix[5];
        const double x=matrix[1]*u-matrix[2]*v;
        const double y=(matrix[1]*v+matrix[2]*u)*matrix[3];
        point={x+matrix[4],y+matrix[5]};
    }if(reverseMirrored&&pose.angle<0)std::reverse(ring.begin(),ring.end());}
    return result;
}
std::span<const Polygon> PreparedPartModuleGeometry::layer(bool slot2,int32_t index)const{
    require(index==standardLayers[0]||index==standardLayers[1],PN2S_INVALID,"PartModule layer is absent from the standard layer list");
    const bool first=index==standardLayers[0];return first==slot2?std::span<const Polygon>(primary):std::span<const Polygon>(alternate);
}
PreparedPartModuleGeometry preparedPartModuleGeometry(std::span<const Polygon> primary,
    std::optional<std::span<const Polygon>> alternate,PlacementOrientation pose,std::array<int32_t,2> layers){
    PreparedPartModuleGeometry result;result.orientation=pose;result.standardLayers=layers;
    result.primary=moduleTransformContours(primary,pose);result.alternate=moduleTransformContours(alternate.value_or(primary),pose);
    bool first=true;
    for(const auto &ring:result.primary)for(const auto point:ring){
        if(first){result.bounds={point.x,point.y,point.x,point.y};first=false;}
        else {if(point.x<result.bounds.x0)result.bounds.x0=point.x;if(point.y<result.bounds.y0)result.bounds.y0=point.y;if(point.x>result.bounds.x1)result.bounds.x1=point.x;if(point.y>result.bounds.y1)result.bounds.y1=point.y;}
    }
    const auto quantize=[](Point point){const auto y=point.y*1000.;return std::array<int64_t,2>{rounded(point.x*1000000.),_mm_cvttsd_si32(_mm_set_sd(y<0.?y-.5:y+.5))};};
    result.minimumOffset=quantize({result.bounds.x0,result.bounds.y0});result.maximumOffset=quantize({result.bounds.x1,result.bounds.y1});return result;
}
}
