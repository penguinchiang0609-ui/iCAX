#include "recovered_multiheads_infer.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>

namespace study::recovered {
namespace {
class InferSort {
    std::span<const int32_t> values;
    std::vector<size_t> order;
    bool before(size_t a,size_t b)const{return values[a]<values[b];}
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
    InferSort(std::span<const int32_t> p):values(p),order(p.size()){std::iota(order.begin(),order.end(),0);}
    std::vector<size_t> run(size_t depth){sort(0,order.size(),depth);return std::move(order);}
};
}
std::vector<size_t> multiheadsInferOrder(std::span<const int32_t> heights,std::optional<size_t> depth) {
    return InferSort(heights).run(depth.value_or(heights.size()));
}

std::vector<std::vector<size_t>> multiheadsInferGroups(
    std::span<const MultiheadsInferPlacement> placements,const MultiheadsHardware &hardware) {
    std::vector<std::vector<size_t>> result;
    if(hardware.heads48<2)return result;
    std::map<std::pair<int64_t,int64_t>,std::vector<size_t>> buckets;
    for(size_t i=0;i<placements.size();++i) {
        const auto &p=placements[i];
        if(!p.hasNonemptyGroup58)buckets[{p.x48,p.moduleD0}].push_back(i);
    }
    const auto difference=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));};
    for(auto &[key,indexes]:buckets) {
        if(indexes.size()<2)continue;
        std::vector<int32_t> heights;heights.reserve(indexes.size());
        for(const auto i:indexes)heights.push_back(placements[i].y50);
        const auto order=multiheadsInferOrder(heights);const auto old=indexes;
        for(size_t i=0;i<order.size();++i)indexes[i]=old[order[i]];
        for(size_t first=0;first+1<indexes.size();++first) {
            const auto delta=difference(placements[indexes[first]].y50,placements[indexes[first+1]].y50);
            const auto gap=std::abs(double(delta)/1000.);
            // COMISD/JA skips on ordered greater only; unordered limits survive.
            if(gap>hardware.maximumGap60 || hardware.minimumGap58>gap)continue;
            size_t next=first+1;std::vector<size_t> members{indexes[first]};
            while(next<indexes.size() && members.size()<size_t(hardware.heads48) &&
                difference(placements[indexes[next-1]].y50,placements[indexes[next]].y50)==delta)
                members.push_back(indexes[next++]);
            if(hardware.fixedDivisions50>0 && members.size()!=size_t(hardware.fixedDivisions50))continue;
            if(members.size()>1)result.push_back(std::move(members));
            // Original 3f681d..3f6848 assigns the next cursor, then increments
            // it once more in the outer loop. Preserve that skipped slot.
            first=next;
        }
    }
    return result;
}
}

