#include "recovered_escher_boundary_geometry.hpp"
#include <bit>
#include <random>
#include <list>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int64_t truncate(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int64_t roundInteger(double value){return truncate(value<0.?value-.5:value+.5);}
int32_t normalizeAngle(int64_t value){auto angle=value%3600000;if(angle<0)angle+=3600000;return int32_t(angle);}
}
EscherIntegerPoint escherBoundaryScaleVector(EscherIntegerPoint value,double scale){
    return {roundInteger(double(value[0])*scale),roundInteger(double(value[1])*scale)};
}
ConvolutionIntegerPolygon escherBoundaryMapBasisTransform(const ConvolutionIntegerPolygon &source,
    EscherIntegerPoint first,EscherIntegerPoint second){
    auto result=source;
    const double normFirst=std::sqrt(double(first[1])*double(first[1])+double(first[0])*double(first[0]));
    const double normSecond=std::sqrt(double(second[1])*double(second[1])+double(second[0])*double(second[0]));
    const auto determinant=sub(mul(first[0],second[1]),mul(second[0],first[1]));
    const double inverse=1./double(determinant);
    const double xy=(double(sub(0,second[0]))*normFirst)*inverse,xx=(double(second[1])*normFirst)*inverse;
    const double yx=(double(sub(0,first[1]))*normSecond)*inverse,yy=(normSecond*double(first[0]))*inverse;
    const auto convert=[](double value){const auto rounded=std::round(value);return rounded>=-0x1p63&&rounded<=0x1p63?truncate(rounded):int64_t(0);};
    for(auto &ring:result)for(auto &point:ring){const auto y=convert(double(point[1])*yy+double(point[0])*yx);
        const auto x=convert(double(point[1])*xy+double(point[0])*xx);point={x,y};}
    return result;
}
void escherBoundaryMapBasisRestore(ConvolutionIntegerPolygon &polygon,EscherIntegerPoint first,EscherIntegerPoint second){
    const double inverseFirst=1./std::sqrt(double(first[1])*double(first[1])+double(first[0])*double(first[0]));
    const double inverseSecond=1./std::sqrt(double(second[1])*double(second[1])+double(second[0])*double(second[0]));
    for(auto &ring:polygon)for(auto &point:ring){
        // Each wrapped basis product is rounded independently before addition.
        const auto firstX=roundInteger(double(mul(point[0],first[0]))*inverseFirst),firstY=roundInteger(double(mul(point[0],first[1]))*inverseFirst);
        const auto secondX=roundInteger(double(mul(point[1],second[0]))*inverseSecond),secondY=roundInteger(double(mul(point[1],second[1]))*inverseSecond);
        point={add(firstX,secondX),add(firstY,secondY)};
    }
}
std::vector<ConvolutionIntegerSegment> escherBoundaryGridLines(int32_t first,int32_t last,int64_t period,int64_t lower,int64_t upper){
    std::vector<ConvolutionIntegerSegment> result;
    for(int64_t index=first;index<=int64_t(last);++index){const auto x=mul(index,period);result.push_back({EscherIntegerPoint{x,lower},EscherIntegerPoint{x,upper}});}
    return result;
}
std::vector<ConvolutionIntegerSegment> escherBoundaryGridSplit(std::span<const ConvolutionIntegerSegment> input,
    std::span<const ConvolutionIntegerSegment> cutters){
    std::vector<ConvolutionIntegerSegment> all(input.begin(),input.end());all.insert(all.end(),cutters.begin(),cutters.end());
    const auto split=convolutionSplitIntegerSegments(all);std::vector<ConvolutionIntegerSegment> result;
    for(const auto &entry:split)if(uint64_t(entry.source)<input.size()){
        auto segment=entry.segment;const auto &original=input[size_t(entry.source)];
        const auto dot=add(mul(sub(segment[1][1],segment[0][1]),sub(original[1][1],original[0][1])),
            mul(sub(original[1][0],original[0][0]),sub(segment[1][0],segment[0][0])));
        if(dot<0)std::swap(segment[0],segment[1]);result.push_back(segment);
    }
    return result;
}
std::vector<ConvolutionIntegerSegment> escherBoundaryCancelEdges(std::span<const ConvolutionIntegerSegment> input){
    std::vector<ConvolutionIntegerSegment> canonical;std::map<ConvolutionIntegerSegment,int32_t> counts;
    for(auto edge:input){if(edge[0]==edge[1])continue;const bool forward=edge[0]>edge[1];if(!forward)std::swap(edge[0],edge[1]);canonical.push_back(edge);
        auto &value=counts[edge];value=std::bit_cast<int32_t>(uint32_t(value)+(forward?1U:UINT32_MAX));}
    std::vector<ConvolutionIntegerSegment> result;
    for(auto edge:convolutionIntegerEdgeOrder(canonical)){auto amount=counts.at(edge);if(amount<0){amount=std::bit_cast<int32_t>(0U-uint32_t(amount));std::swap(edge[0],edge[1]);}
        for(int32_t index=0;index<amount;++index)result.push_back(edge);}
    return result;
}
EscherBoundaryCellGroups escherBoundaryCollectCells(std::span<const ConvolutionIntegerSegment> edges,int64_t width,int64_t height){
    require(width>0&&height>0,PN2S_INVALID,"Escher boundary cells require positive grid dimensions");
    const auto coordinate=[&](int64_t sum,int64_t period){const auto magnitude=sum<0?sub(0,sum):sum;
        const auto q=std::bit_cast<int32_t>(uint32_t(sub(add(mul(magnitude,period),sum)/period,magnitude)));
        const auto absolute=q<0?-int64_t(q):int64_t(q);
        return int32_t((int64_t(q)+absolute*2)/2-absolute);};
    const auto mix=[](uint64_t value){value=(value^(value>>32))*0xe9846af9b1a615dULL;value=(value^(value>>32))*0xe9846af9b1a615dULL;return value^(value>>28);};
    const auto hash=[&](std::array<int32_t,2> point){return mix(mix(uint64_t(int64_t(point[0]))+0x9e3779b9)+0x9e3779b9+uint64_t(int64_t(point[1])));};
    EscherBoundaryCellGroups groups;std::map<std::array<int32_t,2>,size_t> indices;std::list<size_t> order;
    using Iterator=std::list<size_t>::iterator;std::vector<Iterator> buckets(8,order.end());
    const auto rehash=[&](size_t capacity){buckets.assign(capacity,order.end());for(auto current=order.begin();current!=order.end();){const auto next=std::next(current);auto &first=buckets[hash(groups.cells[*current])&(capacity-1)];if(first!=order.end())order.splice(first,order,current);first=current;current=next;}};
    for(const auto &edge:edges){const std::array<int32_t,2> cell{coordinate(add(edge[0][0],edge[1][0]),width),coordinate(add(edge[0][1],edge[1][1]),height)};
        auto found=indices.find(cell);size_t index;
        if(found!=indices.end())index=found->second;
        else {
            const auto required=size_t(std::ceil(float(groups.cells.size()+1)));if(buckets.size()<required){size_t desired=std::max<size_t>(8,required);if(buckets.size()<512)desired=std::max(desired,buckets.size()*8);rehash(std::bit_ceil(desired));}
            index=groups.cells.size();groups.cells.push_back(cell);groups.segments.emplace_back();indices.emplace(cell,index);auto &first=buckets[hash(cell)&(buckets.size()-1)];first=order.insert(first,index);
        }
        groups.segments[index].push_back(edge);
    }
    EscherBoundaryCellGroups result;for(const auto index:order){result.cells.push_back(groups.cells[index]);result.segments.push_back(std::move(groups.segments[index]));}return result;
}
bool escherBoundaryPointContains(const ConvolutionIntegerPolygon &polygon,EscherIntegerPoint point){
    bool inside=false;
    for(const auto &ring:polygon){
        require(!ring.empty(),PN2S_INVALID,"Escher boundary membership requires nonempty rings");
        auto previous=ring.back();
        for(const auto next:ring){
            if((point[1]<previous[1])!=(point[1]<next[1])){
                const int64_t dy=sub(next[1],previous[1]),sign=(dy>0)-(dy<0);
                const auto left=mul(mul(sub(point[1],previous[1]),sub(next[0],previous[0])),sign);
                const auto right=mul(mul(sub(point[0],previous[0]),sign),dy);
                if(left>right)inside=!inside;
            }
            previous=next;
        }
    }
    return inside;
}
std::vector<EscherPatternRecord> escherExpandBoundaryCoefficients(std::span<const EscherPatternRecord> records,
    EscherIntegerPoint horizontal,EscherIntegerPoint step,std::span<const std::vector<int32_t>> xs,
    std::span<const std::vector<int32_t>> ys,EscherIntegerPoint origin){
    require(records.size()==xs.size()&&records.size()==ys.size(),PN2S_INVALID,"Escher boundary coefficient member count mismatch");
    std::vector<EscherPatternRecord> result;size_t amount=0;for(const auto &coefficients:xs)amount+=coefficients.size();result.reserve(amount);
    for(size_t member=0;member<records.size();++member){
        require(xs[member].size()==ys[member].size(),PN2S_INVALID,"Escher boundary X/Y coefficient count mismatch");
        const auto &source=records[member];const auto baseX=add(source.x,origin[0]),baseY=add(source.y,origin[1]);
        for(size_t index=0;index<xs[member].size();++index){auto record=source;
            record.x=add(add(mul(ys[member][index],step[0]),mul(xs[member][index],horizontal[0])),baseX);
            record.y=add(add(mul(ys[member][index],step[1]),baseY),mul(xs[member][index],horizontal[1]));result.push_back(record);}
    }
    return result;
}
EscherPatternLayout escherBoundaryLayoutFromCoefficients(std::span<const EscherPatternRecord> records,
    const EscherPeriodCandidate &candidate,std::span<const std::vector<int32_t>> xs,
    std::span<const std::vector<int32_t>> ys,EscherIntegerPoint origin,std::span<const EscherRecordGeometry> geometries){
    EscherPatternLayout result;if(candidate.width<1)return result;
    const auto &u=candidate.basis[0];const double radians=std::atan2(double(u[0])*0.-double(u[1]),double(u[1])*0.+double(u[0]));
    const auto angle=normalizeAngle(roundInteger((radians*57.29577951308232)*10000.));
    result.secondary=escherRotateRecords(records,angle,geometries);
    result.primary=escherExpandBoundaryCoefficients(result.secondary,{candidate.width,0},candidate.step,xs,ys,origin);
    result.basis={candidate.width,0,candidate.step[0],candidate.step[1]};result.bounds=escherLayoutBounds(result.primary);return result;
}
EscherBoundarySampleSelection escherSampleBoundaryRegions(EscherIntegerPoint horizontal,EscherIntegerPoint step,
    int32_t attempts,std::span<const EscherWeightedBoundaryRegion> regions){
    require(regions.size()<=INT32_MAX,PN2S_INVALID,"Escher weighted region count exceeds the original index range");
    std::mt19937 random(0x1d986);EscherBoundarySampleSelection result;
    const auto sample=[&](){const uint64_t low=uint32_t(random())>>11,high=uint32_t(random());
        return (double((high<<21)+low)*0x1p-53)*std::bit_cast<double>(uint64_t(0x3feffe5c91d14e3c))+std::bit_cast<double>(uint64_t(0x3f1a36e2eb1c432d));};
    for(int32_t trial=0;trial<attempts;++trial){const double first=sample(),second=sample();
        EscherIntegerPoint origin{truncate(double(step[0])*first+double(horizontal[0])*second),
            truncate(double(horizontal[1])*second+double(step[1])*first)};
        std::vector<int32_t> indices;double weight=0.;
        for(size_t index=0;index<regions.size();++index)if(escherBoundaryPointContains(regions[index].polygon,origin)){indices.push_back(int32_t(index));weight+=regions[index].weight;}
        if(result.weight<weight){result.origin=origin;result.indices=std::move(indices);result.weight=weight;}
    }
    return result;
}
}
