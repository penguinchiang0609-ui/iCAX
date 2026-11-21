#include "recovered_escher_nfp.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace study::recovered {
int escherCompareAngularKeys(EscherAngularKey a,EscherAngularKey b) {
    if(a.hemisphere!=b.hemisphere)return a.hemisphere<b.hemisphere?-1:1;
    if(a.hemisphere<0){if(a.cosine<b.cosine)return -1;if(b.cosine<a.cosine)return 1;}
    else {if(b.cosine<a.cosine)return -1;if(a.cosine<b.cosine)return 1;}return 0;
}
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int sign(int64_t a){return (a>0)-(a<0);}
ConvolutionIntegerPoint difference(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return {sub(a[0],b[0]),sub(a[1],b[1])};}
int64_t cross(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return sub(mul(a[0],b[1]),mul(a[1],b[0]));}
EscherAngularKey angularKey(ConvolutionIntegerPoint direction) {
    const double x=double(direction[0]),y=double(direction[1]);EscherAngularKey key{x/std::sqrt(y*y+x*x),sign(direction[1])};
    if(key.cosine==-1&&key.hemisphere==0)key.hemisphere=1;return key;
}
class AngularEdgeSort {
    std::vector<EscherAngularEdge> &edges;
    static bool before(const EscherAngularEdge &a,const EscherAngularEdge &b) {
        if(const auto result=escherCompareAngularKeys(a.first,b.first))return result<0;
        if(const auto result=escherCompareAngularKeys(a.last,b.last))return result<0;return a.segment<b.segment;
    }
    void insertion(size_t first,size_t last){for(size_t n=first+1;n<last;++n){auto item=edges[n];size_t j=n;if(before(item,edges[first])){std::move_backward(edges.begin()+first,edges.begin()+n,edges.begin()+n+1);j=first;}else while(j>first&&before(item,edges[j-1])){edges[j]=edges[j-1];--j;}edges[j]=item;}}
    void median(size_t first,size_t middle,size_t last){if(before(edges[middle],edges[first]))std::swap(edges[middle],edges[first]);if(before(edges[last],edges[middle])){std::swap(edges[last],edges[middle]);if(before(edges[middle],edges[first]))std::swap(edges[middle],edges[first]);}}
    std::pair<size_t,size_t> partition(size_t first,size_t last){
        const auto mid=first+(last-first)/2;
        if(last-first>41){const auto step=(last-first)/8;median(first,first+step,first+2*step);median(mid-step,mid,mid+step);median(last-1-2*step,last-1-step,last-1);median(first+step,mid,last-1-step);}else median(first,mid,last-1);
        size_t pfirst=mid,plast=mid+1;while(first<pfirst&&!before(edges[pfirst-1],edges[pfirst])&&!before(edges[pfirst],edges[pfirst-1]))--pfirst;while(plast<last&&!before(edges[plast],edges[pfirst])&&!before(edges[pfirst],edges[plast]))++plast;
        size_t right=plast,left=pfirst;
        for(;;){for(;right<last;++right){if(before(edges[pfirst],edges[right]))continue;if(before(edges[right],edges[pfirst]))break;if(plast!=right)std::swap(edges[plast],edges[right]);++plast;}
            for(;first<left;--left){if(before(edges[left-1],edges[pfirst]))continue;if(before(edges[pfirst],edges[left-1]))break;--pfirst;if(pfirst!=left-1)std::swap(edges[pfirst],edges[left-1]);}
            if(left==first&&right==last)return {pfirst,plast};
            if(left==first){if(plast!=right)std::swap(edges[pfirst],edges[plast]);++plast;std::swap(edges[pfirst++],edges[right++]);}
            else if(right==last){--left;--pfirst;if(left!=pfirst)std::swap(edges[left],edges[pfirst]);std::swap(edges[pfirst],edges[--plast]);}
            else std::swap(edges[right++],edges[--left]);
        }
    }
    void down(size_t base,size_t hole,size_t count,EscherAngularEdge item){const auto top=hole,limit=(count-1)/2;
        while(hole<limit){size_t child=hole*2+1;if(!before(edges[base+child+1],edges[base+child]))++child;edges[base+hole]=edges[base+child];hole=child;}
        if(hole==limit&&count%2==0){edges[base+hole]=edges[base+count-1];hole=count-1;}
        while(top<hole){const auto parent=(hole-1)/2;if(!before(edges[base+parent],item))break;edges[base+hole]=edges[base+parent];hole=parent;}edges[base+hole]=item;
    }
    void heap(size_t first,size_t last){size_t count=last-first;for(size_t n=count/2;n>0;--n)down(first,n-1,count,edges[first+n-1]);while(count>1){auto item=edges[first+count-1];edges[first+count-1]=edges[first];down(first,0,--count,item);}}
    void sort(size_t first,size_t last,size_t depth){while(last-first>32){if(!depth){heap(first,last);return;}const auto [a,b]=partition(first,last);depth=(depth>>1)+(depth>>2);if(a-first<last-b){sort(first,a,depth);first=b;}else{sort(b,last,depth);last=a;}}if(first<last)insertion(first,last);}
public:
    explicit AngularEdgeSort(std::vector<EscherAngularEdge> &input):edges(input){}
    void run(size_t depth){sort(0,edges.size(),depth);}
};
}
void escherSortAngularEdges(std::vector<EscherAngularEdge> &edges,size_t depth){AngularEdgeSort(edges).run(depth==SIZE_MAX?edges.size():depth);}
EscherNfpBoundary escherBuildNfpBoundary(ConvolutionIntegerPoint reference,const ConvolutionIntegerPolygon &rings,double tolerance,double minimumAngle,double maximumAngle) {
    EscherNfpBoundary output;output.tolerance=tolerance;output.reference=reference;if(rings.empty())return output;
    const auto angleKey=[](double angle){const double sine=std::sin(angle);EscherAngularKey key{std::cos(angle),(sine>0)-(sine<0)};if(key.cosine==-1&&key.hemisphere==0)key.hemisphere=1;return key;};
    const auto minimum=angleKey(std::max(-3.141592653589793,minimumAngle-tolerance)),maximum=angleKey(std::min(3.141592653589793,maximumAngle+tolerance));
    const EscherAngularKey negative{-1,-1},positive{-1,1};
    for(const auto &ring:rings) {
        if(ring.empty())throw std::invalid_argument("Escher NFP boundary contains an empty ring");
        auto previous=ring.back();auto previousKey=angularKey(difference(previous,reference));
        for(auto current:ring) {
            const ConvolutionIntegerSegment segment{previous,current};
            if(current==reference){output.edges.push_back({segment,negative,positive});continue;}
            const auto currentKey=angularKey(difference(current,reference));const bool forward=cross(difference(previous,reference),difference(current,reference))>0;
            auto first=forward?previousKey:currentKey,last=forward?currentKey:previousKey;
            first.cosine=first.hemisphere==-1?std::max(-1.,first.cosine-tolerance):std::min(1.,first.cosine+tolerance);
            last.cosine=last.hemisphere==-1?std::max(-1.,last.cosine+tolerance):std::min(1.,last.cosine-tolerance);
            if(escherCompareAngularKeys(last,first)<0) {
                if(escherCompareAngularKeys(minimum,last)<0)output.edges.push_back({segment,negative,last});
                if(escherCompareAngularKeys(first,maximum)<0)output.edges.push_back({segment,first,positive});
            }else if(escherCompareAngularKeys(minimum,last)<0&&escherCompareAngularKeys(first,maximum)<0)output.edges.push_back({segment,first,last});
            previous=current;previousKey=currentKey;
        }
    }
    escherSortAngularEdges(output.edges);for(const auto &edge:output.edges){auto key=edge.last;if(!output.prefixMaximum.empty()&&escherCompareAngularKeys(output.prefixMaximum.back(),key)>=0)key=output.prefixMaximum.back();output.prefixMaximum.push_back(key);}
    return output;
}
size_t escherNfpRayLowerBound(const EscherNfpBoundary &owner,EscherAngularKey key) {
    return size_t(std::lower_bound(owner.prefixMaximum.begin(),owner.prefixMaximum.end(),key,[](auto a,auto b){return escherCompareAngularKeys(a,b)<0;})-owner.prefixMaximum.begin());
}
std::vector<ConvolutionHierarchyRayHit> escherNfpRayIntersections(const EscherNfpBoundary &owner,ConvolutionIntegerPoint direction) {
    std::vector<ConvolutionHierarchyRayHit> output;if(owner.edges.empty())return output;const auto key=angularKey(direction);
    for(size_t index=escherNfpRayLowerBound(owner,key);index<owner.edges.size();++index) {
        const auto &edge=owner.edges[index];if(escherCompareAngularKeys(key,edge.first)<0)break;
        if(escherCompareAngularKeys(edge.last,key)>=0&&sign(cross(direction,difference(edge.segment[0],owner.reference)))!=sign(cross(direction,difference(edge.segment[1],owner.reference)))) {
            ConvolutionHierarchyRayHit hit;hit.segment=edge.segment;output.push_back(hit);
        }
    }return output;
}
std::vector<EscherPeriodInterval> escherNfpPeriodIntervals(std::span<const ConvolutionHierarchyRayHit> hits) {
    std::vector<EscherPeriodInterval> output;int32_t winding=0;bool outside=true;double upper=0;
    for(const auto &hit:hits) {
        winding=std::bit_cast<int32_t>(uint32_t(winding)+uint32_t(hit.crossing));
        if(outside) {if(winding>1){outside=false;upper=std::sqrt(hit.distanceSquared);}}
        else if(winding<2){outside=true;output.push_back({std::sqrt(hit.distanceSquared),upper});}
    }
    if(!outside)output.push_back({0,upper});std::reverse(output.begin(),output.end());return output;
}
namespace {
struct PeriodEntry {int32_t index=0;double multiplier=0;};
class PeriodHeap {
    std::span<const EscherPeriodInterval> intervals;
    std::vector<PeriodEntry> entries;
    bool before(PeriodEntry a,PeriodEntry b)const {
        const double al=intervals[a.index][0]/a.multiplier,bl=intervals[b.index][0]/b.multiplier;
        if(al<bl)return true;if(al!=bl)return false;
        return intervals[a.index][1]/a.multiplier<intervals[b.index][1]/b.multiplier;
    }
    void up(size_t hole,PeriodEntry entry) {
        while(hole>0){const auto parent=(hole-1)/2;if(!before(entry,entries[parent]))break;entries[hole]=entries[parent];hole=parent;}
        entries[hole]=entry;
    }
public:
    explicit PeriodHeap(std::span<const EscherPeriodInterval> values):intervals(values){}
    bool empty()const{return entries.empty();}
    void push(PeriodEntry entry){entries.push_back(entry);up(entries.size()-1,entry);}
    PeriodEntry pop() {
        const auto result=entries.front(),replacement=entries.back();entries.pop_back();const auto count=entries.size();
        if(count){size_t hole=0,limit=(count-1)/2;
            while(hole<limit){size_t child=2*hole+1;if(!before(entries[child],entries[child+1]))++child;entries[hole]=entries[child];hole=child;}
            if(hole==limit&&count%2==0){entries[hole]=entries[count-1];hole=count-1;}up(hole,replacement);
        }return result;
    }
};
int64_t roundInteger(double value){value=value<0?value-.5:value+.5;return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
}
double escherNfpPeriodMinimum(std::span<const EscherPeriodInterval> intervals) {
    if(intervals.empty()||!(intervals.front()[0]<=0))throw std::invalid_argument("Escher period intervals require an interval containing zero");
    double result=0;for(const auto &interval:intervals){const double length=interval[1]-interval[0];if(result<=length)result=length;}
    PeriodHeap heap(intervals);for(size_t index=1;index<intervals.size();++index)heap.push({int32_t(index),std::numeric_limits<double>::infinity()});
    while(!heap.empty()) {
        auto entry=heap.pop();const auto &interval=intervals[entry.index];
        const double high=interval[1]/entry.multiplier;
        if(result<interval[0]/entry.multiplier)break;
        if(result<interval[1]) {
            if(result<=high)result=high;
            double next=std::ceil(interval[0]/result);if(interval[1]/result<=next)next-=1;
            const double limit=entry.multiplier-1;if(limit<=next)next=limit;
            heap.push({entry.index,next});
        }
    }return result;
}
ConvolutionIntegerPoint escherNfpPeriodEndpoint(std::vector<ConvolutionHierarchyRayHit> &hits) {
    if(hits.empty())return {0,0};convolutionSortHierarchyRayHits(hits);
    const double period=escherNfpPeriodMinimum(escherNfpPeriodIntervals(hits));
    const double scale=period/std::sqrt(hits.front().distanceSquared);const auto point=hits.front().point;
    return {roundInteger(double(point[0])*scale),roundInteger(double(point[1])*scale)};
}
namespace {
bool badWinding(std::span<const ConvolutionHierarchyRayHit> hits){int32_t winding=0;for(const auto &hit:hits){winding=std::bit_cast<int32_t>(uint32_t(winding)+uint32_t(hit.crossing));if(winding< -1)return true;}return false;}
double distanceSquared(ConvolutionIntegerPoint point){const double x=double(point[0]),y=double(point[1]);return y*y+x*x;}
}
void escherPrepareNfpRayHits(ConvolutionIntegerPoint direction,ConvolutionIntegerPoint translation,size_t first,int32_t tolerance,
    std::vector<ConvolutionHierarchyRayHit> &hits,bool restore) {
    const ConvolutionIntegerPoint reference{sub(0,direction[0]),sub(0,direction[1])};
    for(size_t index=first;index<hits.size();++index)convolutionClassifyHierarchyRay(direction,reference,translation,hits[index],restore);
    const double threshold=double(std::bit_cast<int32_t>(uint32_t(tolerance)*uint32_t(tolerance)));
    auto end=std::remove_if(hits.begin()+first,hits.end(),[&](const auto &hit){return hit.distanceSquared<threshold;});hits.erase(end,hits.end());
    std::vector<ConvolutionHierarchyRayHit> suffix(hits.begin()+first,hits.end());convolutionSortHierarchyRayHits(suffix);std::copy(suffix.begin(),suffix.end(),hits.begin()+first);
}
bool escherSearchNfpPeriod(const EscherNfpBoundary &boundary,const ConvolutionEdgeIndex &index,ConvolutionIntegerPoint &period,
    ConvolutionIntegerPoint direction,std::vector<ConvolutionHierarchyRayHit> &hits,int32_t minimumTrials) {
    hits=escherNfpRayIntersections(boundary,direction);if(hits.empty())return false;
    escherPrepareNfpRayHits(direction,{0,0},0,10,hits,false);if(badWinding(hits)||hits.empty())return false;
    for(size_t side=0;side<2;++side) {
        if(side)for(auto &coordinate:period)coordinate=sub(0,coordinate);
        ConvolutionIntegerPoint shift{0,0},endpoint=direction;int32_t trial=0;
        for(;trial<10;++trial) {
            for(size_t axis=0;axis<2;++axis){shift[axis]=add(shift[axis],period[axis]);endpoint[axis]=add(endpoint[axis],period[axis]);}
            if(distanceSquared(shift)>0x1p63||distanceSquared(endpoint)>0x1p63)return false;
            const auto first=hits.size();for(const auto &segment:index.query({shift,endpoint})){ConvolutionHierarchyRayHit hit;hit.segment=segment;hits.push_back(hit);}
            if(first==hits.size()){if(minimumTrials<=trial)break;}
            else {escherPrepareNfpRayHits(direction,{sub(0,shift[0]),sub(0,shift[1])},first,10,hits,false);if(badWinding({hits.data()+first,hits.size()-first}))return false;}
        }
        if(trial==10)return false;
    }return true;
}
}
