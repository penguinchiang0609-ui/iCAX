#include "recovered_convolution_polygon.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace study::recovered {
namespace {
double quiet(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)|0x8000000000000ULL);}
// Preserve SSE destination operand ordering, including NaN sign/payload.
double add(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a+b;}
double sub(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a-b;}
double mul(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a*b;}
double div(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a/b;}
double minSse(double a,double b){return a<b?a:b;}
double maxSse(double a,double b){return a>b?a:b;}
int64_t roundCoordinate(double v){
    const double adjusted=v>=0?add(v,.5):sub(v,.5);
    // CVTTSD2SI returns the integer-indefinite value for NaN and overflow.
    if(!(adjusted>=-0x1p63&&adjusted<0x1p63))return INT64_MIN;
    return static_cast<int64_t>(adjusted);
}
}
std::vector<ShakerPoint> convolutionTopologyPoints(const ConvolutionTopology &t){
    std::vector<ShakerPoint> points;const auto end=t.edges.at(t.lastEdge).last;auto current=t.edges.at(t.firstEdge).first;
    do{const auto &v=t.vertices.at(current);points.push_back(v.point);current=v.outgoing<0?-1:t.edges.at(v.outgoing).last;}while(current!=end);
    return points;
}
void convolutionScalePolygon(std::span<ShakerPoint> polygon,double factor){
    for(auto &p:polygon){p[0]=mul(p[0],factor);p[1]=mul(p[1],factor);}
    if(factor<0)std::reverse(polygon.begin(),polygon.end());
}
void convolutionRotatePolygon(std::span<ShakerPoint> polygon,int32_t angle){
    const auto flip=[](double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)^0x8000000000000000ULL);};
    for(auto &p:polygon){
        const auto x=p[0],y=p[1];
        if(angle==0)continue;
        if(angle==2700000){p={y,flip(x)};continue;}
        if(angle==1800000){p={mul(x,-1.),mul(y,-1.)};continue;}
        if(angle==900000){p={flip(y),x};continue;}
        const double radians=(double(angle)/10000.)*0.017453292519943295;
        const double c=std::cos(radians),s=std::sin(radians);p={sub(mul(c,x),mul(y,s)),add(mul(s,x),mul(y,c))};
    }
}
void convolutionAppendPolygonRecords(std::span<const ShakerPoint> points,std::vector<ShakerBoundary> &out){
    const auto count=std::bit_cast<int32_t>(uint32_t(points.size()));if(count<=0)return;
    const uint32_t base=uint32_t(out.size());
    for(int32_t n=0;n<count;++n){ShakerBoundary r;r.start=points[n];r.end=points[(n+1)%count];
        r.previous=std::bit_cast<int32_t>(base+uint32_t((n+count-1)%count));r.next=std::bit_cast<int32_t>((base+uint32_t((n+1)%count))&0x7fffffffU);out.push_back(r);}
}
std::vector<ShakerBoundary> convolutionPolygonRecords(std::span<const std::vector<ShakerPoint>> polygons){
    std::vector<ShakerBoundary> result;for(const auto &p:polygons)convolutionAppendPolygonRecords(p,result);return result;
}
ShakerPoint convolutionFirstPoint(const ConvolutionMultiPolygon &multi){for(const auto &polygon:multi)for(const auto &ring:polygon)if(!ring.empty())return ring.front();return {0,0};}
std::array<double,4> convolutionMultiBounds(const ConvolutionMultiPolygon &multi){
    const auto first=multi.at(0).at(0).at(0);std::array<double,4> bounds{first[0],first[1],first[0],first[1]};
    for(const auto &polygon:multi)for(const auto &ring:polygon)for(auto point:ring)for(size_t axis=0;axis<2;++axis){bounds[axis]=minSse(point[axis],bounds[axis]);bounds[axis+2]=maxSse(point[axis],bounds[axis+2]);}return bounds;
}
ShakerPoint convolutionChooseScale(std::span<const ConvolutionMultiPolygon> operands,int64_t limit){
    if(operands.empty())throw std::invalid_argument("Scale requires at least one operand");ShakerPoint maximum{};
    for(const auto &multi:operands){auto low=convolutionFirstPoint(multi),high=low;
        for(const auto &polygon:multi)for(const auto &ring:polygon)for(auto point:ring)for(size_t axis=0;axis<2;++axis){if(low[axis]>point[axis])low[axis]=point[axis];high[axis]=maxSse(point[axis],high[axis]);}
        for(size_t axis=0;axis<2;++axis)maximum[axis]=maxSse(sub(high[axis],low[axis]),maximum[axis]);
    }
    return {div(double(limit),mul(maximum[0],4.)),div(double(limit),mul(maximum[1],4.))};
}
ConvolutionScale convolutionScaleConfig(std::span<const ConvolutionMultiPolygon> operands,int64_t limit,bool isotropic){
    ConvolutionScale result;result.factors=convolutionChooseScale(operands,limit);
    if(isotropic){const auto factor=minSse(result.factors[1],result.factors[0]);result.factors={factor,factor};}
    for(const auto &multi:operands){const auto b=convolutionMultiBounds(multi);result.references.push_back({mul(add(b[2],b[0]),.5),mul(add(b[1],b[3]),.5)});}return result;
}
void convolutionApplyScale(ConvolutionMultiPolygon &multi,ShakerPoint factors,std::span<const ShakerPoint> references){
    if(references.size()<multi.size())throw std::invalid_argument("Missing polygon reference points");
    for(size_t n=0;n<multi.size();++n)for(auto &ring:multi[n])for(auto &point:ring)for(size_t axis=0;axis<2;++axis)point[axis]=mul(sub(point[axis],references[n][axis]),factors[axis]);
}
std::vector<ConvolutionIntegerPoint> convolutionCleanIntegerRing(std::span<const ConvolutionIntegerPoint> ring){
    if(ring.size()<4)return {ring.begin(),ring.end()};
    const auto distance=[](ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){const double x=double(std::bit_cast<int64_t>(uint64_t(a[0])-uint64_t(b[0]))),y=double(std::bit_cast<int64_t>(uint64_t(a[1])-uint64_t(b[1])));return y*y+x*x;};
    std::vector<ConvolutionIntegerPoint> result{ring.front()};
    for(size_t n=1;n<ring.size();++n)if(distance(result.back(),ring[n])>=9.)result.push_back(ring[n]);
    if(distance(result.back(),result.front())<9.)result.pop_back();return result;
}
std::array<double,4> convolutionPolygonBounds(const ConvolutionPolygon &polygon){
    const double maximum=std::numeric_limits<double>::max();std::array<double,4> bounds{maximum,maximum,-maximum,-maximum};
    for(const auto &ring:polygon)for(auto point:ring)for(size_t axis=0;axis<2;++axis){bounds[axis]=minSse(bounds[axis],point[axis]);bounds[axis+2]=maxSse(bounds[axis+2],point[axis]);}return bounds;
}
std::vector<ConvolutionIntegerPoint> convolutionRoundRing(std::span<const ShakerPoint> ring){
    std::vector<ConvolutionIntegerPoint> result;result.reserve(ring.size());
    for(auto p:ring){const ConvolutionIntegerPoint integer{roundCoordinate(p[0]),roundCoordinate(p[1])};if(result.empty()||integer!=result.back())result.push_back(integer);}
    if(result.size()>1&&result.front()==result.back())result.pop_back();return result;
}
std::vector<ConvolutionIntegerPoint> convolutionQuantizeRing(std::span<const ShakerPoint> input){
    // Each recursive vendor pass keeps original double points. An iterative
    // reduction preserves that order without growing the native call stack.
    ConvolutionRing reduced;auto ring=input;
    for(;;){
        if(ring.size()<4)return convolutionRoundRing(ring);
        const auto bounds=convolutionPolygonBounds({ConvolutionRing(ring.begin(),ring.end())});
        if(sub(bounds[2],bounds[0])<32.||sub(bounds[3],bounds[1])<32.)return convolutionRoundRing(ring);
        const auto count=ring.size();std::vector<ConvolutionIntegerPoint> integers(count),integerEdges(count);ConvolutionRing edges(count);
        for(size_t n=0;n<count;++n)integers[n]={roundCoordinate(ring[n][0]),roundCoordinate(ring[n][1])};
        for(size_t n=0;n<count;++n)for(size_t axis=0;axis<2;++axis){const auto next=(n+1)%count;edges[n][axis]=sub(ring[next][axis],ring[n][axis]);integerEdges[n][axis]=std::bit_cast<int64_t>(uint64_t(integers[next][axis])-uint64_t(integers[n][axis]));}
        std::vector<uint8_t> keep(count),zeroSign(count),changedSign(count);
        for(size_t n=0;n<count;++n){const auto prev=(n+count-1)%count;
            const double cross=sub(mul(edges[n][1],edges[prev][0]),mul(edges[prev][1],edges[n][0]));const int sign=(cross>0)-(cross<0);
            const auto integerCross=std::bit_cast<int64_t>(uint64_t(integerEdges[n][1])*uint64_t(integerEdges[prev][0])-uint64_t(integerEdges[prev][1])*uint64_t(integerEdges[n][0]));const int integerSign=(integerCross>0)-(integerCross<0);
            keep[n]=integerEdges[n][0]!=0||integerEdges[n][1]!=0;zeroSign[n]=sign==0||integerSign==0;changedSign[n]=sign!=integerSign;
        }
        // The two passes mutate in traversal order; neighbors observe earlier
        // deletions in this same pass, including at the ring seam.
        for(const auto *flags:{&zeroSign,&changedSign})for(size_t n=0;n<count;++n)if(keep[(n+count-1)%count]&&keep[n]&&keep[(n+1)%count]&&(*flags)[n])keep[n]=0;
        ConvolutionRing next;next.reserve(count);for(size_t n=0;n<count;++n)if(keep[n])next.push_back(ring[n]);
        if(next.size()==count||next.size()<=2)return convolutionRoundRing(ring);
        reduced=std::move(next);ring=reduced;
    }
}
ConvolutionIntegerMultiPolygon convolutionQuantizeNested(const ConvolutionMultiPolygon &multi){
    ConvolutionIntegerMultiPolygon out;out.reserve(multi.size());for(const auto &polygon:multi){auto &p=out.emplace_back();p.reserve(polygon.size());for(const auto &ring:polygon)p.push_back(convolutionQuantizeRing(ring));}return out;
}
ConvolutionIntegerMultiPolygon convolutionCleanNested(const ConvolutionIntegerMultiPolygon &multi){
    ConvolutionIntegerMultiPolygon out;for(const auto &polygon:multi){auto &p=out.emplace_back();for(const auto &ring:polygon)p.push_back(convolutionCleanIntegerRing(ring));}return out;
}
std::vector<ConvolutionIntegerMultiPolygon> convolutionQuantizeOperands(std::vector<ConvolutionMultiPolygon> &operands,const ConvolutionScale &scale){
    if(scale.references.size()<operands.size())throw std::invalid_argument("Missing operand reference points");
    for(size_t n=0;n<operands.size();++n){std::vector<ShakerPoint> references(operands[n].size(),scale.references[n]);convolutionApplyScale(operands[n],scale.factors,references);}
    std::vector<ConvolutionIntegerMultiPolygon> out;for(const auto &multi:operands)out.push_back(convolutionCleanNested(convolutionQuantizeNested(multi)));std::vector<ConvolutionMultiPolygon>().swap(operands);return out;
}
ConvolutionPolygon convolutionDequantizePolygon(const ConvolutionIntegerPolygon &polygon){
    ConvolutionPolygon out;out.reserve(polygon.size());for(const auto &ring:polygon){auto &r=out.emplace_back();r.reserve(ring.size());for(auto p:ring)r.push_back({double(p[0]),double(p[1])});}return out;
}
}
