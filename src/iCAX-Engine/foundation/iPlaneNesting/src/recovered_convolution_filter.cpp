#include "recovered_convolution_filter.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int64_t magnitude(int64_t a){return a<0?sub(0,a):a;}
int sign(int64_t a){return a>0?1:a<0?-1:0;}
ConvolutionIntegerBox box(const ConvolutionIntegerSegment &s){return {std::min(s[0][0],s[1][0]),std::min(s[0][1],s[1][1]),std::max(s[0][0],s[1][0]),std::max(s[0][1],s[1][1])};}
void expand(ConvolutionIntegerBox &a,const ConvolutionIntegerBox &b){for(size_t k=0;k<2;++k){a[k]=std::min(a[k],b[k]);a[k+2]=std::max(a[k+2],b[k+2]);}}
struct BulkEntry {ConvolutionIntegerPoint center;ConvolutionIntegerSegment edge;};
class BulkBuilder {
    std::vector<BulkEntry> entries;
    void median(size_t a,size_t b,size_t c,size_t axis){const auto less=[&](size_t i,size_t j){return entries[i].center[axis]<entries[j].center[axis];};if(less(b,a))std::swap(entries[b],entries[a]);if(less(c,b)){std::swap(entries[c],entries[b]);if(less(b,a))std::swap(entries[b],entries[a]);}}
    std::pair<size_t,size_t> partition(size_t first,size_t last,size_t axis){
        const auto less=[&](size_t a,size_t b){return entries[a].center[axis]<entries[b].center[axis];};const auto mid=first+(last-first)/2;
        if(last-first>41){const auto step=(last-first)/8;median(first,first+step,first+2*step,axis);median(mid-step,mid,mid+step,axis);median(last-1-2*step,last-1-step,last-1,axis);median(first+step,mid,last-1-step,axis);}else median(first,mid,last-1,axis);
        size_t pfirst=mid,plast=mid+1;while(first<pfirst&&!less(pfirst-1,pfirst)&&!less(pfirst,pfirst-1))--pfirst;while(plast<last&&!less(plast,pfirst)&&!less(pfirst,plast))++plast;
        size_t right=plast,left=pfirst;
        for(;;){for(;right<last;++right){if(less(pfirst,right))continue;if(less(right,pfirst))break;if(plast!=right)std::swap(entries[plast],entries[right]);++plast;}
            for(;first<left;--left){if(less(left-1,pfirst))continue;if(less(pfirst,left-1))break;--pfirst;if(pfirst!=left-1)std::swap(entries[pfirst],entries[left-1]);}
            if(left==first&&right==last)return {pfirst,plast};
            if(left==first){if(plast!=right)std::swap(entries[pfirst],entries[plast]);++plast;std::swap(entries[pfirst++],entries[right++]);}
            else if(right==last){--left;--pfirst;if(left!=pfirst)std::swap(entries[left],entries[pfirst]);std::swap(entries[pfirst],entries[--plast]);}
            else std::swap(entries[right++],entries[--left]);}
    }
    void nth(size_t first,size_t middle,size_t last,size_t axis){if(middle==last)return;while(last-first>32){const auto [a,b]=partition(first,last,axis);if(middle<b){if(a<=middle)return;last=a;}else first=b;}
        for(size_t n=first+1;n<last;++n){auto item=entries[n];size_t j=n;while(j>first&&item.center[axis]<entries[j-1].center[axis]){entries[j]=entries[j-1];--j;}entries[j]=item;}}
    std::pair<std::unique_ptr<ConvolutionIndexNode>,ConvolutionIntegerBox> node(size_t first,size_t last,ConvolutionIntegerBox bounds,size_t capacity,size_t minimum){
        auto result=std::make_unique<ConvolutionIndexNode>();ConvolutionIntegerBox actual{};
        if(capacity<2){result->leaf=true;for(size_t n=first;n<last;++n){const auto b=box(entries[n].edge);if(n==first)actual=b;else expand(actual,b);result->entries.push_back({b,entries[n].edge,0});}}
        else {result->leaf=false;const auto append=[&](auto &&self,size_t begin,size_t end,ConvolutionIntegerBox region)->void {
            const auto count=end-begin;if(count<=capacity){auto [child,b]=node(begin,end,region,capacity/16,minimum/16);if(result->entries.empty())actual=b;else expand(actual,b);result->entries.push_back({b,{},0});result->children.push_back(std::move(child));return;}
            size_t groups=count/capacity,split=(groups/2)*capacity;
            if(count%capacity){if(count%capacity<minimum){const auto remaining=count-minimum;groups=remaining/capacity;split=remaining%capacity;if(split){if(groups)split=((groups+2)/2)*capacity;}else split=((groups+1)/2)*capacity;}else split=((groups+1)/2)*capacity;}
            const size_t axis=sub(region[2],region[0])<sub(region[3],region[1]);nth(begin,begin+split,end,axis);const auto cut=add(region[axis],sub(region[axis+2],region[axis])/2);auto left=region,right=region;left[axis+2]=cut;right[axis]=cut;self(self,begin,begin+split,left);self(self,begin+split,end,right);
        };append(append,first,last,bounds);}
        return {std::move(result),actual};
    }
public:
    void build(ConvolutionFilterIndex &out,std::span<const ConvolutionIntegerSegment> input){out.root.reset();out.count=input.size();out.height=0;if(input.empty())return;ConvolutionIntegerBox bounds=box(input.front());
        for(const auto &s:input){entries.push_back({{add(s[0][0],s[1][0])/2,add(s[0][1],s[1][1])/2},s});expand(bounds,box(s));}
        size_t capacity=1,next=16;while(next<input.size()){capacity=next;next*=16;++out.height;}out.root=node(0,entries.size(),bounds,capacity,(capacity/16)*4).first;
    }
};
}
int convolutionFilterPointSide(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b,ConvolutionIntegerPoint p){const double x=double(sub(b[0],a[0])),y=double(sub(b[1],a[1])),px=double(sub(p[0],a[0])),py=double(sub(p[1],a[1]));const double cross=py*x-px*y,scale=std::max({1.0,std::abs(x),std::abs(y),std::abs(px),std::abs(py)});return cross==0||(std::isfinite(cross)&&std::abs(cross)<=scale*0x1p-52)?0:cross>0?1:-1;}
bool convolutionFilterSegmentsIntersect(const ConvolutionIntegerSegment &a,const ConvolutionIntegerSegment &b){
    const bool pointA=a[0]==a[1],pointB=b[0]==b[1];if(pointA&&pointB)return a[0]==b[0];const auto ba=box(a),bb=box(b);for(size_t k=0;k<2;++k)if(std::min(ba[k+2],bb[k+2])<std::max(ba[k],bb[k]))return false;
    const int a1=convolutionFilterPointSide(b[0],b[1],a[1]),a0=convolutionFilterPointSide(b[0],b[1],a[0]);if(a0*a1==1)return false;
    const int b1=convolutionFilterPointSide(a[0],a[1],b[1]),b0=convolutionFilterPointSide(a[0],a[1],b[0]);if(b0*b1==1)return false;
    const auto ax=sub(a[1][0],a[0][0]),ay=sub(a[1][1],a[0][1]),bx=sub(b[1][0],b[0][0]),by=sub(b[1][1],b[0][1]);
    if((a0||a1||b0||b1)&&sub(mul(by,ax),mul(ay,bx))!=0)return true;
    const auto x=std::min(magnitude(ax),magnitude(bx)),y=std::min(magnitude(ay),magnitude(by));size_t axis;
    if(pointA)axis=magnitude(bx)<magnitude(by);else if(pointB)axis=magnitude(ax)<magnitude(ay);else {if(x==y&&x<=0)return true;axis=x<y;}
    if(pointA||pointB){const auto &p=pointA?a:b;const auto &s=pointA?b:a;auto delta=sub(s[1][axis],s[0][axis]),offset=sub(p[0][axis],s[0][axis]);if(delta<0){offset=sub(0,offset);delta=sub(0,delta);}return offset>=0&&offset<=delta;}
    return std::max(std::min(a[0][axis],a[1][axis]),std::min(b[0][axis],b[1][axis]))<=std::min(std::max(a[0][axis],a[1][axis]),std::max(b[0][axis],b[1][axis]));
}
bool convolutionFilterSegmentOutside(const ConvolutionIntegerSegment &s,const ConvolutionIntegerBox &b){
    int64_t dx,loX,hiX,dy,loY,hiY;if(s[1][0]<s[0][0]){dx=sub(s[0][0],s[1][0]);loX=sub(s[0][0],b[2]);hiX=sub(s[0][0],b[0]);}else{dx=sub(s[1][0],s[0][0]);loX=sub(b[0],s[0][0]);hiX=sub(b[2],s[0][0]);}
    if(!dx){if(!loX)loX=-1;if(!hiX)hiX=1;if(sign(loX)*sign(hiX)>0)return true;}if(loX>dx||hiX<0)return true;
    if(s[1][1]<s[0][1]){dy=sub(s[0][1],s[1][1]);loY=sub(s[0][1],b[3]);hiY=sub(s[0][1],b[1]);}else{dy=sub(s[1][1],s[0][1]);loY=sub(b[1],s[0][1]);hiY=sub(b[3],s[0][1]);}
    if(!dy){if(!dx&&(loX>hiY||hiX<loY))return true;if(sign(loY)*sign(hiY)>0)return true;}
    const auto left=mul(dy,loX),right=mul(hiY,dx),top=mul(dy,hiX),bottom=mul(loY,dx);if(left>right||top<bottom)return true;if(bottom<=left){loY=loX;dy=dx;}if(loY>dy)return true;if(right>=top)hiY=hiX;return hiY<0;
}
void ConvolutionFilterIndex::build(std::span<const ConvolutionIntegerSegment> edges){BulkBuilder().build(*this,edges);}
bool ConvolutionFilterIndex::intersects(const ConvolutionIntegerSegment &s)const{const auto visit=[&](auto &&self,const ConvolutionIndexNode &n)->bool {if(n.leaf){for(const auto &e:n.entries)if(convolutionFilterSegmentsIntersect(e.edge,s))return true;}else for(size_t k=0;k<n.entries.size();++k)if(!convolutionFilterSegmentOutside(s,n.entries[k].box)&&self(self,*n.children[k]))return true;return false;};return root&&visit(visit,*root);}
ConvolutionIntegerPolygon convolutionFilterTopRings(const ConvolutionHierarchy &node){if(node.ring)return {*node.ring};ConvolutionIntegerPolygon out;for(const auto &child:node.children){if(!child||!child->ring)throw std::invalid_argument("Filter grouping child requires a ring");out.push_back(*child->ring);}return out;}
const std::optional<ConvolutionIntegerPoint> &ConvolutionFilterContext::reference(const ConvolutionIntegerRing &ring){auto [it,fresh]=references.try_emplace(&ring);if(fresh){ConvolutionIntegerPoint p{};if(convolutionHierarchyReference(ring,p))it->second=p;}return it->second;}
const ConvolutionFilterIndex &ConvolutionFilterContext::index(const ConvolutionHierarchy &node){auto [it,fresh]=indices.try_emplace(&node);if(fresh){std::vector<ConvolutionIntegerSegment> edges;for(const auto &ring:convolutionFilterTopRings(node))for(size_t k=0;k<ring.size();++k)edges.push_back({ring[k],ring[(k+1)%ring.size()]});it->second.build(edges);}return it->second;}
bool convolutionFilterContourPair(ConvolutionFilterContext &context,const ConvolutionIntegerRing &shift,const ConvolutionIntegerRing &polygon,const ConvolutionIntegerRing &candidate,bool desired){if(shift.empty())throw std::invalid_argument("Filter shift ring is empty");const auto &p=context.reference(candidate);return p&&convolutionHierarchyContains(polygon,{}, {sub((*p)[0],shift.front()[0]),sub((*p)[1],shift.front()[1])})==desired;}
ConvolutionContourCandidates convolutionFilterContourCandidates(ConvolutionFilterContext &context,const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,std::span<const ConvolutionIntegerRing*const> candidates){const auto &index=context.index(a);const auto rings=convolutionFilterTopRings(b);ConvolutionContourCandidates out;
    for(auto candidate:candidates){if(!candidate)throw std::invalid_argument("Null filter candidate");const auto &point=context.reference(*candidate);if(!point)continue;bool accepted=true;for(const auto &ring:rings){if(ring.empty())throw std::invalid_argument("Empty filter operand ring");ConvolutionIntegerPoint previous{sub((*point)[0],ring.back()[0]),sub((*point)[1],ring.back()[1])};for(auto p:ring){const ConvolutionIntegerPoint current{sub((*point)[0],p[0]),sub((*point)[1],p[1])};if(index.intersects({current,previous})){accepted=false;break;}previous=current;}if(!accepted)break;}if(accepted)out.push_back(candidate);}return out;
}
}
