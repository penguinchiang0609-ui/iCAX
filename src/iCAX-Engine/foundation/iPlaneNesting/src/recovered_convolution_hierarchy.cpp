#include "recovered_convolution_hierarchy.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int sign(int64_t a){return (a>0)-(a<0);}
int64_t cross(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return sub(mul(a[0],b[1]),mul(a[1],b[0]));}
ConvolutionIntegerPoint difference(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return {sub(a[0],b[0]),sub(a[1],b[1])};}
int64_t truncate(double v){return v>=-0x1p63&&v<0x1p63?int64_t(v):INT64_MIN;}
}
ConvolutionHierarchyForest convolutionReduceHierarchyRelations(std::span<const ConvolutionHierarchyRelation> relations){
    const size_t count=relations.size();std::vector<std::vector<int32_t>> contained(count),parallel(count);std::vector<int32_t> roots;
    for(size_t n=0;n<count;++n){const auto r=relations[n];if(r.parent==-1)roots.push_back(int32_t(n));else {if(r.parent<0||size_t(r.parent)>=count)throw std::invalid_argument("Invalid hierarchy parent");(r.contained?contained:parallel)[r.parent].push_back(int32_t(n));}}
    ConvolutionHierarchyForest out;out.children.resize(count);std::vector<uint8_t> visited(count);
    const auto visit=[&](auto &&self,int32_t node,int32_t parent)->void {
        if(visited[node])throw std::invalid_argument("Cyclic hierarchy relations");visited[node]=1;
        (parent==-1?out.roots:out.children[parent]).push_back(node);
        for(auto next:parallel[node])self(self,next,parent);
        for(auto next:contained[node])self(self,next,node);
    };
    for(auto root:roots)visit(visit,root,-1);
    // The original visits only components reachable from its roots.
    return out;
}
bool convolutionHierarchyEmpty(const ConvolutionHierarchy &node){return (!node.ring||node.ring->empty())&&node.children.empty();}
bool convolutionHierarchySimple(const ConvolutionHierarchy &node){
    for(const auto &child:node.children)if(bool(!node.ring)!=(child->hole==node.hole)||!child->ring)return false;
    for(const auto &child:node.children)if(!convolutionHierarchySimple(*child))return false;return true;
}
void convolutionCheckHierarchy(const ConvolutionHierarchy &node,bool expected,const ConvolutionHierarchyDiagnostic &diagnostic){
    if(node.hole!=expected&&diagnostic)diagnostic("Bad normative hole/external alternance");
    if(node.ring){if(node.ring->size()<2)throw std::invalid_argument("Hierarchy orientation requires at least two distinct points");if(node.hole!=(convolutionIntegerOrientation(*node.ring)==-1)&&diagnostic)diagnostic("Bad descritive hole/external alternance");expected=!expected;}
    for(const auto &child:node.children)convolutionCheckHierarchy(*child,expected,diagnostic);
}
ConvolutionHierarchy convolutionBuildHierarchy(const ConvolutionIntegerPolygon &rings,std::span<const ConvolutionHierarchyRelation> relations,std::span<const uint8_t> holes,bool allowNonSimple,const ConvolutionHierarchyDiagnostic &diagnostic){
    if(rings.size()!=relations.size()||holes.size()!=rings.size())throw std::invalid_argument("Hierarchy input sizes differ");
    const auto forest=convolutionReduceHierarchyRelations(relations);
    if(forest.roots.empty())throw std::invalid_argument("Hierarchy assembly requires a root");
    const auto build=[&](auto &&self,int32_t index)->ConvolutionHierarchy {
        ConvolutionHierarchy node;node.hole=holes[index]!=0;node.ring=std::make_shared<const ConvolutionIntegerRing>(rings[index]);
        for(auto child:forest.children[index])node.children.push_back(std::make_shared<ConvolutionHierarchy>(self(self,child)));return node;
    };
    if(forest.roots.size()==1){auto node=build(build,forest.roots[0]);if(!allowNonSimple&&!convolutionHierarchySimple(node)&&diagnostic){diagnostic("Hierarchize produced not valid polygon on clean entry in single root-case");diagnostic("");}return node;}
    ConvolutionHierarchy node;node.hole=holes[forest.roots[0]]!=0;for(auto root:forest.roots)node.children.push_back(std::make_shared<ConvolutionHierarchy>(build(build,root)));return node;
}
ConvolutionHierarchy convolutionCanonicalizeHierarchy(const ConvolutionHierarchy &input,const ConvolutionHierarchyDiagnostic &diagnostic){
    ConvolutionHierarchy result;result.ring=input.ring;result.hole=input.hole;
    for(const auto &child:input.children){auto normalized=convolutionCanonicalizeHierarchy(*child,diagnostic);
        if((normalized.hole==input.hole&&input.ring)||(normalized.hole!=input.hole&&!input.ring)){
            if(input.ring||normalized.ring)result.children.insert(result.children.end(),normalized.children.begin(),normalized.children.end());
        }else result.children.push_back(std::make_shared<ConvolutionHierarchy>(std::move(normalized)));
    }
    convolutionCheckHierarchy(result,result.hole,diagnostic);return result;
}
int64_t convolutionIntegerArea(std::span<const ConvolutionIntegerPoint> ring){
    if(ring.empty())throw std::invalid_argument("Integer area requires a nonempty ring");int64_t total=0;auto last=ring.back();for(auto p:ring){total=add(total,cross(last,p));last=p;}return total/2;
}
ConvolutionHierarchyFlattened convolutionFlattenHierarchy(const ConvolutionHierarchy &tree){
    ConvolutionHierarchyFlattened out;const auto visit=[&](auto &&self,const ConvolutionHierarchy &node,bool root)->void {
        if(node.ring){if(root)out.roots.push_back(int32_t(out.rings.size()));out.rings.push_back(*node.ring);root=false;}
        for(const auto &child:node.children)self(self,*child,root);
    };visit(visit,tree,true);return out;
}
std::array<bool,2> convolutionHierarchyPairDisposition(std::span<const ConvolutionIntegerPoint> a,std::span<const ConvolutionIntegerPoint> b){
    const auto size=[](std::span<const ConvolutionIntegerPoint> ring){if(ring.empty())throw std::invalid_argument("Pair disposition requires nonempty rings");auto lo=ring.front(),hi=lo;for(auto p:ring)for(size_t n=0;n<2;++n){lo[n]=std::min(lo[n],p[n]);hi[n]=std::max(hi[n],p[n]);}return difference(hi,lo);};
    const auto sa=size(a),sb=size(b);return {sa[0]<sb[0]&&sa[1]<sb[1],sb[0]<sa[0]&&sb[1]<sa[1]};
}
void convolutionClassifyHierarchyRay(ConvolutionIntegerPoint direction,ConvolutionIntegerPoint reference,ConvolutionIntegerPoint translation,ConvolutionHierarchyRayHit &hit,bool restore){
    for(auto &p:hit.segment)for(size_t axis=0;axis<2;++axis)p[axis]=add(p[axis],translation[axis]);
    const auto a=hit.segment[0],b=hit.segment[1];
    if(!cross(difference(b,a),direction)){hit.point={0,0};hit.alignment=0;hit.distanceSquared=0;hit.crossing=0;}
    else {
        hit.crossing=sign(cross(difference(a,reference),difference(b,reference)))*2;
        const int sa=sign(cross(direction,difference(a,reference))),sb=sign(cross(direction,difference(b,reference)));
        if(!sa||!sb){hit.point=(!sa&&sb)?a:b;hit.crossing/=2;auto delta=difference(b,a);if(hit.point==b)delta={sub(0,delta[0]),sub(0,delta[1])};
            const double length=std::sqrt(double(delta[1])*double(delta[1])+double(delta[0])*double(delta[0]));
            hit.alignment=double(add(mul(delta[0],direction[0]),mul(delta[1],direction[1])))/length;
        }else {
            const double ax=double(a[0]),ay=double(a[1]),bx=double(b[0]),by=double(b[1]),dx=double(direction[0]),dy=double(direction[1]);
            const double x=ax-bx,y=ay-by,c=ax*by-ay*bx,den=(-dy)*x-y*(-dx);
            const double px=((-dx)*c-x*0.0)/den,py=((-dy)*c-y*0.0)/den;
            hit.point={truncate(px+(px<0?-0.5:0.5)),truncate(py+(py<0?-0.5:0.5))};
        }
        hit.distanceSquared=double(hit.point[1])*double(hit.point[1])+double(hit.point[0])*double(hit.point[0]);
    }
    if(restore){for(auto &p:hit.segment)for(size_t axis=0;axis<2;++axis)p[axis]=sub(p[axis],translation[axis]);for(size_t axis=0;axis<2;++axis)hit.point[axis]=sub(hit.point[axis],translation[axis]);}
}
bool convolutionHierarchyRayIntersects(const ConvolutionIntegerSegment &ray,const ConvolutionIntegerSegment &edge){
    const auto direction=difference(ray[1],ray[0]),a=difference(edge[0],ray[0]),b=difference(edge[1],ray[0]);const int sa=sign(cross(direction,a)),sb=sign(cross(direction,b));
    if(sa==sb&&sa)return false;const auto side=cross(difference(edge[1],edge[0]),difference(ray[0],edge[0]));
    if(sa)return mul(sa,side)<=0;if(sb)return mul(sb,side)>=0;
    const size_t axis=direction[0]==0?1:0;return mul(sign(direction[axis]),a[axis])>=0||mul(sign(direction[axis]),b[axis])>=0;
}
bool convolutionHierarchyEndpointOrder(const ConvolutionHierarchyRayHit &a,const ConvolutionHierarchyRayHit &b,const ConvolutionHierarchyDiagnostic &diagnostic){
    if(a.crossing==1&&b.crossing==1)return true;if(a.crossing==-1&&b.crossing==-1)return false;
    if((a.segment[0]==a.point)==(b.segment[0]==b.point)&&diagnostic)diagnostic("We have a source or a sink in a polygon");
    const auto away=[](const auto &hit){auto p=hit.segment[0];if(p==hit.point)p=hit.segment[1];else if(hit.segment[1]!=hit.point)p={0,0};return difference(p,hit.point);};
    const auto da=away(a),db=away(b);const auto magnitude=[](int64_t n){return n<0?sub(0,n):n;};bool selectA=da[1]<0;
    if(mul(da[1],db[1])>=0){const auto ay=selectA?sub(0,da[1]):da[1],by=selectA?sub(0,db[1]):db[1];selectA=(mul(by,magnitude(da[0]))>mul(magnitude(db[0]),ay))!=selectA;}
    return (selectA?a.crossing:b.crossing)>0;
}
namespace {
class HierarchyRaySort {
    std::vector<ConvolutionHierarchyRayHit> &hits;
    static bool before(const ConvolutionHierarchyRayHit &a,const ConvolutionHierarchyRayHit &b){
        if(a.distanceSquared!=b.distanceSquared)return a.distanceSquared>b.distanceSquared;
        const auto product=std::bit_cast<int32_t>(uint32_t(a.crossing)*uint32_t(b.crossing));const auto magnitude=product<0?std::bit_cast<int32_t>(0u-uint32_t(product)):product;
        return magnitude<=1?a.alignment>b.alignment:a.crossing>b.crossing;
    }
    void insertion(size_t first,size_t last){for(size_t n=first+1;n<last;++n){auto item=hits[n];size_t j=n;if(before(item,hits[first])){std::move_backward(hits.begin()+first,hits.begin()+n,hits.begin()+n+1);j=first;}else while(j>first&&before(item,hits[j-1])){hits[j]=hits[j-1];--j;}hits[j]=item;}}
    void median(size_t first,size_t middle,size_t last){if(before(hits[middle],hits[first]))std::swap(hits[middle],hits[first]);if(before(hits[last],hits[middle])){std::swap(hits[last],hits[middle]);if(before(hits[middle],hits[first]))std::swap(hits[middle],hits[first]);}}
    std::pair<size_t,size_t> partition(size_t first,size_t last){
        const auto mid=first+(last-first)/2;
        if(last-first>41){const auto step=(last-first)/8;median(first,first+step,first+2*step);median(mid-step,mid,mid+step);median(last-1-2*step,last-1-step,last-1);median(first+step,mid,last-1-step);}else median(first,mid,last-1);
        size_t pfirst=mid,plast=mid+1;while(first<pfirst&&!before(hits[pfirst-1],hits[pfirst])&&!before(hits[pfirst],hits[pfirst-1]))--pfirst;while(plast<last&&!before(hits[plast],hits[pfirst])&&!before(hits[pfirst],hits[plast]))++plast;
        size_t right=plast,left=pfirst;
        for(;;){for(;right<last;++right){if(before(hits[pfirst],hits[right]))continue;if(before(hits[right],hits[pfirst]))break;if(plast!=right)std::swap(hits[plast],hits[right]);++plast;}
            for(;first<left;--left){if(before(hits[left-1],hits[pfirst]))continue;if(before(hits[pfirst],hits[left-1]))break;--pfirst;if(pfirst!=left-1)std::swap(hits[pfirst],hits[left-1]);}
            if(left==first&&right==last)return {pfirst,plast};
            if(left==first){if(plast!=right)std::swap(hits[pfirst],hits[plast]);++plast;std::swap(hits[pfirst++],hits[right++]);}
            else if(right==last){--left;--pfirst;if(left!=pfirst)std::swap(hits[left],hits[pfirst]);std::swap(hits[pfirst],hits[--plast]);}
            else std::swap(hits[right++],hits[--left]);
        }
    }
    void down(size_t base,size_t hole,size_t count,ConvolutionHierarchyRayHit item){const auto top=hole,limit=(count-1)/2;
        while(hole<limit){size_t child=hole*2+1;if(!before(hits[base+child+1],hits[base+child]))++child;hits[base+hole]=hits[base+child];hole=child;}
        if(hole==limit&&count%2==0){hits[base+hole]=hits[base+count-1];hole=count-1;}
        while(top<hole){const auto parent=(hole-1)/2;if(!before(hits[base+parent],item))break;hits[base+hole]=hits[base+parent];hole=parent;}hits[base+hole]=item;
    }
    void heap(size_t first,size_t last){size_t count=last-first;for(size_t n=count/2;n>0;--n)down(first,n-1,count,hits[first+n-1]);while(count>1){auto item=hits[first+count-1];hits[first+count-1]=hits[first];down(first,0,--count,item);}}
    void sort(size_t first,size_t last,size_t depth){while(last-first>32){if(!depth){heap(first,last);return;}const auto [a,b]=partition(first,last);depth=(depth>>1)+(depth>>2);if(a-first<last-b){sort(first,a,depth);first=b;}else{sort(b,last,depth);last=a;}}if(first<last)insertion(first,last);}
public:
    explicit HierarchyRaySort(std::vector<ConvolutionHierarchyRayHit> &input):hits(input){}
    void run(size_t depth){sort(0,hits.size(),depth);}
};
}
void convolutionSortHierarchyRayHits(std::vector<ConvolutionHierarchyRayHit> &hits,size_t depth){HierarchyRaySort(hits).run(depth==SIZE_MAX?hits.size():depth);}
}
