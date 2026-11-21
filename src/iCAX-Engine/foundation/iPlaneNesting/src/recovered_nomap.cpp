#include "recovered_nomap.hpp"
#include "recovered_amo_raster.hpp"
#include "recovered_direction.hpp"
#include <bit>
namespace study::recovered {
namespace {
int32_t i32(int64_t value){return std::bit_cast<int32_t>(uint32_t(value));}
int32_t sub32(int64_t a,int64_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int32_t add32(int64_t a,int64_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
Row closedIntersection(const Row &a,const Row &b){
    Row result;size_t i=0,j=0;
    while(i<a.size()&&j<b.size()){
        const int32_t low=std::max(i32(a[i][0]),i32(b[j][0]));
        const int32_t high=std::min(i32(a[i][1]),i32(b[j][1]));
        if(low<=high)result.push_back({low,high});
        if(i32(a[i][1])<i32(b[j][1]))++i;else ++j;
    }
    return result;
}
}
NoMapFootprint noMapFootprint(Rows rows){
    NoMapFootprint result;result.rows=std::move(rows);int32_t lowRow=0,highRow=0;
    for(size_t n=0;n<result.rows.size();++n)for(const auto interval:result.rows[n]){
        if(i32(interval[0])<result.minimumY){result.minimumY=i32(interval[0]);lowRow=int32_t(n);}
        if(i32(interval[1])>result.maximumY){result.maximumY=i32(interval[1]);highRow=int32_t(n);}
    }
    result.priorityRows.push_back(highRow);
    if(highRow!=lowRow)result.priorityRows.push_back(lowRow);
    std::sort(result.priorityRows.begin(),result.priorityRows.end());
    const auto append=[&](int32_t first,int32_t end){if(first<end){result.scanBoundaries.push_back(first);result.scanBoundaries.push_back(end);}};
    append(0,result.priorityRows.front());
    if(result.priorityRows.size()==2)append(result.priorityRows[0]+1,result.priorityRows[1]);
    append(result.priorityRows.back()+1,int32_t(result.rows.size()));
    return result;
}
NoMapRaster noMapRaster(std::span<const Polygon> contours,Point reference,int64_t pitch,bool align,double weight){
    require(!contours.empty()&&!contours.front().empty()&&pitch>0,PN2S_INVALID,"NoMap raster needs nonempty contours and positive pitch");
    double left=contours.front().front().x,minimumY=contours.front().front().y;
    for(const auto &ring:contours)for(const auto p:ring){if(p.x<left)left=p.x;if(p.y<minimumY)minimumY=p.y;}
    const double step=double(pitch)/1000000.;
    if(align)left=reference.x-std::ceil((reference.x-left)/step-1e-7)*step;
    NoMapRaster result;result.offsetX=rounded((reference.x-left)*1000000.);result.offsetY=i32(rounded((reference.y-minimumY)*1000.));
    const auto floating=amoRasterStrips(contours,{step*.5+left,minimumY+0.},left,step,.001,.000001,weight);
    Rows rows(size_t(floating.binCount));
    for(size_t row=0;row<rows.size();++row)for(auto index=floating.indices[row];index<floating.indices[row+1];++index){
        const auto interval=floating.intervals[size_t(index)];
        rows[row].push_back({int32_t(std::floor((interval[0]+1e-7)*1000.)),int32_t(std::ceil((interval[1]-1e-7)*1000.))});
    }
    result.footprint=noMapFootprint(std::move(rows));return result;
}
std::vector<Polygon> noMapContours(const NoMapContourLayers &input,bool slot2,bool duplicatePitch){
    require(bool(input.geometry),PN2S_INVALID,"NoMap contour geometry provider is missing");
    const auto count=std::max(1,std::min(input.available,input.maximum));std::vector<Polygon> result;
    for(int32_t layer=0;layer<count;++layer)for(const auto &ring:input.geometry(slot2,layer)){
        auto copy=ring;const double dx=double(std::min(layer,1))*input.layerX,dy=double(layer)*input.layerY;
        for(auto &p:copy){p.x+=dx;p.y+=dy;}result.push_back(std::move(copy));
    }
    if(duplicatePitch){const auto count=result.size();const double dx=double(input.pitch)/1000000.;for(size_t n=0;n<count;++n){auto copy=result[n];for(auto &p:copy){p.x=dx+p.x;p.y+=0.;}result.push_back(std::move(copy));}}
    return result;
}
bool noMapErodeInterval(const Row &base,Interval footprint,Row &allowed){
    Row candidate;
    for(const auto interval:base){
        const auto low=sub32(interval[0],footprint[0]),high=sub32(interval[1],footprint[1]);
        if(low<=high)candidate.push_back({low,high});
    }
    if(candidate.empty()){allowed.clear();return false;}
    // Native +18 is both length and initialization indicator. Callers stop on
    // the first empty result, so they never reinitialize a failed intersection.
    if(allowed.empty())allowed=std::move(candidate);
    else allowed=closedIntersection(candidate,allowed);
    return !allowed.empty();
}
bool noMapApplyGeometryRow(const Row &base,const Row &footprint,Row &allowed){
    if(footprint.empty())return !allowed.empty();
    for(const auto interval:footprint)
        if(!noMapErodeInterval(base,interval,allowed))return false;
    return true;
}
bool noMapComputeRow(NoMapRowState &state,const NoMapFootprint &footprint,const Rows &base,
    std::vector<int32_t> &maximumWidths,int32_t x,Row &allowed){
    require(maximumWidths.size()==base.size(),PN2S_INVALID,"NoMap width cache must match base strips");
    const int32_t last=int32_t(footprint.rows.size())-1;
    const int32_t sample=std::max(1,last/4);
    for(int32_t row=0;row<=last;row+=sample){
        const auto &shape=footprint.rows[size_t(row)];if(shape.empty())continue;
        const auto bx=add32(x,row);
        require(bx>=0&&size_t(bx)<base.size(),PN2S_INVALID,"NoMap sampled strip exceeds base map");
        auto &maximum=maximumWidths[size_t(bx)];
        if(maximum==INT32_MIN){
            maximum=INT32_MIN+1;
            for(const auto interval:base[size_t(bx)])maximum=std::max(maximum,sub32(interval[1],interval[0]));
        }
        if(sub32(shape.front()[1],shape.front()[0])>maximum)return false;
    }
    const auto apply=[&](int32_t row){
        require(row>=0&&row<=last,PN2S_INVALID,"NoMap footprint row index is invalid");
        const auto bx=add32(x,row);
        if(bx<0||size_t(bx)>=base.size())return false;
        return noMapApplyGeometryRow(base[size_t(bx)],footprint.rows[size_t(row)],allowed);
    };
    for(const auto row:footprint.priorityRows)if(!apply(row))return false;
    if(state.lastFailure>=0){
        const int32_t from=add32(state.lastFailure,-1),end=add32(state.lastFailure,1);
        for(int32_t absolute=from;absolute<=end;absolute=add32(absolute,1)){
            const auto row=sub32(absolute,x);if(row<0)continue;if(row>last)break;
            if(!apply(row)){state.lastFailure=absolute;return false;}
            if(absolute==INT32_MAX)break;
        }
    }
    if(footprint.scanBoundaries.empty())return true;
    int32_t lower=footprint.scanBoundaries.front(),upper=footprint.scanBoundaries.back();
    const int32_t middle=add32(lower,upper)/2;int32_t right=middle;
    while(true){
        if(lower<middle){if(!apply(lower)){state.lastFailure=add32(x,lower);return false;}++lower;}
        else if(right>=upper){state.lastFailure=-1;return true;}
        if(right<upper){if(!apply(right)){state.lastFailure=add32(x,right);return false;}++right;}
    }
}
void noMapStoreRow(NoMapEndpointCache &cache,int32_t x,const Row &row,bool success,bool mode){
    cache.lastQuery=x;const auto index=size_t(cache.perStrip?x:0);
    cache.lower.at(index)=success?i32(row.front()[0]):INT32_MIN;
    cache.upper.at(index)=success?i32(row.back()[1]):INT32_MIN;
    if(mode&&cache.flagCount==0){cache.flagCount=cache.lower.size();cache.flags.assign((cache.lower.size()+31)/32,0);}
    if(cache.flagCount){const auto bit=uint32_t(1)<<(index&31);auto &word=cache.flags.at(index>>5);if(mode)word|=bit;else word&=~bit;}
}
std::optional<Row> noMapCachedRow(const NoMapEndpointCache &cache,int32_t x,uintptr_t key,bool mode){
    const auto keyIndex=size_t(cache.perStrip&&cache.perStripKey?x:0);
    if(cache.keys.at(keyIndex)!=key||(!cache.perStrip&&cache.lastQuery!=x))return {};
    const auto index=size_t(cache.perStrip?x:0);
    const bool flag=cache.flagCount&&((cache.flags.at(index>>5)>>(index&31))&1);
    if(flag!=mode||cache.lower.at(index)==INT32_MIN+1)return {};
    if(cache.lower[index]==INT32_MIN)return Row{};
    return Row{{cache.lower[index],cache.upper.at(index)}};
}
void noMapInvalidate(NoMapEndpointCache &cache,int32_t &firstPossible,int32_t blockSize,
    int32_t footprintRows,int32_t baseRows,int32_t firstBlock,int32_t endBlock,uint32_t flag){
    auto first=std::max(0,add32(sub32(int64_t(blockSize)*firstBlock,footprintRows),1));
    const auto previous=first-1;
    if(previous>=0&&cache.flagCount){const auto bit=size_t(cache.perStrip?previous:0);if((cache.flags.at(bit>>5)>>(bit&31))&1)--first;}
    if((flag&0xfffffffdu)==0)firstPossible=std::min(firstPossible,first);
    const auto end=std::min(i32(int64_t(blockSize)*endBlock),baseRows);
    if(!cache.perStrip){if(cache.lastQuery<first||cache.lastQuery>=end)return;cache.lower.at(0)=cache.upper.at(0)=INT32_MIN+1;}
    else for(auto row=first;row<end;++row)cache.lower.at(size_t(row))=cache.upper.at(size_t(row))=INT32_MIN+1;
}
int32_t noMapFindFirst(NoMapEndpointCache &cache,int32_t &firstPossible,const NoMapFootprint &footprint,
    const Rows &base,std::vector<int32_t> &widths,int32_t first,int32_t end){
    int32_t x=std::max(firstPossible,first);Row row;NoMapRowState state;
    while(x<end&&!noMapComputeRow(state,footprint,base,widths,x,row))++x;
    if(first<=firstPossible)firstPossible=x;
    if(x<end)noMapStoreRow(cache,x,row,true,false);
    return std::min(x,end);
}
int32_t noMapFindLast(NoMapEndpointCache &cache,int32_t firstPossible,const NoMapFootprint &footprint,
    const Rows &base,std::vector<int32_t> &widths,int32_t first,int32_t end){
    const auto limit=std::max(firstPossible,first);int32_t x=end;Row row;NoMapRowState state;bool found=false;
    while(limit<x){--x;if(noMapComputeRow(state,footprint,base,widths,x,row)){found=true;break;}}
    if(found)noMapStoreRow(cache,x,row,true,false);
    return found?x:std::min(limit,end)-1;
}
NoMapRange noMapUnconstrainedRange(NoMapEndpointCache &cache,int32_t &firstPossible,
    const NoMapFootprint &footprint,const Rows &base,std::vector<int32_t> &widths,NoMapRangeOptions options){
    NoMapRange result{std::max(0,options.first),0};size_t last=base.size();while(last&&base[last-1].empty())--last;
    if(last==0){result.end=result.first;return result;}
    result.end=int32_t(last)-int32_t(footprint.rows.size())+1;
    if(options.end!=-1)result.end=std::min(result.end,options.end);
    if(result.end<=result.first)return result;
    result.first=noMapFindFirst(cache,firstPossible,footprint,base,widths,result.first,result.end);
    if(options.requestRightToLeft&&options.enableRightToLeft&&!options.nestingMode14b0&&result.first<result.end)
        result.end=noMapFindLast(cache,firstPossible,footprint,base,widths,result.first,result.end)+1;
    return result;
}
std::vector<StripPoint> noMapExtremalPoints(const Rows &allowed,const ExtremalInput &input){
    if(input.count<=0||input.first>=input.end)return {};
    const int32_t strip=input.fromLow?input.first:input.end-1;
    require(strip>=0&&size_t(strip)<allowed.size(),PN2S_INVALID,"NoMap extremal boundary strip exceeds map");
    const auto &row=allowed[size_t(strip)];if(row.empty())return {};
    const auto y=input.lower?i32(row.front()[0]):i32(row.back()[1]);
    if(y<0)return {};
    return {{strip,y}};
}
std::vector<StripPoint> noMapDirectionPoints(const Rows &rows,NoMapRange range,int64_t pitch,
    std::span<const Point> directions,int32_t lowerY,int32_t upperY){
    std::vector<StripPoint> result;
    if(range.first>=range.end)return result;
    for(const auto direction:directions){const bool lower=direction.y<0.;
        const auto value=scanDirection(rows,size_t(range.first),size_t(range.end),double(pitch)/1000000.*direction.x,
            direction.y,lower?lowerY:upperY,lower);
        if(value.point&&std::find(result.begin(),result.end(),*value.point)==result.end())result.push_back(*value.point);
    }
    return result;
}
}
