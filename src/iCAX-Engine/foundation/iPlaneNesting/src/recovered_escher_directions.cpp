#include "recovered_escher_directions.hpp"
#include <bit>
#include <algorithm>
#include <limits>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int32_t mul32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)*uint32_t(b));}
int32_t add32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int64_t truncate64(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int64_t roundInteger(double value){return truncate64(value<0?value-.5:value+.5);}
int32_t angleInteger(double value){const auto low=std::bit_cast<int32_t>(uint32_t(roundInteger(value)));const auto remainder=low%3600000;return remainder<0?remainder+3600000:remainder;}
double squared(EscherIntegerPoint point){const double x=double(point[0]),y=double(point[1]);return y*y+x*x;}
EscherIntegerPoint rotate(EscherIntegerPoint point,int32_t angle){
    if(angle==0)return point;if(angle==900000)return {sub(0,point[1]),point[0]};if(angle==1800000)return {sub(0,point[0]),sub(0,point[1])};if(angle==2700000)return {point[1],sub(0,point[0])};
    return escherRotateRecordPoint(point,(double(angle)/10000.)*.017453292519943295);
}
std::array<double,2> lineIntersection(EscherIntegerPoint direction,ConvolutionIntegerSegment edge){
    const double ax=double(edge[0][0]),ay=double(edge[0][1]),bx=double(edge[1][0]),by=double(edge[1][1]),dx=double(direction[0]),dy=double(direction[1]);
    const double x=ax-bx,y=ay-by,c=ax*by-ay*bx,den=(-dy)*x-y*(-dx);
    return {((-dx)*c-x*0.)/den,((-dy)*c-y*0.)/den};
}
class PointDirectionSort {
    std::vector<EscherIntegerPoint> &points;EscherIntegerPoint reference;
    bool before(EscherIntegerPoint a,EscherIntegerPoint b)const {return squared({sub(a[0],reference[0]),sub(a[1],reference[1])})<squared({sub(b[0],reference[0]),sub(b[1],reference[1])});}
    void insertion(size_t first,size_t last){for(size_t n=first+1;n<last;++n){auto item=points[n];size_t j=n;if(before(item,points[first])){std::move_backward(points.begin()+first,points.begin()+n,points.begin()+n+1);j=first;}else while(j>first&&before(item,points[j-1])){points[j]=points[j-1];--j;}points[j]=item;}}
    void median(size_t first,size_t middle,size_t last){if(before(points[middle],points[first]))std::swap(points[middle],points[first]);if(before(points[last],points[middle])){std::swap(points[last],points[middle]);if(before(points[middle],points[first]))std::swap(points[middle],points[first]);}}
    std::pair<size_t,size_t> partition(size_t first,size_t last){
        const auto mid=first+(last-first)/2;
        if(last-first>41){const auto step=(last-first)/8;median(first,first+step,first+2*step);median(mid-step,mid,mid+step);median(last-1-2*step,last-1-step,last-1);median(first+step,mid,last-1-step);}else median(first,mid,last-1);
        size_t pfirst=mid,plast=mid+1;while(first<pfirst&&!before(points[pfirst-1],points[pfirst])&&!before(points[pfirst],points[pfirst-1]))--pfirst;while(plast<last&&!before(points[plast],points[pfirst])&&!before(points[pfirst],points[plast]))++plast;
        size_t right=plast,left=pfirst;
        for(;;){for(;right<last;++right){if(before(points[pfirst],points[right]))continue;if(before(points[right],points[pfirst]))break;if(plast!=right)std::swap(points[plast],points[right]);++plast;}
            for(;first<left;--left){if(before(points[left-1],points[pfirst]))continue;if(before(points[pfirst],points[left-1]))break;--pfirst;if(pfirst!=left-1)std::swap(points[pfirst],points[left-1]);}
            if(left==first&&right==last)return {pfirst,plast};
            if(left==first){if(plast!=right)std::swap(points[pfirst],points[plast]);++plast;std::swap(points[pfirst++],points[right++]);}
            else if(right==last){--left;--pfirst;if(left!=pfirst)std::swap(points[left],points[pfirst]);std::swap(points[pfirst],points[--plast]);}
            else std::swap(points[right++],points[--left]);
        }
    }
    void down(size_t base,size_t hole,size_t count,EscherIntegerPoint item){const auto top=hole,limit=(count-1)/2;
        while(hole<limit){size_t child=hole*2+1;if(!before(points[base+child+1],points[base+child]))++child;points[base+hole]=points[base+child];hole=child;}
        if(hole==limit&&count%2==0){points[base+hole]=points[base+count-1];hole=count-1;}
        while(top<hole){const auto parent=(hole-1)/2;if(!before(points[base+parent],item))break;points[base+hole]=points[base+parent];hole=parent;}points[base+hole]=item;
    }
    void heap(size_t first,size_t last){size_t count=last-first;for(size_t n=count/2;n>0;--n)down(first,n-1,count,points[first+n-1]);while(count>1){auto item=points[first+count-1];points[first+count-1]=points[first];down(first,0,--count,item);}}
    void sort(size_t first,size_t last,size_t depth){while(last-first>32){if(!depth){heap(first,last);return;}const auto [a,b]=partition(first,last);depth=(depth>>1)+(depth>>2);if(a-first<last-b){sort(first,a,depth);first=b;}else{sort(b,last,depth);last=a;}}if(first<last)insertion(first,last);}
public:
    PointDirectionSort(std::vector<EscherIntegerPoint> &input,EscherIntegerPoint origin):points(input),reference(origin){}
    void run(size_t depth){sort(0,points.size(),depth);}
};
}
void escherSortPointDirections(std::vector<EscherIntegerPoint> &points,EscherIntegerPoint reference,size_t depth){PointDirectionSort(points,reference).run(depth==SIZE_MAX?points.size():depth);}
std::vector<EscherIntegerPoint> escherPolygonDirections(const ConvolutionIntegerPolygon &rings,EscherIntegerPoint reference,uint64_t limit,double threshold) {
    std::vector<EscherIntegerPoint> output;for(const auto &ring:rings)for(size_t index=0;index<ring.size();++index){const auto p=ring[index],previous=ring[(index+ring.size()-1)%ring.size()],next=ring[(index+1)%ring.size()];
        const double py=double(sub(previous[1],p[1])),px=double(sub(previous[0],p[0])),ny=double(sub(next[1],p[1])),nx=double(sub(next[0],p[0]));
        const double angle=std::atan2(px*ny-py*nx,nx*px+py*ny)*57.29577951308232*10000.;
        if(double(angleInteger(angle))/10000.<threshold&&p!=EscherIntegerPoint{0,0})output.push_back(p);
    }
    escherSortPointDirections(output,reference);if(output.size()>limit)output.resize(size_t(limit));return output;
}
std::vector<EscherIntegerPoint> escherDirectionRange(EscherIntegerPoint point,int32_t begin,int32_t end,int32_t denominator) {
    require(denominator>0,PN2S_INVALID,"Escher angle range denominator must be positive");std::vector<EscherIntegerPoint> output;
    const auto last=mul32(end,denominator);for(int32_t current=mul32(begin,denominator);current<=last;current=add32(current,denominator))if(current!=0)output.push_back(rotate(point,angleInteger((double(current)/double(denominator))*10000.)));return output;
}
std::vector<EscherIntegerPoint> escherPrimaryDirections(const EscherNfpBoundary &boundary,int32_t begin,int32_t end,int32_t step,int32_t denominator,double maximumLength) {
    require(denominator>0&&step>0,PN2S_INVALID,"Escher primary angle sampling requires positive denominator and step");std::vector<EscherIntegerPoint> output;
    const int64_t length=truncate64(maximumLength);const auto last=mul32(end,denominator);
    for(int32_t current=mul32(begin,denominator);current<last;current=add32(current,step)) {
        const auto direction=rotate({length,0},angleInteger((double(current)/double(denominator))*10000.));const auto edges=escherNfpRayIntersections(boundary,direction);
        double best=std::numeric_limits<double>::max();EscherIntegerPoint endpoint{};
        for(const auto &hit:edges){const auto intersection=lineIntersection(direction,hit.segment);const EscherIntegerPoint point{roundInteger(intersection[0]),roundInteger(intersection[1])};const double norm=std::sqrt(squared(point));if(norm<best&&norm>10){best=norm;endpoint=point;}}
        if(endpoint!=EscherIntegerPoint{0,0})output.push_back(endpoint);
    }return output;
}
void escherPeriodEndpointDirections(const EscherNfpBoundary &boundary,std::vector<EscherIntegerPoint> &directions) {
    for(auto &direction:directions){auto hits=escherNfpRayIntersections(boundary,direction);escherPrepareNfpRayHits(direction,{0,0},0,10,hits,false);direction=escherNfpPeriodEndpoint(hits);}
}
void escherDirectionRows(std::span<const EscherSupportTable> tables,const EscherNfpBoundary &boundary,const ConvolutionEdgeIndex &index,
    std::span<const EscherIntegerPoint> periods,std::span<const std::array<int32_t,2>> ranges,const EscherDirectionRowOptions &options,
    EscherCandidateLists &output,int64_t &minimumArea,const EscherSupportTable *aggregateSupport) {
    require(!tables.empty()&&options.stepDegrees>0&&options.density>0,PN2S_INVALID,"Escher direction rows require members and positive angle sampling");
    const double stepAngle=(double(angleInteger(double(options.stepDegrees%360)*10000.))/10000.)*.017453292519943295;
    for(const auto period:periods) {
        const EscherIntegerPoint perpendicular{sub(0,period[1]),period[0]};
        const int64_t crossValue=std::bit_cast<int64_t>(uint64_t(perpendicular[0])*uint64_t(period[1])-uint64_t(period[0])*uint64_t(perpendicular[1]));
        const int64_t magnitude=crossValue<0?sub(0,crossValue):crossValue;
        if(double(magnitude)*options.areaFactor/double(tables.size())>double(minimumArea))continue;
        const double bearing=std::atan2(double(period[0])*0.-double(period[1]),double(period[1])*0.+double(period[0]));
        const bool aligned=!options.restricted||std::fabs(std::fmod(std::fabs(bearing)+.01,std::fabs(stepAngle))-.01)<.001;
        std::vector<EscherIntegerPoint> directions;
        if(aligned){for(const auto range:ranges){auto rotated=escherDirectionRange(perpendicular,range[0],range[1],options.density);directions.insert(directions.end(),rotated.begin(),rotated.end());}directions.push_back(perpendicular);}
        else for(int32_t angle=0;angle<360;angle=add32(angle,options.stepDegrees)) {
            const auto direction=rotate({truncate64(options.maximumLength),0},angleInteger(double(angle%360)*10000.));
            const double u=double(period[0]),v=double(perpendicular[0]),x=double(direction[0]),y=double(direction[1]);
            const auto relativeAngle=angleInteger(std::atan2(u*x-v*y,u*y+v*x)*57.29577951308232*10000.);
            const double relative=std::fmod(double(relativeAngle)/10000.+180.,360.)-180.;
            if(std::any_of(options.worldRelativeRanges.begin(),options.worldRelativeRanges.end(),[&](auto range){return double(range[0])<=relative&&relative<=double(range[1]);}))directions.push_back(direction);
        }
        escherAppendNfpCandidates(tables,boundary,index,period,directions,output,minimumArea,aggregateSupport);
    }
}
}
