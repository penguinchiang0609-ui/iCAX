#include "recovered_escher_candidate_sort.hpp"
namespace study::recovered {
namespace {
template<class T,class Before> class CandidateSort {
    std::vector<T> &items;Before before;
    void insertion(size_t first,size_t last){for(size_t n=first+1;n<last;++n){auto item=std::move(items[n]);size_t j=n;if(before(item,items[first])){std::move_backward(items.begin()+first,items.begin()+n,items.begin()+n+1);j=first;}else while(j>first&&before(item,items[j-1])){items[j]=std::move(items[j-1]);--j;}items[j]=std::move(item);}}
    void median(size_t first,size_t middle,size_t last){if(before(items[middle],items[first]))std::swap(items[middle],items[first]);if(before(items[last],items[middle])){std::swap(items[last],items[middle]);if(before(items[middle],items[first]))std::swap(items[middle],items[first]);}}
    std::pair<size_t,size_t> partition(size_t first,size_t last){
        const auto middle=first+(last-first)/2;
        if(last-first>41){const auto step=(last-first)/8;median(first,first+step,first+2*step);median(middle-step,middle,middle+step);median(last-1-2*step,last-1-step,last-1);median(first+step,middle,last-1-step);}else median(first,middle,last-1);
        size_t pivotFirst=middle,pivotLast=middle+1;while(first<pivotFirst&&!before(items[pivotFirst-1],items[pivotFirst])&&!before(items[pivotFirst],items[pivotFirst-1]))--pivotFirst;while(pivotLast<last&&!before(items[pivotLast],items[pivotFirst])&&!before(items[pivotFirst],items[pivotLast]))++pivotLast;
        size_t right=pivotLast,left=pivotFirst;
        for(;;){for(;right<last;++right){if(before(items[pivotFirst],items[right]))continue;if(before(items[right],items[pivotFirst]))break;if(pivotLast!=right)std::swap(items[pivotLast],items[right]);++pivotLast;}
            for(;first<left;--left){if(before(items[left-1],items[pivotFirst]))continue;if(before(items[pivotFirst],items[left-1]))break;--pivotFirst;if(pivotFirst!=left-1)std::swap(items[pivotFirst],items[left-1]);}
            if(left==first&&right==last)return {pivotFirst,pivotLast};
            if(left==first){if(pivotLast!=right)std::swap(items[pivotFirst],items[pivotLast]);++pivotLast;std::swap(items[pivotFirst++],items[right++]);}
            else if(right==last){--left;--pivotFirst;if(left!=pivotFirst)std::swap(items[left],items[pivotFirst]);std::swap(items[pivotFirst],items[--pivotLast]);}
            else std::swap(items[right++],items[--left]);
        }
    }
    void down(size_t base,size_t hole,size_t count,T item){const auto top=hole,limit=(count-1)/2;
        while(hole<limit){size_t child=hole*2+1;if(!before(items[base+child+1],items[base+child]))++child;items[base+hole]=std::move(items[base+child]);hole=child;}
        if(hole==limit&&count%2==0){items[base+hole]=std::move(items[base+count-1]);hole=count-1;}
        while(top<hole){const auto parent=(hole-1)/2;if(!before(items[base+parent],item))break;items[base+hole]=std::move(items[base+parent]);hole=parent;}items[base+hole]=std::move(item);
    }
    void heap(size_t first,size_t last){size_t count=last-first;for(size_t n=count/2;n>0;--n)down(first,n-1,count,std::move(items[first+n-1]));while(count>1){auto item=std::move(items[first+count-1]);items[first+count-1]=std::move(items[first]);down(first,0,--count,std::move(item));}}
    void sort(size_t first,size_t last,size_t depth){while(last-first>32){if(!depth){heap(first,last);return;}const auto [a,b]=partition(first,last);depth=(depth>>1)+(depth>>2);if(a-first<last-b){sort(first,a,depth);first=b;}else{sort(b,last,depth);last=a;}}if(first<last)insertion(first,last);}
public:
    CandidateSort(std::vector<T> &values,Before predicate):items(values),before(predicate){}
    void run(size_t depth){sort(0,items.size(),depth);}
};
int slopeCompare(int64_t ay,double a,int64_t by,double b){if((ay<0)!=(by<0))return ay<0?1:-1;return a<b?-1:b<a?1:0;}
double squared(EscherIntegerPoint point){const double x=double(point[0]),y=double(point[1]);return y*y+x*x;}
}
bool escherCandidateSlopeLess(const EscherPeriodCandidate &a,const EscherPeriodCandidate &b) {
    const auto first=slopeCompare(a.basis[0][1],a.basisSlope,b.basis[0][1],b.basisSlope);if(first)return first<0;
    const auto second=slopeCompare(a.step[1],a.stepSlope,b.step[1],b.stepSlope);if(second)return second<0;
    const auto an=squared(a.basis[0]),bn=squared(b.basis[0]);if(an!=bn)return an<bn;return squared(a.step)<squared(b.step);
}
void escherSortCandidateAreas(std::vector<EscherCandidateAreaIndex> &items,size_t depth) {
    const auto before=[](const auto &a,const auto &b){return std::tie(a.area,a.index)<std::tie(b.area,b.index);};
    CandidateSort(items,before).run(depth==SIZE_MAX?items.size():depth);
}
void escherSortCandidateSlopes(std::vector<EscherPeriodCandidate> &items,size_t depth) {
    CandidateSort(items,escherCandidateSlopeLess).run(depth==SIZE_MAX?items.size():depth);
}
}
