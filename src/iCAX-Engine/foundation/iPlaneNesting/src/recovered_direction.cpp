#include "recovered_direction.hpp"
#include <bit>
namespace study::recovered {
std::vector<double> noiseValues(Random &rng,std::span<const ValuePart> parts,std::span<const double> incoming,double totalArea,double amplitude) {
    require(parts.size()==incoming.size(),PN2S_INVALID,"Noise values must cover all types");
    const double low=1.-amplitude, high=amplitude+1.;
    std::vector<double> out;double total=0;
    for(size_t i=0;i<parts.size();++i) {
        const double factor=rng.unit()*(high-low)+low;
        const double value=incoming[i]*factor;out.push_back(value);
        total+=static_cast<double>(parts[i].optional?0:parts[i].quantity)*value;
    }
    if(total!=0.)for(auto &v:out)v=(totalArea/total)*v;
    return out;
}
double uniformValue(Random &rng,double base,double low,double high) {
    const double factor=rng.unit()*(high-low)+low;
    return base*factor;
}
std::vector<Extremal1cPair> extremalGenerator1c(Random &rng,int batch,int count,int64_t front,
    bool force,std::span<const ValuePart> parts,std::span<const double> incoming,double totalArea) {
    std::vector<Extremal1cPair> out;
    for(int k=0;k<count;++k) {
        Extremal1cPair p;p.batch=batch;p.front=front;p.extremaA=front>0?5:3;p.uniformA=k!=0;
        p.noise=noiseValues(rng,parts,incoming,totalArea,.1);
        p.inspectProcessesA=force || rng.unit()<.33;
        p.inspectProcessesB=force || rng.unit()<.33;
        out.push_back(std::move(p));
    }
    return out;
}
std::vector<int> btGeneratorOrder(int count10,int flag14,int limit18,int count1c) {
    std::vector<int> out;
    if(flag14>0)out.push_back(0x14);
    if(limit18>0)out.push_back(0x18);
    if(count1c>0)out.push_back(0x1c);
    if(count10>0)out.push_back(0x10);
    return out;
}
int64_t fixedFrontStripEnd(int64_t front,int64_t minX,int64_t maxX,int64_t pitch) {
    require(pitch>0,PN2S_INVALID,"Fixed front requires positive pitch");
    const int64_t v=(minX-maxX)+front;
    return std::bit_cast<int32_t>(static_cast<uint32_t>(v/pitch)+(v%pitch!=0 && v>=0?1U:0U));
}
namespace {
Point draw(Random &rng, double scaleY, double scaleX) {
    const int sign = rng.unit() < .5 ? -1 : 1;
    const double u = rng.unit();
    // Keep the original pow call, operation order and random consumption.
    double y = std::pow(u * .7 + .3, 2.);
    y *= sign;
    y *= scaleY;
    return {scaleX * -1., y};
}
}
std::vector<Point> baseDirections(Random &rng,double field218,double field220,int count) {
    std::vector<Point> out{{field220 * -1., field218 * 0.}};
    for(int k=0;k<count;++k)out.push_back(draw(rng,field218,field220));
    return out;
}
std::vector<Point> randomDirections(Random &rng,double field218,double field220) {
    std::vector<Point> out;
    for(int k=0;k<2;++k)out.push_back(draw(rng,field218,field220));
    return out;
}
std::vector<DirectionPair> directionGenerator14(Random &rng,double field218,double field220,int batch) {
    std::vector<DirectionPair> out;
    for(auto d:baseDirections(rng,field218,field220,2))out.push_back({{d},batch,2});
    for(int k=0;k<3;++k)out.push_back({randomDirections(rng,field218,field220),batch,5});
    return out;
}
std::vector<Direction10Pair> directionGenerator10(Random &rng,double sy,double sx,int batch,int count10,
    double alpha,double gamma,double probability,double scale,const std::vector<ValuePart> &parts,double totalArea) {
    std::vector<Direction10Pair> out;
    for(int k=0;k<count10;++k) {
        Direction10Pair pair;pair.candidate={baseDirections(rng,sy,sx,2),batch,1,{}};
        auto perturb=[&](double value) {
            const double negative=std::bit_cast<double>(std::bit_cast<uint64_t>(value)^0x8000000000000000ULL);
            double result=rng.unit()*((value+.1)-negative);
            result+=negative;result+=value;return result;
        };
        pair.complexity.alpha=perturb(alpha);pair.complexity.gamma=perturb(gamma);
        // The explicit exponents disable the constructor's random-alpha/gamma branch.
        if(rng.unit()<probability)pair.complexity.perimeter=rng.unit()*(scale-0.)+0.;
        pair.complexity.useBBox=rng.unit()<.2;
        pair.valuesA=values(parts,totalArea,pair.complexity);
        out.push_back(std::move(pair));
    }
    return out;
}
std::vector<DirectionPair> directionGenerator18(Random &rng,double field218,double field220,
                                               int32_t batch,int32_t limit18,std::span<const DirectionGroup> groups) {
    const auto directions=baseDirections(rng,field218,field220,10);
    std::vector<DirectionPair> out;
    // Original arithmetic is 32-bit wrapping followed by a signed comparison.
    const auto threshold=std::bit_cast<int32_t>(static_cast<uint32_t>(batch)*2U);
    for(const auto &group:groups) {
        const auto remaining=std::bit_cast<int32_t>(static_cast<uint32_t>(group.optional?0:group.quantity)-static_cast<uint32_t>(group.placed));
        if(remaining<threshold)continue;
        for(auto direction:directions) {
            const auto previousCount=out.size();
            out.push_back({{direction},batch,3,{group.key}});
            if(static_cast<int64_t>(previousCount)>=limit18)return out;
        }
    }
    return out;
}
DirectionScan scanDirection(const Rows &rows,size_t first,size_t end,double xStep,
                            double dy,int64_t boundY,bool lower,bool trace) {
    require(first<=end && end<=rows.size(),PN2S_INVALID,"Direction scan range outside map");
    DirectionScan result;
    if(first==end)return result;
    const bool forward=xStep<0;
    size_t index=forward?first:end-1,last=forward?end-1:first;
    const double bound=static_cast<double>(boundY)/1000.*dy;
    double best=-std::numeric_limits<double>::max();
    auto inspect=[&]{
        if(trace)result.visited.push_back(index);
        const auto &row=rows[index];
        const int64_t y=row.empty()?std::numeric_limits<int32_t>::min():(lower?row.front()[0]:row.back()[1]);
        const double x=static_cast<double>(index)*xStep;
        if(y>=0) {
            const double value=(static_cast<double>(y)/1000.)*dy+x;
            if(value>best){best=value;result.point=std::array<int64_t,2>{static_cast<int64_t>(index),y};}
        }
        return x;
    };
    inspect();
    // Original scans always inspect the next row before applying the bound.
    while(index!=last) {
        if(forward)++index;else --index;
        const double x=inspect();
        if(best>=bound+x)break;
    }
    return result;
}
std::vector<std::array<int64_t,2>> directionPoints(const Rows &rows,int64_t pitch,
                                                std::span<const Point> directions,int64_t lo,int64_t hi) {
    std::vector<std::array<int64_t,2>> out;
    for(auto d:directions) {
        const bool lower=d.y<0;
        auto scan=scanDirection(rows,0,rows.size(),static_cast<double>(pitch)/1e6*d.x,d.y,lower?lo:hi,lower);
        // 3e34c0 with flag=1 retains the first occurrence, no sorting.
        if(scan.point && std::find(out.begin(),out.end(),*scan.point)==out.end())out.push_back(*scan.point);
    }
    return out;
}
}
