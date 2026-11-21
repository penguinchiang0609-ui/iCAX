#include "recovered_escher_record.hpp"
#include <bit>
#include <numeric>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int64_t cross(EscherIntegerPoint a,EscherIntegerPoint b){return sub(mul(a[0],b[1]),mul(a[1],b[0]));}
EscherIntegerPoint difference(EscherIntegerPoint a,EscherIntegerPoint b){return {sub(a[0],b[0]),sub(a[1],b[1])};}
int64_t roundInteger(double value) {
    value=value<0?value-.5:value+.5;
    return !std::isfinite(value)||value>=0x1p63||value< -0x1p63?INT64_MIN:int64_t(value);
}
EscherIntegerPoint rotate(EscherIntegerPoint point,double cosine,double sine) {
    const double x=double(point[0]),y=double(point[1]);
    const double rotatedX=x*cosine-y*sine,rotatedY=x*sine+y*cosine;
    return {roundInteger(rotatedX),roundInteger(rotatedY)};
}
}
EscherSupportTable escherBuildSupportTable(std::vector<EscherIntegerPoint> points) {
    require(points.size()>=2,PN2S_INVALID,"Escher angle support requires at least two different points");
    std::sort(points.begin(),points.end());std::vector<EscherIntegerPoint> upper,lower;
    for(const auto &point:points) {
        while(upper.size()>1&&cross(difference(upper.back(),upper[upper.size()-2]),difference(point,upper[upper.size()-2]))>=0)upper.pop_back();
        while(lower.size()>1&&cross(difference(lower.back(),lower[lower.size()-2]),difference(point,lower[lower.size()-2]))<=0)lower.pop_back();
        upper.push_back(point);lower.push_back(point);
    }
    size_t up=upper.size()-1,down=0;EscherSupportTable result;
    while(down<lower.size()-1||up!=0) {
        const auto first=lower[down],second=upper[up];EscherIntegerPoint direction;
        if(down==lower.size()-1){direction=difference(upper[up],upper[up-1]);--up;}
        else if(up==0){direction=difference(lower[down+1],lower[down]);++down;}
        else {
            const auto u=difference(upper[up],upper[up-1]),l=difference(lower[down+1],lower[down]);
            if(mul(l[0],u[1])<=mul(l[1],u[0])){direction=u;--up;}else{direction=l;++down;}
        }
        const auto magnitude=[](int64_t value){return value<0?0U-uint64_t(value):uint64_t(value);};
        const uint64_t divisor=std::gcd(magnitude(direction[0]),magnitude(direction[1]));
        require(divisor!=0&&divisor<=uint64_t(INT64_MAX),PN2S_INVALID,"Escher angle support has a degenerate edge");
        direction[0]/=int64_t(divisor);direction[1]/=int64_t(divisor);result.push_back({first,second,direction});
    }
    return result;
}
std::array<EscherIntegerPoint,2> escherSupportExtrema(std::span<const EscherSupportRecord> table,EscherIntegerPoint direction) {
    require(!table.empty(),PN2S_INVALID,"Escher angle support table is empty");
    int64_t x=direction[0],y=sub(0,direction[1]);if(y<0){x=sub(0,x);y=direction[1];}
    size_t first=0,count=table.size();while(count) {
        const size_t half=count/2,middle=first+half;
        if(sub(mul(x,table[middle].direction[0]),mul(y,table[middle].direction[1]))>0){first=middle+1;count-=half+1;}else count=half;
    }
    if(first==table.size())first=0;return {table[first].first,table[first].second};
}
EscherIntegerPoint escherRotateRecordPoint(EscherIntegerPoint point,double radians) {
    const double cosine=std::cos(radians),sine=std::sin(radians);return rotate(point,cosine,sine);
}
EscherIntegerBox escherBoundsFromSupport(std::array<EscherIntegerPoint,2> first,
    std::array<EscherIntegerPoint,2> second,double cosine,double sine) {
    const auto origin=rotate(second[0],cosine,sine);EscherIntegerBox bounds{origin[0],origin[1],origin[0],origin[1]};
    for(const auto &point:std::array<EscherIntegerPoint,3>{second[1],first[0],first[1]}) {
        const auto rotated=rotate(point,cosine,sine);bounds[0]=std::min(bounds[0],rotated[0]);bounds[1]=std::min(bounds[1],rotated[1]);
        bounds[2]=std::max(bounds[2],rotated[0]);bounds[3]=std::max(bounds[3],rotated[1]);
    }
    return bounds;
}
EscherIntegerBox escherBoundsForAngle(std::span<const EscherSupportRecord> table,int32_t angle) {
    const double radians=(double(angle)/10000.)*0.017453292519943295;
    const double cosine=std::cos(radians),sine=std::sin(radians);
    const int64_t x=roundInteger(cosine*10000.),y=roundInteger(sine*-10000.);
    const auto first=escherSupportExtrema(table,{x,y}),second=escherSupportExtrema(table,{sub(0,y),x});
    return escherBoundsFromSupport(first,second,cosine,sine);
}
void escherUpdateRecordBounds(EscherPatternRecord &record,std::span<const EscherSupportRecord> intrinsic,
    std::span<const EscherSupportRecord> collision) {
    auto first=escherBoundsForAngle(intrinsic,record.angle),second=escherBoundsForAngle(collision,record.angle);
    if((record.reflected&0xffU)!=0) {
        const int64_t low=first[1];first[1]=sub(0,first[3]);first[3]=sub(0,low);
        const int64_t collisionLow=second[1];second[1]=sub(0,second[3]);second[3]=sub(0,collisionLow);
    }
    for(size_t index=0;index<4;++index)record.intrinsic[index]=std::bit_cast<uint64_t>(first[index]);record.bounds=second;
}
}
