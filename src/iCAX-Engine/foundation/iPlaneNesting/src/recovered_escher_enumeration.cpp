#include "recovered_escher_enumeration.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int32_t narrow(uint64_t value){return std::bit_cast<int32_t>(uint32_t(value));}
uint64_t rowsFitting(int64_t height,int64_t top,int64_t step){return height<top?0:uint64_t(sub(height,top)/step)+1;}
bool keyLess(const std::array<int64_t,3> &a,const std::array<int64_t,3> &b){return std::tie(a[0],a[2],a[1])<std::tie(b[0],b[2],b[1]);}
class SingleRowPointerSort {
    using Pointer=EscherRepeatRow *;std::vector<Pointer> &items;
    static bool before(Pointer a,Pointer b){return std::tie(a->x,a->y)<std::tie(b->x,b->y);}
    void insertion(size_t first,size_t last){for(size_t n=first+1;n<last;++n){const auto item=items[n];size_t j=n;if(before(item,items[first])){std::move_backward(items.begin()+first,items.begin()+n,items.begin()+n+1);j=first;}else while(j>first&&before(item,items[j-1])){items[j]=items[j-1];--j;}items[j]=item;}}
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
    void down(size_t base,size_t hole,size_t count,Pointer item){const auto top=hole,limit=(count-1)/2;
        while(hole<limit){size_t child=hole*2+1;if(!before(items[base+child+1],items[base+child]))++child;items[base+hole]=items[base+child];hole=child;}
        if(hole==limit&&count%2==0){items[base+hole]=items[base+count-1];hole=count-1;}
        while(top<hole){const auto parent=(hole-1)/2;if(!before(items[base+parent],item))break;items[base+hole]=items[base+parent];hole=parent;}items[base+hole]=item;
    }
    void heap(size_t first,size_t last){size_t count=last-first;for(size_t n=count/2;n>0;--n)down(first,n-1,count,items[first+n-1]);while(count>1){const auto item=items[first+count-1];items[first+count-1]=items[first];down(first,0,--count,item);}}
    void sort(size_t first,size_t last,size_t depth){while(last-first>32){if(!depth){heap(first,last);return;}const auto [a,b]=partition(first,last);depth=(depth>>1)+(depth>>2);if(a-first<last-b){sort(first,a,depth);first=b;}else{sort(b,last,depth);last=a;}}if(first<last)insertion(first,last);}
public:
    explicit SingleRowPointerSort(std::vector<Pointer> &values):items(values){}
    void run(size_t depth){sort(0,items.size(),depth);}
};
}
void escherSortSingleRowPointers(std::vector<EscherRepeatRow *> &rows,size_t depth){SingleRowPointerSort(rows).run(depth==SIZE_MAX?rows.size():depth);}
void escherDistributeSingleRows(int64_t phase,int64_t width,uint64_t rowCount,uint64_t remainder,
    int32_t columns,std::vector<std::vector<EscherRepeatRow>> &rows) {
    (void)rowCount;std::vector<EscherRepeatRow *> ordered;
    for(auto &member:rows)for(auto &row:member){row.x=sub(row.x,phase);if(row.x<0)row.x=add(row.x,width);ordered.push_back(&row);}
    escherSortSingleRowPointers(ordered);
    for(size_t index=0;index<ordered.size();++index)ordered[index]->count=index<remainder?columns:narrow(uint32_t(columns)-1U);
}
EscherPatternLayout escherSingleWidthPattern(std::span<const EscherPatternRecord> records,std::span<const EscherPeriodCandidate> candidates,
    EscherIntegerBox rectangle,uint64_t required,std::array<int64_t,3> &key,std::span<const EscherRecordGeometry> geometries,
    int32_t restrictionDegrees) {
    require(records.size()>=1&&records.size()<=2&&records.size()==geometries.size(),PN2S_INVALID,"Escher same-width pattern requires one or two members");
    std::optional<size_t> selected;std::vector<std::vector<EscherRepeatRow>> selectedRows;std::vector<EscherIntegerPoint> selectedOffsets;
    int64_t bestLength=key[0],bestNext=key[1];
    for(size_t candidateIndex=0;candidateIndex<candidates.size();++candidateIndex) {
        const auto &candidate=candidates[candidateIndex];
        require(candidate.boxes.size()==records.size()&&candidate.offsets.size()==records.size()&&candidate.width>0&&candidate.step[1]>0,
            PN2S_INVALID,"Escher same-width candidate has incomplete geometry");
        if(restrictionDegrees!=0) {
            const int32_t angle=(restrictionDegrees%360)*10000;const double radians=double(angle)/10000.*.017453292519943295;
            const auto &u=candidate.basis[0];const double bearing=std::atan2(double(u[0])*0.-double(u[1]),double(u[1])*0.+double(u[0]));
            if(!(std::fabs(std::fmod(std::fabs(bearing)+.01,std::fabs(radians))-.01)<.001))continue;
        }
        const auto &first=candidate.boxes[0];const uint64_t preliminary=rowsFitting(rectangle[3],sub(first[3],first[1]),candidate.step[1])*records.size();
        if(!preliminary)continue;
        uint64_t preliminaryColumns=required/preliminary;if(required%preliminary!=0&&required/preliminary!=0)++preliminaryColumns;
        if(sub(add(mul(int64_t(preliminaryColumns-1),candidate.width),first[2]),first[0])>bestLength)continue;
        auto boxes=candidate.boxes;
        for(size_t member=0;member<boxes.size();++member)for(size_t coordinate=0;coordinate<4;++coordinate)
            boxes[member][coordinate]=add(boxes[member][coordinate],candidate.offsets[member][coordinate%2]);
        std::vector<uint64_t> counts,adjusted(records.size());uint64_t rowTotal=0;
        for(const auto &box:boxes){counts.push_back(rowsFitting(rectangle[3],box[3],candidate.step[1]));rowTotal+=counts.back();}
        bool extra=false;
        if(counts.size()==2) {
            const bool origin=boxes[0][0]==0&&boxes[0][1]==0;const size_t other=origin?1:0,base=origin?0:1;
            if(counts[other]<counts[base]){adjusted[0]=adjusted[1]=counts[base];extra=true;}
            else {
                adjusted[other]=counts[other];adjusted[base]=rowsFitting(add(rectangle[3],boxes[other][1]),boxes[base][3],candidate.step[1]);
                extra=adjusted[base]==counts[base]+1;
            }
        } else adjusted[0]=counts[0];
        if(!rowTotal)continue;
        uint64_t columns=required/rowTotal+1,remainder=required%rowTotal;
        if(columns>1&&(remainder==required||remainder==0)){remainder=rowTotal;--columns;}
        const int64_t baseLength=sub(add(mul(int64_t(columns-1),candidate.width),boxes[0][2]),boxes[0][0]);if(baseLength>bestLength)continue;
        auto rows=escherSingleRowPoints(boxes,adjusted,candidate.width,candidate.step);
        bool swapped=extra&&(boxes[0][0]!=0||boxes[0][1]!=0);if(swapped)std::swap(rows[0],rows[1]);
        const bool equalCounts=counts.size()!=2||counts[0]==counts[1];const size_t tailMember=equalCounts?0:1;
        require(!rows[0].empty()&&!rows[tailMember].empty(),PN2S_INVALID,"Escher same-width candidate has no boundary rows");
        const auto samePoint=[](const EscherRepeatRow &a,const EscherRepeatRow &b){return a.x==b.x&&a.y==b.y;};
        if(extra&&samePoint(rows[0].front(),rows[tailMember].back())){extra=false;rows[equalCounts?0:1].pop_back();}
        std::vector<int64_t> xs;for(const auto &member:rows)for(const auto &row:member)xs.push_back(row.x);
        int64_t spread=INT64_MAX,phase=0;bool useFirst=true;
        if(!extra) {
            if(xs.size()!=1&&remainder!=1) {
                std::sort(xs.begin(),xs.end());const auto initial=xs;
                require(remainder>0&&remainder<=initial.size()+1,PN2S_INVALID,"Escher same-width partial column exceeds rows");
                for(size_t index=0;index<size_t(remainder-1);++index)xs.push_back(add(initial[index],candidate.width));
                int64_t next=INT64_MAX;
                for(size_t index=0;index<size_t(rowTotal);++index) {
                    const int64_t distance=sub(xs[index+size_t(remainder)-1],xs[index]);
                    if(distance<spread||(distance==spread&&xs[index+1]<next)){spread=distance;next=xs[index+1];phase=xs[index];}
                }
            } else spread=0;
        } else {
            const uint64_t extended=adjusted[0]+adjusted[1];int64_t boundaryFirst=rows[0].front().x,boundaryLast=rows[tailMember].back().x;
            if(!xs.empty()) {
                std::sort(xs.begin(),xs.end());const auto initial=xs;
                require(remainder>0&&remainder<=initial.size(),PN2S_INVALID,"Escher same-width extended column exceeds rows");
                for(size_t index=0;index<size_t(remainder);++index)xs.push_back(add(initial[index],candidate.width));
                int64_t next=INT64_MAX;
                for(size_t index=0;index<size_t(extended);++index) {
                    const int64_t start=xs[index];if(boundaryFirst<start)boundaryFirst=add(boundaryFirst,candidate.width);
                    if(boundaryLast<start)boundaryLast=add(boundaryLast,candidate.width);
                    int64_t end=xs[index+size_t(remainder)-1];if(boundaryFirst<=end&&boundaryLast<=end)end=xs[index+size_t(remainder)];
                    const int64_t distance=sub(end,start);
                    if(distance<spread||(distance==spread&&xs[index+1]<next)) {
                        if(boundaryFirst>=start&&boundaryFirst<=end)useFirst=true;
                        else if(boundaryLast>=start&&boundaryLast<=end)useFirst=false;
                        next=xs[index+1];spread=distance;phase=start;
                    }
                }
                if(!useFirst){std::swap(rows[0].front(),rows[0].back());rows[0].pop_back();}
                else rows[equalCounts?0:1].pop_back();
            } else rows[equalCounts?0:1].pop_back();
        }
        const int64_t length=add(baseLength,spread);const auto nextPosition=std::upper_bound(xs.begin(),xs.end(),phase);
        const int64_t next=nextPosition==xs.end()?0:*nextPosition;
        if(length>rectangle[2]||std::tie(length,next)>=std::tie(bestLength,bestNext))continue;
        escherDistributeSingleRows(phase,candidate.width,rowTotal,remainder,narrow(columns),rows);
        int64_t area=sub(mul(candidate.basis[1][1],candidate.basis[0][0]),mul(candidate.basis[0][1],candidate.basis[1][0]));if(area<0)area=sub(0,area);
        const std::array<int64_t,3> trialKey{length,next,area};if(!keyLess(trialKey,key))continue;
        auto offsets=candidate.offsets;for(size_t member=0;member<offsets.size();++member)offsets[member][0]=sub(offsets[member][0],boxes[member][0]);
        if(!useFirst) {
            const int64_t y=boxes[swapped?0:1][1];for(auto &offset:offsets)offset[1]=sub(offset[1],y);
        }
        if(swapped)std::swap(rows[0],rows[1]);
        key=trialKey;bestLength=length;bestNext=next;selected=candidateIndex;selectedRows=std::move(rows);selectedOffsets=std::move(offsets);
    }
    if(!selected)return {};
    for(auto &offset:selectedOffsets){offset[0]=add(offset[0],rectangle[0]);offset[1]=add(offset[1],rectangle[1]);}
    return escherLayoutFromRows(records,candidates[*selected],selectedRows,selectedOffsets,geometries);
}
bool escherLayoutRotationAccept(std::vector<EscherPatternRecord> &records,int32_t degrees,std::span<const EscherRecordGeometry> geometries) {
    require(degrees!=0&&records.size()==geometries.size(),PN2S_INVALID,"Escher layout rotation restriction requires nonzero degrees and matching geometry");
    for(size_t index=0;index<records.size();++index) {
        auto &record=records[index];const int32_t near=narrow(uint32_t(record.angle/10000)+uint32_t(degrees/2));
        int32_t snapped=narrow(uint32_t(narrow(uint32_t(near)-uint32_t(near%degrees))%360)*10000U)%3600000;if(snapped<0)snapped+=3600000;
        const double difference=std::fabs(double(snapped)/10000.-double(record.angle)/10000.);
        if(.06<std::min(difference,360.-difference))return false;
        record.angle=snapped;escherUpdateRecordBounds(record,geometries[index].intrinsic,geometries[index].collision);
    }
    return true;
}
EscherIntegerBox escherTransposeRectangle(EscherIntegerBox box){return {sub(0,box[3]),box[0],sub(0,box[1]),box[2]};}
}
