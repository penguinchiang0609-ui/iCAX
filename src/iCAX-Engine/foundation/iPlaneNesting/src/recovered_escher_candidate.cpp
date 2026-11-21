#include "recovered_escher_candidate.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int64_t cross(EscherIntegerPoint a,EscherIntegerPoint b){return sub(mul(a[0],b[1]),mul(a[1],b[0]));}
int64_t roundInteger(double value){value=value<0?value-.5:value+.5;return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int32_t truncate32(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
double squared(EscherIntegerPoint p){const double x=double(p[0]),y=double(p[1]);return y*y+x*x;}
}
std::array<EscherIntegerPoint,2> escherTrialSupportExtrema(std::span<const EscherSupportRecord> table,EscherIntegerPoint &direction) {
    require(!table.empty(),PN2S_INVALID,"Escher trial support table is empty");
    if(direction[0]<0)for(auto &coordinate:direction)coordinate=sub(0,coordinate);
    size_t first=0,count=table.size();while(count){const auto half=count/2,middle=first+half;
        if(sub(mul(direction[1],table[middle].direction[0]),mul(direction[0],table[middle].direction[1]))>0){first=middle+1;count-=half+1;}else count=half;}
    if(first==table.size())first=0;return {table[first].first,table[first].second};
}
void escherAppendNfpCandidates(std::span<const EscherSupportTable> tables,const EscherNfpBoundary &boundary,const ConvolutionEdgeIndex &index,
    EscherIntegerPoint period,std::span<const EscherIntegerPoint> directions,EscherCandidateLists &output,int64_t &minimumArea,
    const EscherSupportTable *aggregateSupport) {
    require(!tables.empty(),PN2S_INVALID,"Escher candidate needs at least one member support table");
    const auto boxes=escherCandidateBoxes(tables,period);std::vector<ConvolutionHierarchyRayHit> hits;
    for(auto direction:directions) {
        const double normSquared=squared(direction);if(normSquared>1e18){const double scale=1e18/normSquared;direction={roundInteger(scale*double(direction[0])),roundInteger(scale*double(direction[1]))};}
        int32_t minimumTrials=0;
        if(aggregateSupport){auto supportDirection=direction;const auto extrema=escherTrialSupportExtrema(*aggregateSupport,supportDirection);
            const int64_t first=sub(mul(extrema[0][1],direction[0]),mul(extrema[0][0],direction[1]));
            const int64_t second=sub(mul(extrema[1][1],direction[0]),mul(extrema[1][0],direction[1]));
            const int64_t denominator=cross(direction,period);
            minimumTrials=truncate32(std::ceil(std::fabs(double(std::min(first,second))/double(denominator))));
        }
        auto searchPeriod=period;EscherIntegerPoint endpoint{};
        if(escherSearchNfpPeriod(boundary,index,searchPeriod,direction,hits,minimumTrials)){endpoint=escherNfpPeriodEndpoint(hits);if(std::sqrt(squared(endpoint))<3)endpoint={0,0};}
        if(endpoint==EscherIntegerPoint{0,0})continue;
        const int64_t dot=std::bit_cast<int64_t>(uint64_t(mul(period[1],endpoint[1]))+uint64_t(mul(period[0],endpoint[0])));
        const double cosine=std::fabs(double(dot)/(std::sqrt(squared(period))*std::sqrt(squared(endpoint))));
        auto candidate=escherNormalizePeriodBasis({period,endpoint});if(candidate.width<=0)continue;
        candidate.boxes=boxes;candidate.offsets.resize(tables.size());const auto determinant=cross(period,endpoint);
        const int64_t area=(determinant<0?sub(0,determinant):determinant)/int64_t(int32_t(tables.size()));
        minimumArea=std::min(minimumArea,area);(cosine<5e-8?output.special:output.ordinary).push_back(std::move(candidate));
    }
}
EscherPeriodCandidate escherSwapCandidateBasis(const EscherPeriodCandidate &candidate,std::span<const EscherSupportTable> tables) {
    auto output=escherNormalizePeriodBasis({candidate.basis[1],candidate.basis[0]});output.boxes=escherCandidateBoxes(tables,candidate.basis[1]);output.offsets.resize(tables.size());return output;
}
void escherNormalizeCandidatePhase(EscherPeriodCandidate &candidate) {
    require(!candidate.boxes.empty()&&candidate.boxes.size()<=2&&candidate.offsets.size()==candidate.boxes.size(),PN2S_INVALID,"Escher phase normalization requires one or two matching members");
    auto boxes=candidate.boxes;
    if(boxes.size()==1){candidate.offsets[0]={sub(0,boxes[0][0]),sub(0,boxes[0][1])};return;}
    require(candidate.width>0&&candidate.step[1]>0,PN2S_INVALID,"Escher phase normalization needs positive lattice dimensions");
    if(boxes[0][1]!=boxes[1][1]) {
        const size_t lower=boxes[0][1]<boxes[1][1]?0:1,upper=1-lower;const int64_t delta=sub(boxes[upper][1],boxes[lower][1]),height=candidate.step[1];int64_t remainder=delta;
        while(height<remainder)remainder=sub(remainder,height);
        const auto advance=[&](){for(size_t axis=0;axis<2;++axis){boxes[lower][axis]=sub(boxes[lower][axis],sub(0,candidate.step[axis]));boxes[lower][axis+2]=sub(boxes[lower][axis+2],sub(0,candidate.step[axis]));}};
        if(sub(height,remainder)<remainder){if(boxes[lower][1]<boxes[upper][1])do{advance();}while(boxes[lower][1]<boxes[upper][1]);}
        else if(height<delta)do{advance();}while(height<sub(boxes[upper][1],boxes[lower][1]));
    }
    const size_t base=boxes[1][1]<boxes[0][1]?1:0,other=1-base;const EscherIntegerPoint origin{boxes[base][0],boxes[base][1]};
    for(auto &box:boxes)for(size_t axis=0;axis<2;++axis){box[axis]=sub(box[axis],origin[axis]);box[axis+2]=sub(box[axis+2],origin[axis]);}
    const auto x=boxes[other][0],quotient=x/candidate.width;boxes[other][0]=sub(x,mul(quotient,candidate.width));if(boxes[other][0]<0)boxes[other][0]=sub(boxes[other][0],sub(0,candidate.width));
    for(size_t member=0;member<2;++member)candidate.offsets[member]={sub(boxes[member][0],candidate.boxes[member][0]),sub(boxes[member][1],candidate.boxes[member][1])};
}
bool escherCandidateSlopeNear(double a,double b,EscherIntegerPoint u,EscherIntegerPoint v,double threshold) {
    const auto dot=std::bit_cast<int64_t>(uint64_t(mul(u[1],v[1]))+uint64_t(mul(u[0],v[0])));if(dot<0)return false;
    const double value=std::isinf(a)?b:std::isinf(b)?a:(a*b+1)/(b-a);return threshold<std::fabs(value);
}
}
