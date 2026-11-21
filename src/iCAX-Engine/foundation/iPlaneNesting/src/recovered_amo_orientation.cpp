#include "recovered_amo_orientation.hpp"
namespace study::recovered {
namespace {
int32_t truncate32(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
int64_t truncate64(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
Point quantized(Point value){const double x=value.x*1000000.,y=value.y*1000.;return {double(truncate64(x<0?x-.5:x+.5))/1000000.,double(truncate32(y<0?y-.5:y+.5))/1000.};}
}
std::vector<TilingPattern> amoTilingConvertDescriptors(std::span<const AmoTilingDescriptor> descriptors,const AmoTilingOrientationGeometry &geometry) {
    require(bool(geometry.variantBounds),PN2S_INVALID,"Amo orientation conversion requires actual Part variant bounds");
    std::vector<TilingPattern> patterns;patterns.reserve(descriptors.size());
    const std::array<double,4> bounds{geometry.originalBounds.x0,geometry.originalBounds.y0,geometry.originalBounds.x1,geometry.originalBounds.y1};
    for(const auto &descriptor:descriptors) {
        const double angle=descriptor.angle*100000.;const int32_t turn=truncate32(angle<0?angle-.5:angle+.5);
        std::array<PlacementOrientation,2> requested,actual;std::array<Point,2> corrections;
        for(size_t member=0;member<2;++member){requested[member]=composeOrientation(geometry.base[member],turn,false);actual[member]=canonicalOrientation(requested[member],geometry.symmetryX,geometry.symmetryY);corrections[member]=reverseVariantOffset(bounds,requested[member],actual[member]);}
        const Point position{(descriptor.translation.x-corrections[0].x)+corrections[1].x,(descriptor.translation.y-corrections[0].y)+corrections[1].y};
        TilingPattern pattern;pattern.u={std::fabs(descriptor.period.x),0.};pattern.v={0.,std::fabs(descriptor.period.y)};pattern.style=1;pattern.maximumX=geometry.maximumX;pattern.maximumY=geometry.maximumY;
        for(auto &matrix:pattern.commonCuts)matrix.assign(2,std::vector<double>(2,0.));
        for(size_t member=0;member<2;++member){const auto placed=quantized(member?position:Point{});auto box=geometry.variantBounds(actual[member]);box.x0+=placed.x;box.y0+=placed.y;box.x1+=placed.x;box.y1+=placed.y;pattern.members.push_back({geometry.part,actual[member],placed,box,geometry.required});}
        patterns.push_back(std::move(pattern));
    }
    return patterns;
}
}
