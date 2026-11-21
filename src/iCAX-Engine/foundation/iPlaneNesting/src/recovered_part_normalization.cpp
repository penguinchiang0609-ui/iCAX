#include "recovered_part_normalization.hpp"
#include "recovered_convolution_polygon.hpp"
#include "recovered_escher_normalization.hpp"
namespace study::recovered {
bool partContoursHaveRepeatedVertices(std::span<const Polygon> rings){
    struct Less {bool operator()(Point a,Point b)const{return a.x<b.x||(a.x==b.x&&a.y<b.y);}};
    std::set<Point,Less> points;for(const auto &ring:rings)for(const auto point:partRemoveAdjacentPoints(ring))if(!points.insert(point).second)return true;return false;
}
bool partContoursAreClean(std::span<const Polygon> rings){
    if(rings.empty())return true;ConvolutionPolygon polygon;
    for(const auto &ring:rings){require(ring.size()>=3,PN2S_UNSUPPORTED,"Part multi-ring cleanliness requires complete finite rings");auto &output=polygon.emplace_back();
        for(const auto point:ring){require(std::isfinite(point.x)&&std::isfinite(point.y),PN2S_UNSUPPORTED,"Part cleanliness requires finite double coordinates");output.push_back({point.x,point.y});}}
    std::vector<ConvolutionMultiPolygon> operands{{polygon}};const auto scale=convolutionScaleConfig(operands,INT32_MAX,false);
    const auto quantized=convolutionQuantizeOperands(operands,scale);
    if(escherIntegerRingsNeedUnion(quantized.at(0).at(0)))return false;
    return !partContoursHaveRepeatedVertices(rings);
}
std::vector<Polygon> partNormalizeCleanContours(std::span<const Polygon> rings){
    if(rings.size()<=1)return partNormalizeSingleRing(rings);
    require(partContoursAreClean(rings),PN2S_UNSUPPORTED,"Non-clean multiple-ring Part geometry requires original rich Boolean normalization");
    std::vector<Polygon> result;for(const auto &ring:rings)if(ring.size()>2)result.push_back(partRemoveAdjacentPoints(ring));return result;
}
PreparedPartModuleGeometry cleanPartModuleGeometry(std::span<const Polygon> primary,std::optional<std::span<const Polygon>> alternate,PlacementOrientation orientation,std::array<int32_t,2> layers){
    const auto normalized=partNormalizeCleanContours(primary);
    if(alternate){const auto second=partNormalizeCleanContours(*alternate);return preparedPartModuleGeometry(normalized,std::span<const Polygon>(second),orientation,layers);}
    return preparedPartModuleGeometry(normalized,std::nullopt,orientation,layers);
}
}
