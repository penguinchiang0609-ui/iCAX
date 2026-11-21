#include "recovered_escher_conversion.hpp"
#include <bit>
namespace study::recovered {
namespace {
int32_t truncate32(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
int64_t truncate64(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
Point quantized(Point point){const double x=point.x*1000000.,y=point.y*1000.;return {double(truncate64(x<0?x-.5:x+.5))/1000000.,double(truncate32(y<0?y-.5:y+.5))/1000.};}
}
EscherConvertedPose escherProviderPose(const EscherPatternRecord &record,Point reference,double scale,
    Box originalBounds,bool symmetryX,bool symmetryY){
    require(std::isfinite(scale)&&scale>0,PN2S_INVALID,"Escher provider conversion requires the actual positive integer-job scale");
    const double angle=(double(record.angle)/10000.)*100000.;const int32_t quantized=truncate32(angle<0?angle-.5:angle+.5);
    PlacementOrientation requested{quantized,0,0};if(record.reflected)requested.angle=quantized==0?-36000000:std::bit_cast<int32_t>(0u-uint32_t(quantized));
    const auto matrix=reverseOrientationMatrix({quantized,0,0});reference.x-=matrix[4];reference.y-=matrix[5];
    const auto actual=canonicalOrientation(requested,symmetryX,symmetryY);const auto offset=reverseVariantOffset({originalBounds.x0,originalBounds.y0,originalBounds.x1,originalBounds.y1},requested,actual);
    return {actual,{(double(record.x)/scale-((reference.x*matrix[1]-reference.y*matrix[2])+matrix[4]))+offset.x,
        (double(record.y)/scale-((reference.y*matrix[1]+reference.x*matrix[2])*matrix[3]+matrix[5]))+offset.y}};
}
std::vector<TilingMember> escherConvertProviderLayout(std::span<const EscherPatternRecord> records,const EscherProviderConversionGeometry &geometry){
    require(bool(geometry.sourceReference)&&bool(geometry.variantBounds),PN2S_INVALID,"Escher provider conversion needs real source references and actual Part variant bounds");std::vector<TilingMember> output;output.reserve(records.size());
    for(const auto &record:records){auto pose=escherProviderPose(record,geometry.sourceReference(record.geometry),geometry.scale,geometry.originalBounds,geometry.symmetryX,geometry.symmetryY);pose.position=quantized(pose.position);const auto box=geometry.variantBounds(pose.orientation);output.push_back({geometry.part,pose.orientation,pose.position,{box.x0+pose.position.x,box.y0+pose.position.y,box.x1+pose.position.x,box.y1+pose.position.y},-1});}
    escherSortProviderPlacements(output);return output;
}
}

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
}
void escherSortProviderPlacements(std::vector<TilingMember> &items,size_t depth){CandidateSort(items,[](const auto &a,const auto &b){return a.box.x1<b.box.x1;}).run(depth==SIZE_MAX?items.size():depth);}
}
