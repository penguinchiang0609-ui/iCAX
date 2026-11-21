#include "recovered_escher.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t trunc64(double value) {
    return !std::isfinite(value)||value>=0x1p63||value< -0x1p63?INT64_MIN:static_cast<int64_t>(value);
}
int64_t round64(double value) {return trunc64(value+(value<0?-.5:.5));}
int64_t add64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
}
int32_t buildEscherPattern(const EscherPatternInput &input,Random &random,EscherPatternHost &host) {
    const int64_t height=round64(((input.sheetHeight-input.bottomA8)-input.topB0)*1e6);
    const int64_t width=round64(((input.sheetWidth-input.left98)-input.rightA0)*1e6);
    int64_t offset=0;
    if(input.placedCount13d8!=0) {
        offset=sub64(sub64(input.maxPlacedX,round64(input.leftTrim70*1e6)),round64(input.rightA0*1e6));
        if(offset!=0)offset=add64(offset,round64(input.gap*1e6));
    }
    if(height<=0)return 0;
    const int64_t remaining=sub64(width,offset);
    const double availableWidth=double(remaining)/1e6,availableHeight=double(height)/1e6;
    const double freeArea=availableHeight*availableWidth;
    if(input.geometryArea+input.geometryArea>freeArea)return 0;
    const auto tenfold=std::bit_cast<int64_t>(uint64_t(remaining)*10U);
    if(tenfold<width)return 0;
    int32_t count=host.available();
    if(input.quantityCap && *input.quantityCap<count)count=*input.quantityCap;
    if(count<2)return 0;
    if((uint32_t(input.trial13bc%17)&1U)==0 && (count<=8 || 8.>freeArea/input.geometryArea))return 0;
    const auto provider=host.provider();if(!provider)return 0;
    EscherPatternRequest request;request.count=count;request.width=availableWidth;request.height=availableHeight;
    request.atLeftEdge=offset==0;
    if(input.trial13bc>1) {
        const int64_t scaledHeight=trunc64(availableHeight*provider->scale18);
        const int64_t numerator=add64(scaledHeight,scaledHeight);
        require(provider->spacingE8!=0 && !(numerator==INT64_MIN && provider->spacingE8==-1),PN2S_INVALID,
                "Escher pattern provider has invalid integer spacing");
        const int32_t limit=std::bit_cast<int32_t>(uint32_t(numerator/provider->spacingE8));
        request.discard=random.integer(0,std::max(0,std::min(count/4,limit)));
    }
    const auto placements=host.build(request);
    return placements.empty()?0:host.commit(placements,offset);
}
void improveEscherPatternLength(int64_t &bestLength,std::array<int64_t,4> &rectangle,
    int32_t required,EscherPatternLayout &best,uint32_t limit,const EscherLatticeProvider &provider) {
    int64_t width=bestLength;
    rectangle[2]=add64(rectangle[0],width);
    auto current=provider(rectangle,limit);
    const auto count=[](const EscherPatternLayout &layout){return std::bit_cast<int32_t>(uint32_t(layout.primary.size()));};
    const int32_t initialCount=count(current);
    if(initialCount<required)return;
    require(initialCount!=0,PN2S_INVALID,"Escher lattice search requires a nonempty feasible pattern");
    int64_t lower=0;
    for(;;) {
        const int32_t preceding=std::bit_cast<int32_t>(uint32_t(required)-1U);
        const int64_t numerator=std::bit_cast<int64_t>(uint64_t(int64_t(preceding))*uint64_t(width));
        require(!(numerator==INT64_MIN && initialCount==-1),PN2S_INVALID,"Escher lattice length division overflow");
        lower=numerator/initialCount;
        if(lower>=width && width<bestLength) {bestLength=width;best=current;break;}
        rectangle[2]=add64(rectangle[0],lower);
        auto next=provider(rectangle,limit);
        if(count(next)<required)break;
        current=std::move(next);width=lower;
    }
    while(lower<sub64(width,1)) {
        const int64_t midpoint=add64(lower,width)/2;
        rectangle[2]=add64(rectangle[0],midpoint);
        auto next=provider(rectangle,limit);
        if(count(next)<required)lower=midpoint;
        else {current=std::move(next);width=midpoint;}
    }
    if(width<bestLength){bestLength=width;best=std::move(current);}
}
std::array<int64_t,4> escherLayoutBounds(std::span<const EscherPatternRecord> records) {
    if(records.empty())return {};
    const auto &first=records.front();std::array<int64_t,4> result{
        add64(first.bounds[0],first.x),add64(first.bounds[1],first.y),
        add64(first.bounds[2],first.x),add64(first.bounds[3],first.y)};
    for(const auto &record:records) {
        result[0]=std::min(result[0],add64(record.bounds[0],record.x));
        result[1]=std::min(result[1],add64(record.bounds[1],record.y));
        result[2]=std::max(result[2],add64(record.bounds[2],record.x));
        result[3]=std::max(result[3],add64(record.bounds[3],record.y));
    }
    return result;
}
EscherPatternLayout solveEscherPatterns(EscherSolveHost &host,std::array<int64_t,4> rectangle,
    bool singleMode,bool primitivesOnly,uint64_t required,int64_t &lengthOutput) {
    EscherPatternLayout result;if(required==0)return result;
    std::array<int64_t,3> key{INT64_MAX,INT64_MAX,INT64_MAX};
    const auto less=[](const auto &a,const auto &b){return std::tie(a[0],a[2],a[1])<std::tie(b[0],b[2],b[1]);};
    for(size_t index=0;index<host.singleCount();++index) {
        if(primitivesOnly&&!host.singleIsPrimitive(index))continue;
        const auto previous=key;auto trial=host.single(index,rectangle,singleMode,required,key);
        if(less(key,previous))std::swap(result,trial);
    }
    if(result.primary.size()==required)lengthOutput=key[0];
    key[0]=std::min(key[0],sub64(rectangle[2],rectangle[0]));
    if(required>1&&!singleMode)for(size_t index=0;index<host.pairCount();++index)
        host.improvePair(index,key[0],rectangle,std::bit_cast<int32_t>(uint32_t(required)),result);
    if(result.primary.size()>required) {
        result.primary.resize(size_t(required));result.bounds=escherLayoutBounds(result.primary);
    }
    if(result.primary.size()==required)lengthOutput=key[0];
    return result;
}
}
