#include "recovered_extremal.hpp"
#include <bit>
namespace study::recovered {
namespace {
int32_t add32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t sub32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int64_t sub64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t gridX(int32_t i,int64_t pitch,int64_t origin){return std::bit_cast<int64_t>(uint64_t(int64_t(i))*uint64_t(pitch)+uint64_t(origin));}
int64_t cvtt64(double v){if(!std::isfinite(v)||v>=9223372036854775808.||v< -9223372036854775808.)return INT64_MIN;return int64_t(v);}
int32_t cvtt32(double v){if(!std::isfinite(v)||v>=2147483648.||v< -2147483648.)return INT32_MIN;return int32_t(v);}
double halfAway(double v){return v>=0?v+.5:v-.5;}
}
namespace {
Row intersectConstraints(const Row &a,const Row &b) {
    Row out;size_t i=0,j=0;
    while(i<a.size() && j<b.size()) {
        const auto low=std::max(a[i][0],b[j][0]),high=std::min(a[i][1],b[j][1]);
        if(low<=high)out.push_back({low,high});
        if(a[i][1]<b[j][1])++i;else if(b[j][1]<a[i][1])++j;else{++i;++j;}
    }
    return out;
}
}
void buildStripMask(EvaluatedStripMask &mask,const Row &intervals,int64_t pitch,int64_t origin,int64_t left) {
    require(!intervals.empty() && pitch!=0,PN2S_INVALID,"Strip mask requires nonempty intervals and nonzero pitch");
    const auto strip=[&](int64_t x) {
        const auto relative=std::bit_cast<int64_t>(uint64_t(x)-uint64_t(origin)+uint64_t(left));
        require(!(relative==INT64_MIN && pitch==-1),PN2S_INVALID,"Strip mask division overflow");
        auto value=relative/pitch;if(relative<0 && relative%pitch!=0)--value;
        return std::bit_cast<int32_t>(uint32_t(value));
    };
    mask.first=strip(intervals.front()[0]);mask.end=add32(strip(intervals.back()[1]),1);
    const auto count=sub32(mask.end,mask.first);if(count<=0)return;
    require(count<=10000000,PN2S_RESOURCE_LIMIT,"Strip constraint mask exceeds research resource limit");
    const auto resize=[&](std::vector<uint32_t> &words) {
        words.resize((size_t(count)+31)/32,0);
        if(count%32)words.back()&=(uint32_t(1)<<(count%32))-1U;
    };
    resize(mask.accepted);resize(mask.keepRow);
    const auto set=[&](std::vector<uint32_t> &words,int32_t first,int32_t last) {
        require(first>=0 && first<=last && last<count,PN2S_INVALID,"Strip interval wraps outside mask storage");
        const size_t a=size_t(first)/32,b=size_t(last)/32;
        const uint32_t low=~uint32_t(0)<<(first%32),high=last%32==31?~uint32_t(0):(uint32_t(1)<<(last%32+1))-1U;
        if(a==b){words[a]|=low&high;return;}
        words[a]|=low;for(size_t i=a+1;i<b;++i)words[i]=~uint32_t(0);words[b]|=high;
    };
    for(const auto &interval:intervals) {
        const auto first=sub32(strip(interval[0]),mask.first),last=sub32(strip(interval[1]),mask.first);
        set(mask.accepted,first,last);set(mask.keepRow,first,last);
        mask.keepRow[size_t(first)/32]&=~(uint32_t(1)<<(first%32));
    }
}
void filterConstraintIntervals(std::span<const std::shared_ptr<IntervalConstraint>> constraints,uintptr_t sheet,Row &intervals,uintptr_t geometry,bool includeOptional) {
    for(const auto &entry:constraints) {
        require(bool(entry),PN2S_INVALID,"Null interval constraint");
        auto retained=entry;
        if(includeOptional || !retained->optional24) {
            require(bool(retained->filter),PN2S_INVALID,"Missing interval filter provider");
            retained->filter(sheet,intervals,geometry);if(intervals.empty())break;
        }
    }
}
StripIntervalResult buildStripIntervals(const StripConstraintCache &cache) {
    const auto coordinate=[&](int32_t index){return std::bit_cast<int64_t>(uint64_t(int64_t(index))*uint64_t(cache.pitch)-uint64_t(cache.moduleLeft)+uint64_t(cache.originX));};
    const std::array<int64_t,2> initial{coordinate(cache.first18),coordinate(add32(cache.mask.evaluatedUntil,1))};
    StripIntervalResult out;out.intervals.push_back(initial);
    filterConstraintIntervals(cache.constraints,cache.sheet,out.intervals,cache.geometry,true);
    out.unchanged=out.intervals.size()==1 && out.intervals.front()==initial;return out;
}
void refreshStripMask(StripConstraintCache &cache,int32_t end) {
    const auto previous=cache.mask.evaluatedUntil;
    if(end<=previous || previous==cache.limit1c)return;
    const auto delta=add32(sub32(end,previous),2);
    cache.batch24=std::bit_cast<int32_t>(uint32_t(delta)*3U);
    cache.mask.evaluatedUntil=std::min(cache.limit1c,end);
    const auto filtered=buildStripIntervals(cache);
    buildStripMask(cache.mask,filtered.intervals,cache.pitch,cache.originX,cache.moduleLeft);
}
bool acceptsStrip(StripConstraintCache &cache,int32_t strip) {
    if(strip>=cache.mask.evaluatedUntil)refreshStripMask(cache,add32(cache.batch24,strip));
    const auto &mask=cache.mask;if(strip<mask.first || strip>=mask.end)return false;
    const size_t offset=size_t(int64_t(strip)-mask.first);
    require(offset/32<mask.accepted.size(),PN2S_INVALID,"Constraint mask storage is incomplete");
    return ((mask.accepted[offset/32]>>(offset%32))&1)!=0;
}
Row refineVerticalConstraints(const VerticalConstraintCache &cache,int32_t strip,const Row &row) {
    Row allowed=cache.baseIntervals;
    if(!cache.constraints.empty()) {
        const auto x=std::bit_cast<int64_t>(uint64_t(int64_t(strip))*uint64_t(cache.pitch)-uint64_t(cache.moduleLeft)+uint64_t(cache.originX));
        const std::array<int64_t,2> range{x,std::bit_cast<int64_t>(uint64_t(x)+uint64_t(cache.pitch))};
        for(auto &r:allowed)for(auto &y:r){require(y>=INT32_MIN && y<=INT32_MAX,PN2S_UNSUPPORTED,"Vertical interval exceeds original 32-bit range");y=add32(int32_t(y),cache.yOffset);}
        for(const auto &constraint:cache.constraints) {
            require(constraint && bool(constraint->filter),PN2S_INVALID,"Missing moving vertical constraint");
            constraint->filter(cache.sheet,allowed,cache.geometry,range);if(allowed.empty())break;
        }
        for(auto &r:allowed)for(auto &y:r){require(y>=INT32_MIN && y<=INT32_MAX,PN2S_UNSUPPORTED,"Vertical filter output exceeds original 32-bit range");y=sub32(int32_t(y),cache.yOffset);}
    }
    return intersectConstraints(row,allowed);
}
int32_t constrainedEndpoint(const Rows &rows,int32_t strip,bool lower,const EvaluatedMapConstraints &constraints) {
    require(!(constraints.horizontal && constraints.lazyHorizontal),PN2S_INVALID,"Choose evaluated or lazy horizontal constraints");
    require(!(constraints.vertical && constraints.movingVertical),PN2S_INVALID,"Choose fixed or moving vertical constraints");
    const auto *mask=constraints.lazyHorizontal?&constraints.lazyHorizontal->mask:constraints.horizontal;
    if(mask) {
        if(constraints.lazyHorizontal){if(!acceptsStrip(*constraints.lazyHorizontal,strip))return INT32_MIN;}
        else require(strip<mask->evaluatedUntil,PN2S_UNSUPPORTED,"Strip constraint requires lazy mask evaluation");
        if(strip<mask->first || strip>=mask->end)return INT32_MIN;
        const size_t offset=size_t(int64_t(strip)-mask->first);
        require(offset/32<mask->accepted.size() && offset/32<mask->keepRow.size(),PN2S_INVALID,"Constraint mask storage is incomplete");
        if(((mask->accepted[offset/32]>>(offset%32))&1)==0)return INT32_MIN;
    }
    Row row;if(strip>=0 && size_t(strip)<rows.size())row=rows[size_t(strip)];
    if(mask) {
        if(constraints.lazyHorizontal && strip>=mask->evaluatedUntil)refreshStripMask(*constraints.lazyHorizontal,add32(constraints.lazyHorizontal->batch24,strip));
        const size_t offset=size_t(int64_t(strip)-mask->first);const auto next=add32(strip,1);
        if(((mask->keepRow[offset/32]>>(offset%32))&1)==0 && next>=0 && size_t(next)<rows.size())row=intersectConstraints(row,rows[size_t(next)]);
    }
    if(row.empty())return INT32_MIN;
    if(const auto *vertical=constraints.movingVertical?&constraints.movingVertical->baseIntervals:constraints.vertical) {
        if(constraints.movingVertical && !constraints.movingVertical->constraints.empty())row=refineVerticalConstraints(*constraints.movingVertical,strip,row);
        row=intersectConstraints(row,*vertical);
        if(row.empty())return -1000; // Original output sentinel, distinct from an empty source row.
    }
    const auto y=lower?row.front()[0]:row.back()[1];
    require(y>=INT32_MIN && y<=INT32_MAX,PN2S_UNSUPPORTED,"Constraint endpoint exceeds original 32-bit range");return int32_t(y);
}
namespace {
std::vector<StripPoint> extremalPointsImpl(const Rows &rows,const ExtremalInput &v,const EvaluatedMapConstraints *constraints) {
    require(v.first>=0 && v.end>=0 && size_t(v.end)<=rows.size(),PN2S_INVALID,"Extremal scan range outside map");
    require(v.count>0,PN2S_INVALID,"Extremal rank capacity must be positive");
    if(v.end<=v.first)return {};
    auto endpoint=[&](int32_t i) {
        if(constraints)return constrainedEndpoint(rows,i,v.lower,*constraints);
        const auto &r=rows.at(size_t(i));
        if(r.empty())return INT32_MIN;
        const auto y=v.lower?r.front()[0]:r.back()[1];
        require(y>=INT32_MIN && y<=INT32_MAX,PN2S_UNSUPPORTED,"Map endpoint exceeds original 32-bit range");
        return int32_t(y);
    };
    struct Rank {StripPoint point;int32_t key;};
    std::vector<Rank> ranked;
    const size_t capacity=size_t(std::min(v.count,10));
    auto better=[&](int32_t a,int32_t b){return v.lower?a<b:a>b;};
    auto consider=[&](int32_t i,int32_t y,int32_t key) {
        const Rank item{{i,y},key};
        if(ranked.size()<capacity)ranked.push_back(item);
        else if(better(key,ranked.back().key))ranked.back()=item;
        else return;
        // 3dc410/3dc3b0 exchange the last slot with each better-ranked slot.
        // Stable insertion changes the order of equal-key points.
        for(size_t j=0;j+1<ranked.size();++j)
            if(better(ranked.back().key,ranked[j].key))std::swap(ranked.back(),ranked[j]);
    };
    int32_t first=v.first,last=sub32(v.end,1);
    if(v.fromLow) {
        const auto limited=add32(first,v.maxSteps);
        if(sub32(limited,1)<last)last=sub32(limited,1);
    } else first=std::max(first,sub32(v.end,v.maxSteps));
    if(first<=last) {
        const int32_t initial=v.lower?-1073741824:1073741823;
        int32_t previous=initial,index=v.fromLow?first:last,finish=v.fromLow?last:first;
        const int32_t step=v.fromLow?1:-1;
        auto y=endpoint(index);
        while(index!=finish) {
            const auto nextIndex=add32(index,step),next=endpoint(nextIndex);
            if(y<0)previous=initial;
            else {
                const auto delta=next<0?0:sub32(next,y);
                consider(index,y,sub32(previous,delta));previous=delta;
            }
            index=nextIndex;y=next;
        }
        if(y>=0)consider(index,y,previous);
    }
    std::vector<StripPoint> out;
    for(const auto &r:ranked)out.push_back(r.point);
    if(constraints)return out;
    // Supplement walks forward even for a reverse extremal pass. The first
    // multi-interval row at/before the cutoff ends the scan; it is not skipped.
    const int32_t supplementLast=std::min(sub32(v.end,1),v.supplementEnd);
    require(supplementLast>=v.first,PN2S_INVALID,"Supplement limit precedes first strip");
    for(int32_t i=v.first;i<=supplementLast;++i) {
        if(rows[size_t(i)].size()<2)continue;
        if(i<=add32(v.first,v.maxSteps))break;
        const auto y=endpoint(i);
        if(y!=INT32_MIN){out.push_back({i,y});break;}
    }
    return out;
}
}
std::vector<StripPoint> extremalPoints(const Rows &rows,const ExtremalInput &v){return extremalPointsImpl(rows,v,nullptr);}
std::vector<StripPoint> constrainedExtremalPoints(const Rows &rows,const ExtremalInput &v,const EvaluatedMapConstraints &constraints){return extremalPointsImpl(rows,v,&constraints);}
std::optional<StripPoint> firstPlacement(const FirstPlacementInput &v) {
    require(v.pitch!=0,PN2S_INVALID,"Zero fallback grid pitch");
    double x=0,y=0,width=v.width,height=v.height;
    if(v.sheetBounds){x=v.sheetBounds->x0;y=v.sheetBounds->y0;width=v.sheetBounds->x1-x;height=v.sheetBounds->y1-y;}
    const double h=v.partBounds.y1-v.partBounds.y0;
    if(!(std::abs(height-h)<1e-9) && h>height)return {};
    const auto left=cvtt64(halfAway(x*1000000.)),relative=sub64(left,v.originX);
    require(!(relative==INT64_MIN && v.pitch==-1),PN2S_INVALID,"Fallback division overflow");
    const auto quotient=relative/v.pitch;
    int32_t index=std::bit_cast<int32_t>(uint32_t(quotient));
    if(relative<0 && relative%v.pitch!=0)index=sub32(index,1);
    if(gridX(index,v.pitch,v.originX)!=left) {
        index=add32(index,1);
        // Original subtracts left before adding origin, both in 64-bit ticks.
        const auto correction=sub64(gridX(index,v.pitch,v.originX),left);
        width-=double(correction)/1000000.;
    }
    const double w=v.partBounds.x1-v.partBounds.x0;
    if(!(std::abs(width-w)<1e-9) && w>width)return {};
    return StripPoint{index,sub32(cvtt32(halfAway(y*1000.)),v.originY)};
}
}
