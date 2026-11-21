#include "recovered_shaker_geometry.hpp"
#include <algorithm>
#include <stdexcept>
namespace study::recovered {
namespace {
class BoundarySort {
    std::span<const ShakerBoundary> records;
    std::vector<int32_t> &order;
    double score(size_t n)const{return records[size_t(order[n])].distance;}
    void insertion(size_t first,size_t last){
        for(size_t i=first+1;i<last;++i){const auto item=order[i];const double value=score(i);size_t j=i;
            if(score(first)>value){std::move_backward(order.begin()+first,order.begin()+i,order.begin()+i+1);j=first;}
            else while(j>first&&value<score(j-1)){order[j]=order[j-1];--j;}
            order[j]=item;
        }
    }
    void median(size_t first,size_t mid,size_t last){
        if(score(mid)<score(first))std::swap(order[mid],order[first]);
        if(score(last)<score(mid)){std::swap(order[last],order[mid]);if(score(mid)<score(first))std::swap(order[mid],order[first]);}
    }
    std::pair<size_t,size_t> partition(size_t first,size_t last){
        const auto mid=first+(last-first)/2;
        if(last-first>41){const auto step=(last-first)/8;median(first,first+step,first+2*step);median(mid-step,mid,mid+step);median(last-1-2*step,last-1-step,last-1);median(first+step,mid,last-1-step);}
        else median(first,mid,last-1);
        size_t pfirst=mid,plast=mid+1;
        while(first<pfirst&&!(score(pfirst-1)<score(pfirst))&&!(score(pfirst)<score(pfirst-1)))--pfirst;
        while(plast<last&&!(score(plast)<score(pfirst))&&!(score(pfirst)<score(plast)))++plast;
        size_t right=plast,left=pfirst;
        for(;;){
            for(;right<last;++right){if(score(pfirst)<score(right))continue;if(score(right)<score(pfirst))break;if(plast!=right)std::swap(order[plast],order[right]);++plast;}
            for(;first<left;--left){if(score(left-1)<score(pfirst))continue;if(score(pfirst)<score(left-1))break;--pfirst;if(pfirst!=left-1)std::swap(order[pfirst],order[left-1]);}
            if(left==first&&right==last)return {pfirst,plast};
            if(left==first){if(plast!=right)std::swap(order[pfirst],order[plast]);++plast;std::swap(order[pfirst++],order[right++]);}
            else if(right==last){--left;--pfirst;if(left!=pfirst)std::swap(order[left],order[pfirst]);std::swap(order[pfirst],order[--plast]);}
            else std::swap(order[right++],order[--left]);
        }
    }
    void down(size_t base,size_t hole,size_t count,int32_t item){
        const auto top=hole,limit=(count-1)/2;
        while(hole<limit){size_t child=hole*2+1;if(!(score(base+child)>score(base+child+1)))++child;order[base+hole]=order[base+child];hole=child;}
        if(hole==limit&&count%2==0){order[base+hole]=order[base+count-1];hole=count-1;}
        while(top<hole){const auto parent=(hole-1)/2;if(!(records[size_t(item)].distance>score(base+parent)))break;order[base+hole]=order[base+parent];hole=parent;}
        order[base+hole]=item;
    }
    void heap(size_t first,size_t last){
        size_t n=last-first;for(size_t i=n/2;i>0;--i)down(first,i-1,n,order[first+i-1]);
        while(n>1){const auto item=order[first+n-1];order[first+n-1]=order[first];down(first,0,--n,item);}
    }
    void sort(size_t first,size_t last,size_t depth){
        while(last-first>32){if(!depth){heap(first,last);return;}const auto [a,b]=partition(first,last);depth=(depth>>1)+(depth>>2);
            if(a-first<last-b){sort(first,a,depth);first=b;}else{sort(b,last,depth);last=a;}}
        if(first<last)insertion(first,last);
    }
public:
    BoundarySort(std::span<const ShakerBoundary> b,std::vector<int32_t> &o):records(b),order(o){}
    void run(size_t depth){sort(0,order.size(),depth);}
};
}
void shakerBoundaryOrder(std::span<const ShakerBoundary> b,std::vector<int32_t> &order,size_t depth){
    for(auto n:order)if(n<0||size_t(n)>=b.size())throw std::out_of_range("Invalid Shaker boundary sort index");
    BoundarySort(b,order).run(depth==SIZE_MAX?order.size():depth);
}
}
