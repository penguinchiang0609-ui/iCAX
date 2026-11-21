#include "recovered_convolution_filter.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <list>
#include <set>
#include <stdexcept>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
ConvolutionIntegerBox ringBox(const ConvolutionIntegerRing &ring){if(ring.empty())throw std::invalid_argument("Empty recursive filter ring");ConvolutionIntegerBox b{ring[0][0],ring[0][1],ring[0][0],ring[0][1]};for(auto p:ring)for(size_t k=0;k<2;++k){b[k]=std::min(b[k],p[k]);b[k+2]=std::max(b[k+2],p[k]);}return b;}
ConvolutionFilterBox convertedBox(ConvolutionIntegerBox b){const double x=double(b[0]),y=double(b[1]),xx=double(b[2]),yy=double(b[3]);return {std::min(x,xx),std::min(y,yy),std::max(x,xx),std::max(y,yy)};}
uint64_t pointerHash(const ConvolutionIntegerRing *ring){uint64_t hash=0xcbf29ce484222325ULL,value=reinterpret_cast<uintptr_t>(ring);for(size_t n=0;n<8;++n){hash=(hash^(value&255))*0x100000001b3ULL;value>>=8;}return hash;}
ConvolutionContourCandidates pointerSetOrder(std::span<const ConvolutionIntegerRing*const> input){std::list<const ConvolutionIntegerRing*> order;std::set<const ConvolutionIntegerRing*> seen;using Iterator=decltype(order)::iterator;std::vector<Iterator> buckets(8,order.end());
    for(auto item:input){if(!seen.insert(item).second)continue;const auto required=size_t(std::ceil(float(seen.size())));if(required>buckets.size()){size_t desired=std::max<size_t>(8,required);if(buckets.size()<512)desired=std::max(desired,buckets.size()*8);buckets.assign(std::bit_ceil(desired),order.end());for(auto it=order.begin();it!=order.end();){auto next=std::next(it);auto &first=buckets[pointerHash(*it)&(buckets.size()-1)];if(first!=order.end())order.splice(first,order,it);first=it;it=next;}}
        auto &first=buckets[pointerHash(item)&(buckets.size()-1)];first=order.insert(first,item);
    }return {order.begin(),order.end()};
}
ConvolutionContourCandidates filterChildren(ConvolutionFilterContext &context,const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,std::span<const ConvolutionIntegerRing*const> candidates,const ConvolutionHierarchyDiagnostic &diagnostic){
    if(candidates.empty())return {};if(!b.ring)throw std::invalid_argument("Child filter requires an operand ring");std::vector<ConvolutionFilterChildEntry> boxes;for(size_t n=0;n<a.children.size();++n){if(!a.children[n]||!a.children[n]->ring)throw std::invalid_argument("Child filter requires immediate rings");boxes.push_back({convertedBox(ringBox(*a.children[n]->ring)),n});}ConvolutionFilterChildIndex index;index.build(boxes);
    auto opposite=*b.ring;for(auto &p:opposite)for(auto &v:p)v=sub(0,v);const auto bounds=ringBox(opposite);ConvolutionContourCandidates out;
    for(auto candidate:candidates){const auto &point=context.reference(*candidate);if(!point)continue;auto translated=bounds;for(size_t k=0;k<4;++k)translated[k]=add(translated[k],(*point)[k%2]);const auto query=convertedBox(translated);bool keep=true;
        for(bool contained:{false,true}){for(auto n:index.query(query,contained)){const std::array<const ConvolutionIntegerRing*,1> single{candidate};if(convolutionFilterHierarchy(context,*a.children[n],b,single,true,false,diagnostic).empty()){keep=false;break;}}if(!keep)break;}if(keep)out.push_back(candidate);
    }return out;
}
}
ConvolutionContourCandidates convolutionFilterHierarchy(ConvolutionFilterContext &context,const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,std::span<const ConvolutionIntegerRing*const> candidates,bool checked,bool inherited,const ConvolutionHierarchyDiagnostic &diagnostic){
    if(a.hole&&b.hole){if(diagnostic)diagnostic("ComputeValidIntenalAndExternalPolygons: Polygon1 and polygon2 cannot be two holes");return {};}
    if(candidates.empty())return {};
    if(a.hole)return convolutionFilterHierarchy(context,b,a,candidates,checked,inherited,diagnostic);
    if(!a.ring){if(!b.hole&&!b.ring&&a.children.size()>b.children.size())return convolutionFilterHierarchy(context,b,a,candidates,checked,inherited,diagnostic);
        auto filtered=checked?ConvolutionContourCandidates(candidates.begin(),candidates.end()):convolutionFilterContourCandidates(context,a,b,candidates);
        if(!b.hole&&b.ring)return filterChildren(context,a,b,filtered,diagnostic);
        for(const auto &child:a.children)filtered=convolutionFilterHierarchy(context,*child,b,filtered,true,inherited,diagnostic);return filtered;
    }
    if(b.hole){
        if(!b.ring){auto remaining=pointerSetOrder(candidates);ConvolutionContourCandidates out;for(const auto &child:b.children){auto accepted=convolutionFilterHierarchy(context,a,*child,remaining,checked,inherited,diagnostic);out.insert(out.end(),accepted.begin(),accepted.end());std::set<const ConvolutionIntegerRing*> removed(accepted.begin(),accepted.end());std::erase_if(remaining,[&](auto p){return removed.contains(p);});}return out;}
        auto filtered=checked?ConvolutionContourCandidates(candidates.begin(),candidates.end()):convolutionFilterContourCandidates(context,a,b,candidates);std::erase_if(filtered,[&](auto ring){return !convolutionFilterContourPair(context,*a.ring,*b.ring,*ring,b.hole);});
        for(const auto &child:b.children)filtered=convolutionFilterHierarchy(context,a,*child,filtered,inherited,inherited,diagnostic);return filtered;
    }
    if(!b.ring)return convolutionFilterHierarchy(context,b,a,candidates,checked,inherited,diagnostic);
    const auto disposition=convolutionHierarchyPairDisposition(*a.ring,*b.ring);if(disposition[0]){if(disposition[1])throw std::invalid_argument("Mutually strict dimension disposition");return convolutionFilterHierarchy(context,b,a,candidates,checked,inherited,diagnostic);}
    const auto filtered=checked?ConvolutionContourCandidates(candidates.begin(),candidates.end()):convolutionFilterContourCandidates(context,a,b,candidates);ConvolutionContourCandidates accepted,rejected;
    for(auto ring:filtered)(convolutionFilterContourPair(context,*b.ring,*a.ring,*ring,a.hole)?accepted:rejected).push_back(ring);
    if(!a.children.empty()&&disposition[1]){auto group=a;group.ring.reset();group.hole=true;auto additional=convolutionFilterHierarchy(context,b,group,rejected,inherited,inherited,diagnostic);accepted.insert(accepted.end(),additional.begin(),additional.end());}return accepted;
}
ConvolutionContourCandidates convolutionFilterNegativeContours(const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,std::span<const ConvolutionIntegerRing*const> candidates,bool checked,const ConvolutionHierarchyDiagnostic &diagnostic){ConvolutionFilterContext context;return convolutionFilterHierarchy(context,a,b,candidates,checked,checked,diagnostic);}
ConvolutionIntegerPolygon convolutionFilterNegativeRings(const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,const ConvolutionIntegerPolygon &rings,bool checked,const ConvolutionHierarchyDiagnostic &diagnostic){ConvolutionContourCandidates input;for(const auto &ring:rings)input.push_back(&ring);ConvolutionIntegerPolygon out;for(auto ring:convolutionFilterNegativeContours(a,b,input,checked,diagnostic))out.push_back(*ring);return out;}
}
