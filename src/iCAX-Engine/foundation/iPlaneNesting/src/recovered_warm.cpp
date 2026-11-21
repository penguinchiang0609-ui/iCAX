#include "recovered_warm.hpp"
#include <bit>
namespace study::recovered {
double warmOrderScore(const WarmPlacement &p,const WarmOrderInput &in) {
    const auto add64=[](int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));};
    const auto add32=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));};
    const double y=in.y-((double(add32(p.topF8,p.y50))/1000.)*.5+(double(add32(p.bottomE8,p.y50))/1000.)*.5);
    const double x=in.x-((double(add64(p.leftE0,p.x48))/1e6)*(1.-in.blend)+(double(add64(p.rightF0,p.x48))/1e6)*in.blend);
    return (x*x+y*y)*double(in.sign);
}
namespace {
class WarmSort {
    std::span<const WarmPlacementPtr> placements;
    const WarmOrderInput &input;
    std::vector<size_t> order;
    bool before(size_t a,size_t b)const{return warmOrderScore(*placements[a],input)>warmOrderScore(*placements[b],input);}
    void insertion(size_t first,size_t last) {
        for(size_t i=first+1;i<last;++i) {
            const auto item=order[i];size_t j=i;
            if(before(item,order[first])) {std::move_backward(order.begin()+first,order.begin()+i,order.begin()+i+1);j=first;}
            else while(j>first && before(item,order[j-1])){order[j]=order[j-1];--j;}
            order[j]=item;
        }
    }
    void median(size_t first,size_t mid,size_t last) {
        if(before(order[mid],order[first]))std::swap(order[mid],order[first]);
        if(before(order[last],order[mid])) {std::swap(order[last],order[mid]);if(before(order[mid],order[first]))std::swap(order[mid],order[first]);}
    }
    std::pair<size_t,size_t> partition(size_t first,size_t last) {
        const size_t mid=first+(last-first)/2;
        if(last-first>41) {const auto step=(last-first)/8;median(first,first+step,first+2*step);median(mid-step,mid,mid+step);median(last-1-2*step,last-1-step,last-1);median(first+step,mid,last-1-step);}
        else median(first,mid,last-1);
        size_t pfirst=mid,plast=mid+1;
        while(first<pfirst && !before(order[pfirst-1],order[pfirst]) && !before(order[pfirst],order[pfirst-1]))--pfirst;
        while(plast<last && !before(order[plast],order[pfirst]) && !before(order[pfirst],order[plast]))++plast;
        size_t right=plast,left=pfirst;
        for(;;) {
            for(;right<last;++right) {
                if(before(order[pfirst],order[right]))continue;
                if(before(order[right],order[pfirst]))break;
                if(plast!=right)std::swap(order[plast],order[right]);++plast;
            }
            for(;first<left;--left) {
                if(before(order[left-1],order[pfirst]))continue;
                if(before(order[pfirst],order[left-1]))break;
                --pfirst;if(pfirst!=left-1)std::swap(order[pfirst],order[left-1]);
            }
            if(left==first && right==last)return {pfirst,plast};
            if(left==first) {
                if(plast!=right)std::swap(order[pfirst],order[plast]);++plast;
                std::swap(order[pfirst++],order[right++]);
            } else if(right==last) {
                --left;--pfirst;if(left!=pfirst)std::swap(order[left],order[pfirst]);std::swap(order[pfirst],order[--plast]);
            } else std::swap(order[right++],order[--left]);
        }
    }
    void down(size_t base,size_t hole,size_t count,size_t item) {
        const auto top=hole,limit=(count-1)/2;
        while(hole<limit) {
            size_t child=hole*2+2;if(before(order[base+child],order[base+child-1]))--child;
            order[base+hole]=order[base+child];hole=child;
        }
        if(hole==limit && count%2==0){order[base+hole]=order[base+count-1];hole=count-1;}
        while(top<hole) {const auto parent=(hole-1)/2;if(!before(order[base+parent],item))break;order[base+hole]=order[base+parent];hole=parent;}
        order[base+hole]=item;
    }
    void heap(size_t first,size_t last) {
        size_t n=last-first;
        for(size_t i=n/2;i>0;--i)down(first,i-1,n,order[first+i-1]);
        while(n>1){const auto item=order[first+n-1];order[first+n-1]=order[first];down(first,0,--n,item);}
    }
    void sort(size_t first,size_t last,size_t depth) {
        while(last-first>32) {
            if(!depth){heap(first,last);return;}
            const auto [a,b]=partition(first,last);depth=(depth>>1)+(depth>>2);
            if(a-first<last-b){sort(first,a,depth);first=b;}else{sort(b,last,depth);last=a;}
        }
        if(first<last)insertion(first,last);
    }
public:
    WarmSort(std::span<const WarmPlacementPtr> p,const WarmOrderInput &in):placements(p),input(in),order(p.size()){std::iota(order.begin(),order.end(),0);}
    std::vector<size_t> run(size_t depth){sort(0,order.size(),depth);return std::move(order);}
};
}
std::vector<size_t> warmPlacementOrder(std::span<const WarmPlacementPtr> p,const WarmOrderInput &in,std::optional<size_t> depth) {
    for(const auto &v:p)if(!v)throw std::invalid_argument("warm order requires valid placements");
    return WarmSort(p,in).run(depth.value_or(p.size()));
}
WarmPlacementPtr clonePlacement(const WarmPlacement &source,const PlacementCloneInput &v,
    const PlacementCopyLookup &lookup,bool &validateOnCommit,const std::function<void(const WarmPlacementPtr&)> &commit) {
    if(!v.pitch || !lookup || !commit)throw std::invalid_argument("placement clone requires pitch, relation map and commit");
    const auto add64=[](int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));};
    const auto sub64=[](int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));};
    const auto x=add64(source.x48,v.dx),left=add64(source.leftE0,x),relative=sub64(left,v.originX);
    if(relative==INT64_MIN && v.pitch==-1)throw std::invalid_argument("placement clone division overflow");
    auto quotient=relative/v.pitch;if(relative<0 && relative%v.pitch!=0)--quotient;
    const auto strip=std::bit_cast<int32_t>(uint32_t(quotient));
    const auto aligned=std::bit_cast<int64_t>(uint64_t(int64_t(strip))*uint64_t(v.pitch)+uint64_t(v.originX));
    auto p=std::make_shared<WarmPlacement>(source);
    p->x48=x;p->y50=std::bit_cast<int32_t>(uint32_t(source.y50)+uint32_t(v.dy));p->rounded2c=aligned!=left;
    p->instance18=v.instance18;p->instance40=v.instance40;p->group08=0;
    p->edges.clear();p->group58.reset();p->groupFirst.reset();
    copyPlacementRelations(source,p,lookup);
    const bool saved=validateOnCommit;validateOnCommit=false;
    // The adapter restores its validation flag on C++ exceptions as well.
    try{commit(p);}catch(...){validateOnCommit=saved;throw;}
    validateOnCommit=saved;return p;
}
WarmApplication applyWarmStart(WarmStartHost &host,const TrialRecordPtr &record,double fraction,bool high) {
    if(!record)throw std::invalid_argument("warm application requires a record");
    const auto source=std::dynamic_pointer_cast<WarmNesting>(record->nesting);
    if(!source)throw std::invalid_argument("warm application requires a layout snapshot");
    auto selected=source->placements;WarmApplication out;
    if(selected.size()<2)return out;
    auto &current=host.current();
    const bool near=current.random.unit()<.5;
    if(source->machineAvailable && source->machineAllowsOrder && 0.>=source->box10 && 0.>=source->box08) {
        WarmOrderInput order;order.blend=current.random.unit()+0.;order.x=source->sheet70;
        if(high==near)order.x=order.x+double(rawCandidateLength(source->maximumRight,source->sheet70))/1e6;
        order.y=source->sheet78+(double(source->integerMeasure)/1000.)*(current.random.unit()+0.);
        order.sign=near?-1:1;
        const auto indices=warmPlacementOrder(selected,order);auto old=selected;
        for(size_t i=0;i<indices.size();++i)selected[i]=old[indices[i]];out.reordered=true;
    }
    if(host.isColumnNesting()) {
        current.columnLimit1540=0;host.resetCurrent();
        if(current.placedCount13d8>0 && current.random.unit()<.5 && current.machineCount1f8>1 && current.machineSpacing1f0<0.) {
            current.columnLimit1540=std::min<int32_t>(current.placedCount13d8,10);host.resetCurrent();
        }
    }
    const double limit=(source->quantity18-current.quantity18)*fraction;
    const auto add=[](double a,double b){
        if(std::isnan(a))return std::bit_cast<double>(std::bit_cast<uint64_t>(a)|0x8000000000000ull);
        if(std::isnan(b))return std::bit_cast<double>(std::bit_cast<uint64_t>(b)|0x8000000000000ull);
        return a+b;
    };
    for(size_t i=static_cast<size_t>(static_cast<int64_t>(current.placedCount13d8));i<selected.size();++i) {
        const auto &p=*selected[i];const double value=p.auxiliary3c1?0.:(p.quantity220<0.?p.area58:p.quantity220);
        out.accumulated=add(out.accumulated,value);
        if(out.accumulated>limit){selected.resize(i);break;}
    }
    out.retained=selected.size();host.removeTrailingPlacements();
    if(!selected.empty()) {
        host.notifyStartPredefined();host.resetSelectionQueue();
        std::vector<WarmPlacementPtr> copied;copied.reserve(selected.size());bool complete=true;
        for(const auto &p:selected) {
            if(!host.instanceAvailable(p->instance18) || !host.acceptsPlacement(*p)){complete=false;copied.push_back({});}
            else copied.push_back(host.cloneAndInsert(*p));
        }
        for(size_t i=0;i<copied.size();++i)if(copied[i]) {++out.copied;current.quantity28=add(host.copyProcesses(*selected[i]),current.quantity28);}
        host.notifyEndPredefined();host.pruneAuxiliaryGroups();
        // Original temporary replay owners are released before the reset.
        copied.clear();
        if(!complete){host.resetCurrent();out.resetAfterFailure=true;}
    }
    const double ratio=out.accumulated/current.totalValue78;
    current.shake14e8=ratio<source->shake14e8?ratio:source->shake14e8; // MINSD keeps the second operand for NaN/ties
    const double config=host.debugColorConfig750();
    const int32_t debug=(!(config>=-2147483648. && config<2147483648.))?INT32_MIN:static_cast<int32_t>(config);
    if(debug)host.colorPlacements(near?"0x3fb2ffff":"0xffb2ffff");
    current.source1518=record->sourceId;current.sourceTrial151c=source->trial13bc;
    current.depth1520=std::bit_cast<int32_t>(uint32_t(source->depth1520)+1u);
    if(&current!=source.get())current.lineage14f8=source->lineage14f8;
    current.lineage14f8.push_back({record->sourceId,source->trial13bc,uint64_t(selected.size())});
    current.fraction1510=fraction;out.applied=true;return out;
}
}
