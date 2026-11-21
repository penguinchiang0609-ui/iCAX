#include "recovered_fuzzy_polygon_reconstitutor.hpp"
#include <limits>
#include <numeric>
#include <bit>
#include <list>
namespace study::recovered {
namespace {
int32_t weightTrunc(double value){return value>=double(INT32_MIN)&&value<double(INT32_MAX)+1.?int32_t(value):INT32_MIN;}
bool weightBefore(FuzzyWeightMinimum a,FuzzyWeightMinimum b){return a.value<b.value||(a.value==b.value&&a.position<b.position);}
}
FuzzyWeightRange::FuzzyWeightRange(std::span<const double> values){
    if(values.empty())return;minima_.push_back(std::vector<double>(values.size(),0.));lazy_.push_back({values.begin(),values.end()});positions_.push_back(std::vector<int32_t>(values.size()));std::iota(positions_[0].begin(),positions_[0].end(),0);
    for(size_t level=1,step=2;step<=values.size();++level,step*=2){const auto count=(values.size()+step-1)/step;minima_.push_back(std::vector<double>(count));lazy_.push_back(std::vector<double>(count,0.));positions_.push_back(std::vector<int32_t>(count));for(size_t n=0;n<count;++n)refresh(int32_t(level),int32_t(n));}
    for(size_t n=0;n<values.size();++n)if(values[n]==0.)zeros_.insert(int32_t(n));
}
void FuzzyWeightRange::refresh(int32_t level,int32_t index){if(level<=0)return;const auto child=size_t(index)*2,below=size_t(level-1);auto &value=minima_[size_t(level)][size_t(index)];auto &position=positions_[size_t(level)][size_t(index)];value=minima_[below][child]+lazy_[below][child];position=positions_[below][child];if(child+1<minima_[below].size()){const double right=minima_[below][child+1]+lazy_[below][child+1];if(right<value){value=right;position=positions_[below][child+1];}}}
FuzzyWeightMinimum FuzzyWeightRange::minimum(int32_t begin,int32_t end,int32_t level)const{
    const auto left=(begin-1)>>level,mid=(left+1)<<level,right=end>>level,boundary=right<<level;
    if(end<mid){auto result=minimum(begin,end,level-1);result.value=lazy_[size_t(level)][size_t(left)]+result.value;return result;}
    FuzzyWeightMinimum result{std::numeric_limits<double>::max(),-1};
    const auto consider=[&](FuzzyWeightMinimum next){if(weightBefore(next,result))result=next;};
    if(begin<mid){auto next=minimum(begin,mid,level-1);next.value=next.value+lazy_[size_t(level)][size_t(left)];consider(next);}
    for(int32_t n=left+1;n<right;++n)consider({lazy_[size_t(level)][size_t(n)]+minima_[size_t(level)][size_t(n)],positions_[size_t(level)][size_t(n)]});
    if(boundary<end){auto next=minimum(boundary,end,level-1);next.value=next.value+lazy_[size_t(level)][size_t(right)];consider(next);}return result;
}
FuzzyWeightMinimum FuzzyWeightRange::minimum(int32_t begin,int32_t end)const{require(begin>=0&&end>begin&&size_t(end)<=size(),PN2S_INVALID,"Fuzzy weight minimum range is invalid");return minimum(begin,end,int32_t(minima_.size())-1);}
void FuzzyWeightRange::zeroScan(int32_t level,int32_t index,int32_t above){const auto n=size_t(index),l=size_t(level);const double value=(minima_[l][n]+lazy_[l][n])+double(above);if(value<=0.){if(level==0)zeros_.insert(index);else {const int32_t next=weightTrunc(double(above)+lazy_[l][n]);zeroScan(level-1,index*2,next);if(size_t(index*2+1)<minima_[l-1].size())zeroScan(level-1,index*2+1,next);}}}
void FuzzyWeightRange::add(int32_t begin,int32_t end,double delta,int32_t level,double above){
    const auto left=(begin-1)>>level,mid=(left+1)<<level,right=end>>level,boundary=right<<level;
    if(end<mid){add(begin,end,delta,level-1,above+lazy_[size_t(level)][size_t(left)]);refresh(level,left);return;}
    if(begin<mid){add(begin,mid,delta,level-1,above+lazy_[size_t(level)][size_t(left)]);refresh(level,left);}
    for(int32_t n=left+1;n<right;++n){auto &value=lazy_[size_t(level)][size_t(n)];value=delta+value;zeroScan(level,n,weightTrunc(above));}
    if(boundary<end){add(boundary,end,delta,level-1,above+lazy_[size_t(level)][size_t(right)]);refresh(level,right);}
}
void FuzzyWeightRange::add(int32_t begin,int32_t end,double delta){require(begin>=0&&end>begin&&size_t(end)<=size(),PN2S_INVALID,"Fuzzy weight update range is invalid");add(begin,end,delta,int32_t(minima_.size())-1,0.);}
int32_t FuzzyWeightRange::rangeEnd(int32_t begin)const{const auto at=zeros_.lower_bound(begin);return at==zeros_.end()?int32_t(size()):*at;}
size_t FuzzyWeightRange::size()const{return minima_.empty()?0:minima_[0].size();}
bool FuzzyWeightRange::empty()const{return zeros_.size()>=size();}
namespace {
int64_t fuzzyNeg(int64_t value){return std::bit_cast<int64_t>(uint64_t(0)-uint64_t(value));}
int64_t fuzzyAdd(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t fuzzySub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
EscherIntegerPoint fuzzyDelta(EscherIntegerPoint a,EscherIntegerPoint b){return {fuzzySub(a[0],b[0]),fuzzySub(a[1],b[1])};}
int64_t fuzzyCross(EscherIntegerPoint a,EscherIntegerPoint b){return std::bit_cast<int64_t>(uint64_t(a[0])*uint64_t(b[1])-uint64_t(a[1])*uint64_t(b[0]));}
bool fuzzyHalf(EscherIntegerPoint p){return p[1]>0||(p[1]==0&&p[0]>0);}
struct FuzzyHotLess {bool operator()(EscherIntegerPoint a,EscherIntegerPoint b)const{const bool first=fuzzyHalf(a),second=fuzzyHalf(b);return first!=second?first>second:fuzzyCross(a,b)<0;}};
auto fuzzySegmentKey(const ConvolutionIntegerSegment &edge){return std::array<int64_t,8>{std::min(fuzzyNeg(edge[0][1]),fuzzyNeg(edge[1][1])),std::min(edge[0][0],edge[1][0]),std::max(fuzzyNeg(edge[0][1]),fuzzyNeg(edge[1][1])),std::max(edge[0][0],edge[1][0]),edge[0][0],edge[0][1],edge[1][0],edge[1][1]};}
struct FuzzySegmentLess {bool operator()(const ConvolutionIntegerSegment &a,const ConvolutionIntegerSegment &b)const{return fuzzySegmentKey(a)<fuzzySegmentKey(b);}};
ConvolutionIntegerSegment fuzzyCanonicalSegment(ConvolutionIntegerSegment edge){if(edge[0]<edge[1])std::swap(edge[0],edge[1]);return edge;}
std::vector<ConvolutionIntegerSegment> fuzzyCopiedEdgeOrder(std::span<const ConvolutionIntegerSegment> edges){const auto source=convolutionIntegerEdgeOrder(edges);size_t capacity=8;while(capacity<source.size())capacity=capacity<512?capacity*8:std::bit_ceil(source.size());std::list<ConvolutionIntegerSegment> copy;using Iterator=std::list<ConvolutionIntegerSegment>::iterator;std::vector<Iterator> buckets(capacity,copy.end());for(const auto edge:source){auto &first=buckets[convolutionIntegerEdgeHash(edge)&(capacity-1)];first=copy.insert(first,edge);}return {copy.begin(),copy.end()};}
void fuzzySortNeighborDirections(std::vector<int32_t> &indices,const std::vector<EscherIntegerPoint> &vectors){
    const auto before=[&](int32_t a,int32_t b){return fuzzyCross(vectors[size_t(a)],vectors[size_t(b)])>0;};
    const auto median=[&](size_t a,size_t b,size_t c){if(before(indices[b],indices[a]))std::swap(indices[a],indices[b]);if(before(indices[c],indices[b])){std::swap(indices[b],indices[c]);if(before(indices[b],indices[a]))std::swap(indices[a],indices[b]);}};
    const auto partition=[&](size_t first,size_t last){const auto middle=first+(last-first)/2;if(last-first>41){const auto step=(last-first)/8;median(first,first+step,first+2*step);median(middle-step,middle,middle+step);median(last-1-2*step,last-1-step,last-1);median(first+step,middle,last-1-step);}else median(first,middle,last-1);size_t low=middle,high=middle+1;while(first<low&&!before(indices[low-1],indices[low])&&!before(indices[low],indices[low-1]))--low;while(high<last&&!before(indices[high],indices[low])&&!before(indices[low],indices[high]))++high;size_t right=high,left=low;
        for(;;){for(;right<last;++right){if(before(indices[low],indices[right]))continue;if(before(indices[right],indices[low]))break;if(high!=right)std::swap(indices[high],indices[right]);++high;}for(;first<left;--left){if(before(indices[left-1],indices[low]))continue;if(before(indices[low],indices[left-1]))break;--low;if(low!=left-1)std::swap(indices[low],indices[left-1]);}if(left==first&&right==last)return std::pair{low,high};if(left==first){if(high!=right)std::swap(indices[low],indices[high]);++high;std::swap(indices[low++],indices[right++]);}else if(right==last){--left;--low;if(left!=low)std::swap(indices[left],indices[low]);std::swap(indices[low],indices[--high]);}else std::swap(indices[right++],indices[--left]);}};
    const auto down=[&](size_t base,size_t hole,size_t count,int32_t value){const auto top=hole,limit=(count-1)/2;while(hole<limit){size_t child=hole*2+1;if(!before(indices[base+child+1],indices[base+child]))++child;indices[base+hole]=indices[base+child];hole=child;}if(hole==limit&&count%2==0){indices[base+hole]=indices[base+count-1];hole=count-1;}while(top<hole){const auto parent=(hole-1)/2;if(!before(indices[base+parent],value))break;indices[base+hole]=indices[base+parent];hole=parent;}indices[base+hole]=value;};
    const auto sort=[&](auto &&self,size_t first,size_t last,size_t depth)->void{while(last-first>32){if(!depth){size_t count=last-first;for(size_t n=count/2;n>0;--n)down(first,n-1,count,indices[first+n-1]);while(count>1){const auto value=indices[first+count-1];indices[first+count-1]=indices[first];down(first,0,--count,value);}return;}const auto [a,b]=partition(first,last);depth=(depth>>1)+(depth>>2);if(a-first<last-b){self(self,first,a,depth);first=b;}else {self(self,b,last,depth);last=a;}}if(first<last)for(size_t n=first+1;n<last;++n){const auto value=indices[n];size_t at=n;if(before(value,indices[first])){std::move_backward(indices.begin()+first,indices.begin()+n,indices.begin()+n+1);at=first;}else while(at>first&&before(value,indices[at-1])){indices[at]=indices[at-1];--at;}indices[at]=value;}};sort(sort,0,indices.size(),indices.size());
}
}
struct FuzzyReconstitutor::State {
    std::set<ConvolutionIntegerSegment,FuzzySegmentLess> pending;
    std::multiset<ConvolutionIntegerSegment,FuzzySegmentLess> alive;
    std::map<ConvolutionIntegerSegment,double> graphWeights;
    std::map<EscherIntegerPoint,std::vector<EscherIntegerPoint>> neighbors;
    std::map<EscherIntegerPoint,std::set<EscherIntegerPoint,FuzzyHotLess>> hot;
    std::set<EscherIntegerPoint> sorted;
    std::vector<std::shared_ptr<const std::vector<ConvolutionIntegerSegment>>> paths;
    std::vector<FuzzyWeightRange> weights;
    std::map<ConvolutionIntegerSegment,FuzzyTopologySegmentIndex> index;
};
FuzzyReconstitutor::FuzzyReconstitutor(std::span<const FuzzyWeightedSegment> source):state_(std::make_unique<State>()){
    std::vector<ConvolutionIntegerSegment> edges;std::vector<double> weights;for(const auto &part:source){edges.push_back(part.segment);weights.push_back(part.weight);}fuzzyNormalizeSegmentWeights(edges,weights);
    for(size_t n=0;n<edges.size();++n){auto &value=state_->graphWeights[edges[n]];value=weights[n]+value;}
    std::vector<ConvolutionIntegerSegment> order;for(const auto edge:fuzzyCopiedEdgeOrder(edges))if(state_->graphWeights.at(edge)!=0.)order.push_back(edge);
    for(const auto edge:order){state_->pending.insert(edge);state_->neighbors[edge[0]].push_back(edge[1]);}
    for(const auto edge:order){state_->neighbors[edge[1]].push_back(edge[0]);state_->graphWeights[{edge[1],edge[0]}]=-state_->graphWeights.at(edge);}
}
FuzzyReconstitutor::~FuzzyReconstitutor()=default;
FuzzyReconstitutor::FuzzyReconstitutor(FuzzyReconstitutor &&)noexcept=default;
FuzzyReconstitutor &FuzzyReconstitutor::operator=(FuzzyReconstitutor &&)noexcept=default;
const std::vector<std::shared_ptr<const std::vector<ConvolutionIntegerSegment>>> &FuzzyReconstitutor::paths()const{return state_->paths;}
size_t FuzzyReconstitutor::pendingCount()const{return state_->pending.size();}
const std::map<EscherIntegerPoint,std::vector<EscherIntegerPoint>> &FuzzyReconstitutor::adjacency()const{return state_->neighbors;}
std::optional<FuzzyTopologySegmentIndex> FuzzyReconstitutor::lookup(const ConvolutionIntegerSegment &edge)const{if(const auto at=state_->index.find(edge);at!=state_->index.end()){auto result=at->second;result.forward=true;return result;}if(const auto at=state_->index.find({edge[1],edge[0]});at!=state_->index.end()){auto result=at->second;result.forward=false;return result;}return std::nullopt;}
int32_t FuzzyReconstitutor::rangeEnd(int32_t path,int32_t begin)const{return state_->weights.at(size_t(path)).rangeEnd(begin);}
double FuzzyReconstitutor::weight(int32_t path,int32_t begin,int32_t end)const{return state_->weights.at(size_t(path)).minimum(begin,end).value;}
FuzzyGeometryRange FuzzyReconstitutor::registerPath(std::span<const ConvolutionIntegerSegment> source){
    require(!source.empty(),PN2S_INVALID,"Fuzzy reconstitution cannot register an empty path");std::vector<double> values;values.reserve(source.size());
    for(const auto edge:source){state_->hot[edge[0]].insert(fuzzyDelta(edge[1],edge[0]));const auto canonical=fuzzyCanonicalSegment(edge);require(state_->pending.erase(canonical)!=0,PN2S_INVALID,"Fuzzy reconstitution path visits an unavailable segment");values.push_back(state_->graphWeights[edge]);}
    const auto path=int32_t(state_->paths.size());auto owner=std::make_shared<const std::vector<ConvolutionIntegerSegment>>(source.begin(),source.end());for(size_t n=0;n<source.size();++n){state_->index[source[n]]={path,int32_t(n),true};state_->alive.insert(source[n]);}state_->paths.push_back(owner);state_->weights.emplace_back(values);return {std::move(owner),0,int32_t(source.size())};
}
const std::vector<EscherIntegerPoint> &FuzzyReconstitutor::neighbors(EscherIntegerPoint point){auto &values=state_->neighbors[point];std::erase_if(values,[&](EscherIntegerPoint other){const ConvolutionIntegerSegment edge{point,other};if(state_->pending.contains(fuzzyCanonicalSegment(edge)))return false;const auto found=lookup(edge);return !found||weight(found->path,found->position,found->position+1)==0.;});return values;}
bool FuzzyReconstitutor::hasHot(EscherIntegerPoint point){const auto found=state_->hot.find(point);if(found==state_->hot.end())return false;auto &directions=found->second;while(!directions.empty()){const auto direction=*directions.begin();const auto index=lookup({point,{fuzzyAdd(point[0],direction[0]),fuzzyAdd(point[1],direction[1])}});if(index&&weight(index->path,index->position,index->position+1)!=0.)return true;directions.erase(directions.begin());}state_->hot.erase(found);return false;}
ConvolutionIntegerSegment FuzzyReconstitutor::clockwiseHot(const ConvolutionIntegerSegment &incoming){auto at=state_->hot.find(incoming[1]);require(at!=state_->hot.end()&&!at->second.empty(),PN2S_INVALID,"Fuzzy clockwise query has no hot endpoint");auto &directions=at->second;auto next=directions.lower_bound(fuzzyDelta(incoming[0],incoming[1]));for(size_t turns=0;turns<5;++turns){if(next==directions.end())next=directions.begin();while(next!=directions.end()){const auto d=*next;ConvolutionIntegerSegment edge{incoming[1],{fuzzyAdd(incoming[1][0],d[0]),fuzzyAdd(incoming[1][1],d[1])}};const auto index=lookup(edge);if(index&&weight(index->path,index->position,index->position+1)!=0.)return edge;next=directions.erase(next);}if(directions.empty())break;}throw std::invalid_argument("Fuzzy clockwise query and hot endpoint disagree");}
ConvolutionIntegerSegment FuzzyReconstitutor::nextSeed(){std::vector<ConvolutionIntegerSegment> candidates;if(!state_->pending.empty())candidates.push_back(*state_->pending.begin());if(!empty()){while(!state_->alive.empty()){const auto edge=*state_->alive.begin();const auto index=lookup(edge);require(index.has_value(),PN2S_INVALID,"Fuzzy sorted live segment is unindexed");if(weight(index->path,index->position,index->position+1)!=0.){candidates.push_back(edge);break;}state_->alive.erase(state_->alive.begin());}}
    require(!candidates.empty(),PN2S_INVALID,"Fuzzy reconstitution has no live seed");const auto key=[](ConvolutionIntegerSegment edge){for(auto &point:edge)point={fuzzyNeg(point[1]),point[0]};return std::array<int64_t,8>{std::min(edge[0][0],edge[1][0]),std::min(edge[0][1],edge[1][1]),std::max(edge[0][0],edge[1][0]),std::max(edge[0][1],edge[1][1]),edge[0][0],edge[0][1],edge[1][0],edge[1][1]};};auto edge=candidates[0];for(const auto candidate:candidates)if(key(candidate)<key(edge))edge=candidate;
    auto point=edge[0];if(EscherIntegerPoint{fuzzyNeg(edge[1][1]),edge[1][0]}<EscherIntegerPoint{fuzzyNeg(point[1]),point[0]})point=edge[1];
    if(!state_->sorted.contains(point)){const auto &destinations=neighbors(point);std::vector<EscherIntegerPoint> vectors;std::vector<int32_t> positive,negative;for(size_t n=0;n<destinations.size();++n){auto d=fuzzyDelta(destinations[n],point);d={fuzzyNeg(d[0]),fuzzyNeg(d[1])};vectors.push_back(d);(fuzzyHalf(d)?positive:negative).push_back(int32_t(n));}fuzzySortNeighborDirections(positive,vectors);fuzzySortNeighborDirections(negative,vectors);auto &out=state_->neighbors[point];const auto original=out;out.clear();for(const auto n:positive)out.push_back(original[size_t(n)]);for(const auto n:negative)out.push_back(original[size_t(n)]);state_->sorted.insert(point);}
    const auto &destinations=neighbors(point);require(!destinations.empty(),PN2S_INVALID,"Fuzzy seed has no destination");return {point,destinations[0]};
}
double FuzzyReconstitutor::withdraw(std::span<const FuzzyGeometryRange> ranges){struct Update{int32_t path,begin,end;};std::vector<Update> updates;double minimum=std::numeric_limits<double>::max();for(const auto &range:ranges){require(range.segments&&range.begin>=0&&range.end>range.begin&&size_t(range.end)<=range.segments->size(),PN2S_INVALID,"Fuzzy withdrawal geometry range is invalid");const auto found=lookup((*range.segments)[size_t(range.begin)]);require(found.has_value(),PN2S_INVALID,"Fuzzy withdrawal path is unregistered");const auto shift=found->position-range.begin;require(shift==0,PN2S_INVALID,"Fuzzy withdrawal is unexpectedly partial");const auto value=weight(found->path,range.begin+shift,range.end+shift);if(!(minimum<=value))minimum=value;updates.push_back({found->path,range.begin+shift,range.end+shift});}require(minimum!=std::numeric_limits<double>::max(),PN2S_INVALID,"Fuzzy withdrawal has no finite path weight");for(const auto update:updates)state_->weights[size_t(update.path)].add(update.begin,update.end,-minimum);return minimum;}
bool FuzzyReconstitutor::empty()const{return std::all_of(state_->weights.begin(),state_->weights.end(),[](const auto &range){return range.empty();});}
}
