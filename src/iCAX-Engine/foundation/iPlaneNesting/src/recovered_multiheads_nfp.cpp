#include "recovered_multiheads.hpp"
#include "recovered_escher_nfp.hpp"
#include <bit>
namespace study::recovered {
ShakerPoint multiheadsCachedOrientationBasis(PlacementOrientation pose){
    auto magnitude=pose.angle<0?std::bit_cast<int32_t>(0u-uint32_t(pose.angle)):pose.angle;
    if(magnitude>=36000000)magnitude=std::bit_cast<int32_t>(uint32_t(magnitude)-36000000u);
    const double converted=(double(magnitude)/100000.)*10000.;auto angle=truncateInt32(converted<0?converted-.5:converted+.5)%3600000;
    if(angle<0)angle+=3600000;ConvolutionRing points{{0,10}};convolutionRotatePolygon(points,angle);
    if(pose.angle<0)points[0][1]*=-1.;return points[0];
}
std::vector<MultiheadsClusterPitchSource> multiheadsProducePitchSources(const ConvolutionPolygon &selfNfp,
    std::span<const ShakerPoint> directions,std::span<const double> heights,double sheetHeight,Budget *budget){
    require(directions.size()==heights.size(),PN2S_INVALID,"Multiheads cached orientation basis and source-height counts differ");
    if(directions.empty()||std::any_of(heights.begin(),heights.end(),[](double height){return height<0;}))return {};
    require(!selfNfp.empty()&&!selfNfp.front().empty(),PN2S_INVALID,"Multiheads NFP must contain an actual nonempty boundary");
    if(budget)budget->check(true);
    const ConvolutionPolygon zero{ConvolutionRing{{0,0}}};
    ConvolutionMultiPolygon nfp{zero,selfNfp},origins{zero};
    for(const auto direction:directions)origins.push_back({ConvolutionRing{direction}});
    std::vector<ConvolutionMultiPolygon> operands{std::move(nfp),std::move(origins)};
    const auto scale=convolutionScaleConfig(operands,INT32_MAX,false);
    const auto integers=convolutionQuantizeOperands(operands,scale);
    const auto reference=integers.at(0).at(0).at(0).at(0),origin=integers.at(1).at(0).at(0).at(0);
    const auto boundary=escherBuildNfpBoundary(reference,integers.at(0).at(1));
    std::vector<MultiheadsClusterPitchSource> output;output.reserve(directions.size());
    for(size_t n=0;n<directions.size();++n){if(budget)budget->check();const auto point=integers.at(1).at(n+1).at(0).at(0);
        const ConvolutionIntegerPoint direction{std::bit_cast<int64_t>(uint64_t(point[0])-uint64_t(origin[0])),std::bit_cast<int64_t>(uint64_t(point[1])-uint64_t(origin[1]))};
        auto hits=escherNfpRayIntersections(boundary,direction);escherPrepareNfpRayHits(direction,reference,0,0,hits,true);
        const auto occupied=escherNfpPeriodIntervals(hits);MultiheadsClusterPitchSource source;source.shapeHeight=heights[n];
        if(!occupied.empty()){const auto gaps=multiheadsPitchGaps(occupied);const auto segments=multiheadsIntegerPitchSegments(direction,gaps);
            for(const auto &segment:segments){std::array<double,4> unscaled;for(size_t endpoint=0;endpoint<2;++endpoint)for(size_t axis=0;axis<2;++axis)
                    unscaled[2*endpoint+axis]=double(segment[2*endpoint+axis])/scale.factors[axis]+scale.references[0][axis];
                const double first=std::sqrt(unscaled[1]*unscaled[1]+unscaled[0]*unscaled[0]),last=std::sqrt(unscaled[3]*unscaled[3]+unscaled[2]*unscaled[2]);
                source.intervals.push_back({first,last});}
            if(!source.intervals.empty())source.intervals.back()[1]=sheetHeight+sheetHeight;
        }
        output.push_back(std::move(source));
    }
    if(budget)budget->check(true);return output;
}
} // namespace study::recovered
