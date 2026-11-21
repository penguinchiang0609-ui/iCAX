#include "recovered_convolution.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>
#include <stdexcept>
namespace study::recovered {
namespace {
constexpr double tau=6.283185307179586;
double wrap(double v){while(v<0)v=v+tau;while(tau<=v)v=v-tau;return v;}
void pushUp(std::vector<ConvolutionEvent> &heap,size_t hole,const ConvolutionEvent &item){
    while(hole>0){const auto parent=(hole-1)/2;if(convolutionEventCompare(item,heap[parent])!=-1)break;heap[hole]=heap[parent];hole=parent;}heap[hole]=item;
}
}
std::vector<ConvolutionEntity> convolutionPolygon(std::span<const ShakerPoint> points){
    if(points.size()<3)throw std::invalid_argument("Convolution polygon needs three vertices");
    std::vector<double> angles(points.size());
    for(size_t n=0;n<points.size();++n){const auto a=points[n],b=points[(n+1)%points.size()];if(a==b)throw std::invalid_argument("Zero length edge requires topology normalization");
        const double dx=b[0]-a[0],dy=b[1]-a[1];if(!std::isfinite(dx)||!std::isfinite(dy))throw std::invalid_argument("Non-finite convolution edge");angles[n]=wrap(std::atan2(-dy,-dx));}
    std::vector<ConvolutionEntity> entities;entities.reserve(points.size()*2);
    for(size_t n=0;n<points.size();++n){const double first=angles[(n+points.size()-1)%points.size()],last=angles[n];
        entities.push_back({ConvolutionEntity::Kind::vertex,first,last,int32_t(2*n),wrap(last-first)<3.141592653589793,points[n],points[n]});
        entities.push_back({ConvolutionEntity::Kind::straight,last,last,int32_t(2*n+1),true,points[n],points[(n+1)%points.size()]});}
    return entities;
}
void convolutionBroaden(ConvolutionEntity &e,double alpha){
    const double last=e.shortArc?e.last+alpha:e.last-alpha,first=e.shortArc?e.first-alpha:e.first+alpha;
    e.first=wrap(first);e.last=wrap(last);
}
bool convolutionCompatible(const ConvolutionEntity &a,const ConvolutionEntity &b,bool keepOrder){
    const auto &x=keepOrder?a:b,&y=keepOrder?b:a;
    const double f1=y.shortArc?y.first:y.last,f2=y.shortArc?y.last:y.first,f8=x.shortArc?x.first:x.last,f3=x.shortArc?x.last:x.first;
    if(f2<f1){if(f3<f8||f8<=f2)return true;}
    else{if(f3<f8){if(f1<f3||f8<=f2)return true;return false;}if(f2<f8)return false;}
    return f1<f3;
}
int convolutionEventCompare(const ConvolutionEvent &a,const ConvolutionEvent &b){
    if(a.time<b.time)return -1;if(b.time<a.time)return 1;if(!(a.time==b.time))return -128;
    if(a.phase!=b.phase)return a.phase<b.phase?-1:1;if(a.id!=b.id)return a.id<b.id?-1:1;return 0;
}
void convolutionEventPush(std::vector<ConvolutionEvent> &heap,ConvolutionEvent item){heap.push_back(item);pushUp(heap,heap.size()-1,item);}
ConvolutionEvent convolutionEventPop(std::vector<ConvolutionEvent> &heap){
    if(heap.empty())throw std::invalid_argument("Empty convolution event heap");const auto result=heap.front(),item=heap.back();heap.pop_back();
    if(heap.empty())return result;
    size_t hole=0;const auto count=heap.size(),limit=(count-1)/2;
    while(hole<limit){size_t child=hole*2+1;if(convolutionEventCompare(heap[child],heap[child+1])!=-1)++child;heap[hole]=heap[child];hole=child;}
    if(hole==limit&&count%2==0){heap[hole]=heap[count-1];hole=count-1;}
    pushUp(heap,hole,item);return result;
}
std::vector<ConvolutionEvent> convolutionEvents(std::span<const ConvolutionEntity> entities,bool sideA,uint32_t &nextId){
    std::vector<ConvolutionEvent> result;result.reserve(entities.size()*3);
    for(size_t n=0;n<entities.size();++n){const auto &e=entities[n];const double first=e.shortArc?e.first:e.last;double last=e.shortArc?e.last:e.first;if(last<first)last=last+tau;
        result.push_back({ConvolutionEvent::Kind::insert,first-1e-8,nextId++,0,n,sideA});
        result.push_back({ConvolutionEvent::Kind::peek,first+tau,nextId++,0,n,sideA});
        result.push_back({ConvolutionEvent::Kind::remove,last+1e-8,nextId++,1,n,sideA});}
    return result;
}
std::vector<ConvolutionPair> convolutionSweep(std::span<const ConvolutionEntity> a,std::span<const ConvolutionEntity> b){
    std::vector<ConvolutionEvent> heap;uint32_t nextId=0;
    for(const auto &e:convolutionEvents(a,true,nextId))convolutionEventPush(heap,e);
    for(const auto &e:convolutionEvents(b,false,nextId))convolutionEventPush(heap,e);
    std::array<std::vector<size_t>,2> active;std::vector<ConvolutionPair> pairs;
    while(!heap.empty()){
        const auto e=convolutionEventPop(heap);auto &own=active[e.sideA?1:0];const auto &opposite=active[e.sideA?0:1];const auto &entity=e.sideA?a[e.entity]:b[e.entity];
        if(e.kind==ConvolutionEvent::Kind::remove){const auto it=std::find(own.begin(),own.end(),e.entity);if(it==own.end())throw std::invalid_argument("Convolution removal without insertion");own.erase(it);continue;}
        for(auto n:opposite){const auto &other=e.sideA?b[n]:a[n];if(convolutionCompatible(entity,other,!e.sideA))pairs.push_back(e.sideA?ConvolutionPair{e.entity,n}:ConvolutionPair{n,e.entity});}
        if(e.kind==ConvolutionEvent::Kind::insert)own.push_back(e.entity);
    }
    return pairs;
}
bool convolutionSegment(const ConvolutionEntity &a,const ConvolutionEntity &b,ShakerBoundary &out){
    using Kind=ConvolutionEntity::Kind;
    if(a.kind==Kind::vertex&&b.kind==Kind::vertex){out.start={a.start[0]+b.start[0],a.start[1]+b.start[1]};out.end=out.start;out.direction=a.shortArc&&b.shortArc;return false;}
    const auto *vertex=&a,*edge=&b;if(a.kind!=Kind::vertex){vertex=&b;edge=&a;}
    if(vertex->kind!=Kind::vertex||edge->kind!=Kind::straight)return false;
    out.start={vertex->start[0]+edge->start[0],vertex->start[1]+edge->start[1]};out.end={vertex->start[0]+edge->end[0],vertex->start[1]+edge->end[1]};out.direction=vertex->shortArc;
    out.sourceA={a.start,a.kind==Kind::vertex};out.sourceB={b.start,b.kind==Kind::vertex};out.sourceAElement=a.kind!=Kind::vertex;out.sourceBElement=b.kind!=Kind::vertex;
    out.sourceAIdentity=a.identity?a.identity:reinterpret_cast<uintptr_t>(&a);
    out.sourceBIdentity=b.identity?b.identity:reinterpret_cast<uintptr_t>(&b);
    return true;
}
void convolutionLinkPairs(std::span<const ConvolutionEntity> a,std::span<const ConvolutionEntity> b,std::vector<ConvolutionPair> &pairs){
    for(auto &p:pairs){if(p.a>=a.size()||p.b>=b.size())throw std::out_of_range("Convolution source index");p.next=p.previous=-1;}
    std::vector<size_t> order(pairs.size());std::iota(order.begin(),order.end(),0);
    for(int side=0;side<2;++side){
        const auto fixed=[&](size_t n){const auto &p=pairs[n];return side?b[p.b].index:a[p.a].index;};
        const auto moving=[&](size_t n){const auto &p=pairs[n];return side?a[p.a].index:b[p.b].index;};
        const auto identity=[&](size_t n){return pairs[n].identity?pairs[n].identity:uint64_t(reinterpret_cast<uintptr_t>(&pairs[n]));};
        std::sort(order.begin(),order.end(),[&](size_t x,size_t y){if(fixed(x)!=fixed(y))return fixed(x)<fixed(y);if(moving(x)!=moving(y))return moving(x)<moving(y);return identity(x)<identity(y);});
        for(size_t first=0;first<order.size();){size_t last=first+1;while(last<order.size()&&fixed(order[first])==fixed(order[last]))++last;
            for(size_t n=first;n<last;++n){const auto x=order[n],y=order[n+1<last?n+1:first];if((moving(x)+1)%int32_t(side?a.size():b.size())!=moving(y))continue;
                const auto &p=pairs[x];const bool forward=side?b[p.b].shortArc:a[p.a].shortArc;
                if(forward){pairs[x].next=int32_t(y);pairs[y].previous=int32_t(x);}else{pairs[y].next=int32_t(x);pairs[x].previous=int32_t(y);}}
            first=last;
        }
    }
}
std::vector<ShakerBoundary> convolutionRecords(std::span<const ConvolutionEntity> a,std::span<const ConvolutionEntity> b,std::vector<ConvolutionPair> &pairs,bool adjacency){
    std::vector<ShakerBoundary> records;
    for(auto &p:pairs){ShakerBoundary record;if(convolutionSegment(a[p.a],b[p.b],record)){if(adjacency)p.record=int32_t(records.size());records.push_back(record);}else p.record=-1;}
    if(adjacency){for(const auto &p:pairs)if(p.record>=0){auto &r=records[size_t(p.record)];
        const auto skip=[&](int32_t n,bool next){if(n<0)return -1;n=next?pairs[size_t(n)].next:pairs[size_t(n)].previous;return n<0?-1:pairs[size_t(n)].record;};
        r.previous=skip(p.previous,false);r.next=skip(p.next,true);}}
    return records;
}
}
