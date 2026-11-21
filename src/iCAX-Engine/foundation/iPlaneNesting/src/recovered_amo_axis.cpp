#include "recovered_amo_axis.hpp"
namespace study::recovered {
namespace {
template<class T,class Before> class AmoAxisSort {
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
    AmoAxisSort(std::vector<T> &values,Before predicate):items(values),before(predicate){}
    T select(size_t first,size_t last){const size_t target=first+(last-first)/2;while(last-first>32){const auto [a,b]=partition(first,last);if(target<b){if(a<=target)return items[target];last=a;}else first=b;}if(first<last)insertion(first,last);return items[target];}
};

Point transformed(Point point,const std::array<double,7> &m){return {point.y*(m[1]*m[6])+point.x*(m[0]*m[6])+m[4],point.y*(m[3]*m[6])+point.x*(m[2]*m[6])+m[5]};}
size_t split(std::vector<AmoAxisSegment> &segments,size_t first,size_t last,const AmoAxisSegment &pivot,int endpoint,bool strict){const double bound=endpoint?pivot.to.x:pivot.from.x;const auto accepted=[&](const AmoAxisSegment &value){const double x=endpoint?value.to.x:value.from.x;return strict?x<bound:x<=bound;};while(first!=last){if(accepted(segments[first]))++first;else{do{--last;if(first==last)return first;}while(!accepted(segments[last]));std::swap(segments[first++],segments[last]);}}return first;}
void visit(const std::shared_ptr<AmoAxisNode> &node,AmoAxisQueryState &state){if(!node||state.coordinate<node->minimum||node->maximum<state.coordinate)return;const auto &segment=node->segment;const double minimum=std::min(segment.from.x,segment.to.x)-segment.tolerance,maximum=std::max(segment.from.x,segment.to.x)+segment.tolerance;if(!node->deleted&&minimum<=state.coordinate&&state.coordinate<maximum)amoAxisConsiderSegment(state,segment);visit(node->left,state);visit(node->right,state);}
}
void amoAxisConsiderSegment(AmoAxisQueryState &state,const AmoAxisSegment &segment){
    const double dx=segment.to.x-segment.from.x,dy=segment.to.y-segment.from.y;if(!(dx<0.))return;const double minimum=std::min(segment.from.x,segment.to.x),maximum=std::max(segment.from.x,segment.to.x),epsilon=segment.tolerance/10.;const double x=state.coordinate;if(!(minimum-epsilon<=x&&x<=maximum+epsilon))return;
    const double clamped=std::clamp(x,minimum,maximum);const double interpolated=((clamped-segment.from.x)/dx)*dy+segment.from.y;
    const double lower=std::min(segment.from.y,segment.to.y),upper=std::max(segment.from.y,segment.to.y);const double y=std::clamp(interpolated,lower,upper);if(!state.unrestricted&&!(y<=state.reference))return;
    if(epsilon<std::fabs(x-minimum)&&(state.firstEmpty||state.first<y)){state.firstEmpty=false;state.first=y;}
    if(epsilon<std::fabs(x-maximum)&&(state.secondEmpty||state.second<y)){state.secondEmpty=false;state.second=y;}
}
std::shared_ptr<AmoAxisNode> amoAxisBuildTree(std::vector<AmoAxisSegment> &segments,size_t first,size_t last,int32_t endpoint){
    if(first==last)return {};require(first<last&&last<=segments.size(),PN2S_INVALID,"Amo axis tree range is invalid");const auto before=[&](const auto &a,const auto &b){return endpoint?a.to.x<b.to.x:a.from.x<b.from.x;};const auto pivot=AmoAxisSort(segments,before).select(first,last);const auto separated=split(segments,first,last,pivot,endpoint,true);split(segments,separated,last,pivot,endpoint,false);const size_t middle=first+(last-first)/2;auto left=amoAxisBuildTree(segments,first,middle,1-endpoint),right=amoAxisBuildTree(segments,middle+1,last,1-endpoint);auto node=std::make_shared<AmoAxisNode>();node->segment=segments[middle];node->endpoint=endpoint;node->left=left;node->right=right;node->minimum=std::min(node->segment.from.x,node->segment.to.x)-node->segment.tolerance;node->maximum=std::max(node->segment.from.x,node->segment.to.x)+node->segment.tolerance;node->height=1+std::max(left?left->height:0,right?right->height:0);node->live=1+(left?left->live:0)+(right?right->live:0);node->count=1+(left?left->count:0)+(right?right->count:0);for(const auto &child:{left,right})if(child){node->minimum=std::min(node->minimum,child->minimum);node->maximum=std::max(node->maximum,child->maximum);}return node;
}
AmoTilingAxisIndex amoBuildAxisIndex(const AmoTilingContours &contours,Point direction){
    AmoTilingAxisIndex index;double norm=std::sqrt((-direction.y)*(-direction.y)+direction.x*direction.x);double x=1.,y=0.;if(norm!=0.){x=-direction.y/norm;y=direction.x/norm;}y=-y;const double secondNorm=std::sqrt(x*x+y*y);index.direction=secondNorm!=0.?Point{x/secondNorm,y/secondNorm}:Point{1.,0.};x=index.direction.x;y=index.direction.y;const double matrixNorm=std::sqrt((-y)*(-y)+x*x);index.forward={x,matrixNorm!=0.?(-y)/matrixNorm:1.,y,matrixNorm!=0.?x/matrixNorm:0.,0.,0.,1.};const auto &f=index.forward;const double determinant=f[0]*f[3]-f[1]*f[2];index.inverse={f[3]/determinant,-f[1]/determinant,-f[2]/determinant,f[0]/determinant,0.,0.,1./f[6]};index.inverse[4]=(-f[5])*index.inverse[1]+(-f[4])*index.inverse[0];index.inverse[5]=index.inverse[3]*(-f[5])+(-f[4])*index.inverse[2];
    std::vector<AmoAxisSegment> segments;for(const auto &ring:contours){require(!ring.empty(),PN2S_INVALID,"Amo axis index requires nonempty source rings");auto previous=transformed(ring.back(),index.forward);for(auto point:ring){point=transformed(point,index.forward);if(point.x-previous.x<0.)segments.push_back({previous,point,index.tolerance});previous=point;}}
    index.root=amoAxisBuildTree(segments,0,segments.size());return index;
}
std::optional<Point> amoQueryAxisIndex(const AmoTilingAxisIndex &index,Point point){point=transformed(point,index.forward);AmoAxisQueryState state;state.coordinate=point.x;visit(index.root,state);if(state.firstEmpty||state.secondEmpty)return {};const double selected=state.second<state.first?state.second:state.first;return transformed({point.x,selected},index.inverse);}
double amoTilingMapAxisY(AmoTilingContourMap &map,double x){if(!map.cachedIndices[0])map.cachedIndices[0]=std::make_shared<AmoTilingAxisIndex>(amoBuildAxisIndex(map.rotatedDetailed,{-0.,-1.}));const auto hit=amoQueryAxisIndex(*std::static_pointer_cast<AmoTilingAxisIndex>(map.cachedIndices[0]),{x,0.});return hit?hit->y:-std::numeric_limits<double>::max();}
double amoTilingMapAxisX(AmoTilingContourMap &map,double y){if(!map.cachedIndices[1])map.cachedIndices[1]=std::make_shared<AmoTilingAxisIndex>(amoBuildAxisIndex(map.rotatedDetailed,{-1.,-0.}));const auto hit=amoQueryAxisIndex(*std::static_pointer_cast<AmoTilingAxisIndex>(map.cachedIndices[1]),{0.,y});return hit?hit->x:-std::numeric_limits<double>::max();}
}
