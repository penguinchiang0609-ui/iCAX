#include "recovered_escher_producer.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int64_t area(const EscherPeriodCandidate &candidate){const auto &u=candidate.basis[0],&v=candidate.basis[1];const auto value=sub(mul(u[0],v[1]),mul(u[1],v[0]));return value<0?sub(0,value):value;}
double norm(EscherIntegerPoint point){const double x=double(point[0]),y=double(point[1]);return std::sqrt(y*y+x*x);}
bool permitted(EscherIntegerPoint point,const EscherDirectionRowOptions &options){
    if(!options.restricted)return true;const auto angle=(options.stepDegrees%360)*10000;const auto normalized=angle<0?angle+3600000:angle;
    const double step=(double(normalized)/10000.)*.017453292519943295;
    const double bearing=std::atan2(double(point[0])*0.-double(point[1]),double(point[1])*0.+double(point[0]));
    return std::fabs(std::fmod(std::fabs(bearing)+.01,std::fabs(step))-.01)<.001;
}
}
EscherCandidateBuildOptions escherDefaultCandidateBuildOptions(int32_t restriction,bool adaptivePeriod){
    EscherCandidateBuildOptions options;options.rows.worldRelativeRanges={{-10,10},{-46,-44},{44,46}};options.vertexRelativeRanges={{-60,60}};
    options.rows.density=1;options.primaryBeginDegrees=-90;options.primaryEndDegrees=90;options.rows.stepDegrees=1;options.primaryDenominator=2;options.vertexLimit=10;options.vertexAngleThreshold=135.;options.areaFilterFraction=.2;options.rows.areaFactor=.01;options.discardFraction=.3;options.adaptivePeriod=adaptivePeriod;options.rows.restricted=restriction/10000!=0;
    if(restriction>0){options.primaryBeginDegrees=0;options.primaryEndDegrees=359;const int32_t degrees=restriction/10000;options.rows.stepDegrees=degrees>179?90:degrees;options.primaryDenominator=1;options.rows.density=2;std::swap(options.rows.worldRelativeRanges,options.vertexRelativeRanges);}return options;
}
void escherSortPeriodicCandidatesByArea(std::vector<EscherPeriodCandidate> &candidates){
    require(candidates.size()<=INT32_MAX,PN2S_INVALID,"Escher candidate count exceeds the original index range");
    std::vector<EscherCandidateAreaIndex> indices;indices.reserve(candidates.size());for(size_t index=0;index<candidates.size();++index)indices.push_back({area(candidates[index]),int32_t(index),0});escherSortCandidateAreas(indices);
    std::vector<EscherPeriodCandidate> ordered;ordered.reserve(candidates.size());for(const auto &entry:indices)ordered.push_back(std::move(candidates[size_t(entry.index)]));candidates=std::move(ordered);
}
size_t escherPrunePeriodicCandidatesByArea(std::vector<EscherPeriodCandidate> &candidates,double threshold){return std::erase_if(candidates,[&](const auto &candidate){return threshold<double(area(candidate));});}
void escherAppendSwappedCatalogs(EscherCandidateLists &catalogs,std::span<const EscherSupportTable> tables){
    for(auto *catalog:{&catalogs.ordinary,&catalogs.special}){std::vector<EscherPeriodCandidate> swapped;swapped.reserve(catalog->size());for(const auto &candidate:*catalog)if(candidate.width>0)swapped.push_back(escherSwapCandidateBasis(candidate,tables));catalog->insert(catalog->end(),std::make_move_iterator(swapped.begin()),std::make_move_iterator(swapped.end()));}
}
void escherReducePeriodicCandidateDirections(std::vector<EscherPeriodCandidate> &candidates,double tolerance){
    escherSortCandidateSlopes(candidates);const double threshold=1/std::tan(tolerance);std::vector<EscherPeriodCandidate> output;output.reserve(candidates.size());
    for(size_t first=0;first<candidates.size();){size_t last=first+1;
        while(last<candidates.size()&&escherCandidateSlopeNear(candidates[first].basisSlope,candidates[last].basisSlope,candidates[first].basis[0],candidates[last].basis[0],threshold)&&escherCandidateSlopeNear(candidates[first].stepSlope,candidates[last].stepSlope,candidates[first].step,candidates[last].step,threshold)){
            if(norm(candidates[first].basis[0])!=norm(candidates[last].basis[0]))break;++last;
        }
        size_t selected=first;for(size_t index=first+1;index<last;++index)if(norm(candidates[index].step)<norm(candidates[selected].step))selected=index;
        output.push_back(std::move(candidates[selected]));first=last;
    }
    candidates=std::move(output);escherSortPeriodicCandidatesByArea(candidates);
}
EscherCandidateLists escherBuildPeriodicCandidates(std::span<const EscherSupportTable> tables,const ConvolutionIntegerPolygon &rings,
    const EscherCandidateBuildOptions &options,int64_t &minimumCellArea){
    EscherCandidateLists output;if(rings.empty())return output;
    require(!tables.empty()&&tables.size()<=2&&options.primaryDenominator>0&&options.rows.stepDegrees>0&&options.rows.density>0,PN2S_INVALID,"Escher candidate sampling requires one or two prepared members and positive angle steps");
    require(options.discardFraction>=0&&options.discardFraction<=1,PN2S_INVALID,"Escher candidate discard fraction is outside zero to one");
    ConvolutionIntegerBox box{INT64_MAX,INT64_MAX,INT64_MIN,INT64_MIN};std::vector<EscherIntegerPoint> points;
    for(const auto &ring:rings){require(!ring.empty(),PN2S_INVALID,"Escher NFP contains an empty ring");for(auto point:ring){points.push_back(point);for(size_t axis=0;axis<2;++axis){box[axis]=std::min(box[axis],point[axis]);box[axis+2]=std::max(box[axis+2],point[axis]);}}}
    const double dx=double(sub(box[0],box[2])),dy=double(sub(box[1],box[3]));const double maximumLength=std::sqrt(dy*dy+dx*dx)+10;
    const auto boundary=escherBuildNfpBoundary({0,0},rings);ConvolutionEdgeIndex index;index.build(rings);
    auto vertices=escherPolygonDirections(rings,{0,0},options.vertexLimit,options.vertexAngleThreshold);
    auto primary=escherPrimaryDirections(boundary,options.primaryBeginDegrees,options.primaryEndDegrees,options.rows.stepDegrees,options.primaryDenominator,maximumLength);
    EscherSupportTable aggregate;if(options.adaptivePeriod){escherPeriodEndpointDirections(boundary,primary);escherPeriodEndpointDirections(boundary,vertices);aggregate=escherBuildSupportTable(std::move(points));}
    const auto support=options.adaptivePeriod?&aggregate:nullptr;auto rowOptions=options.rows;rowOptions.maximumLength=maximumLength;
    escherDirectionRows(tables,boundary,index,primary,rowOptions.worldRelativeRanges,rowOptions,output,minimumCellArea,support);
    escherDirectionRows(tables,boundary,index,vertices,options.vertexRelativeRanges,rowOptions,output,minimumCellArea,support);
    for(const auto *directions:{&primary,&vertices})for(const auto period:*directions)if(permitted(period,rowOptions)){
        auto intersections=escherNfpIntersectionDirections(index,period);std::sort(intersections.begin(),intersections.end());intersections.erase(std::unique(intersections.begin(),intersections.end()),intersections.end());
        escherAppendNfpCandidates(tables,boundary,index,period,intersections,output,minimumCellArea,support);
    }
    escherSortPeriodicCandidatesByArea(output.ordinary);
    if(output.ordinary.size()>1000){const auto first=area(output.ordinary.front()),last=area(output.ordinary.back());escherPrunePeriodicCandidatesByArea(output.ordinary,double(sub(last,first))*options.areaFilterFraction+double(first));}
    const auto retained=size_t((1-options.discardFraction)*double(output.ordinary.size()));output.ordinary.resize(retained);
    escherAppendSwappedCatalogs(output,tables);for(auto *catalog:{&output.ordinary,&output.special})for(auto &candidate:*catalog)escherNormalizeCandidatePhase(candidate);
    escherReducePeriodicCandidateDirections(output.ordinary);output.ordinary.insert(output.ordinary.end(),output.special.begin(),output.special.end());return output;
}
}
