#pragma once
namespace study::recovered::detail {
// Original MSVC insertion/median/ninther/equal-partition/heap ordering, including ties.
template<class T,class Before> class AmoCandidateSort {
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
    AmoCandidateSort(std::vector<T> &values,Before predicate):items(values),before(predicate){}
    void run(size_t depth){sort(0,items.size(),depth);}
};
}
