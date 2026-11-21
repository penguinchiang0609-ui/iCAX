#include "recovered_fuzzy_polygon.hpp"
#include "recovered_convolution_offset.hpp"
#include <bit>
#include <list>
#include <set>

namespace study::recovered {
namespace {
uint64_t mix(uint64_t v){v=(v^(v>>32))*0xe9846af9b1a615dULL;v=(v^(v>>32))*0xe9846af9b1a615dULL;return v^(v>>28);}
uint64_t hash(EscherIntegerPoint p){return mix(mix(uint64_t(p[0])+0x9e3779b9)+0x9e3779b9+uint64_t(p[1]));}
uint64_t hash(int32_t value){uint64_t v=0xcbf29ce484222325ULL;for(unsigned n=0;n<4;++n)v=(v^((uint32_t(value)>>(n*8))&255U))*0x100000001b3ULL;return v;}
template<class Key>class NativeSet {
    using Iterator=typename std::list<Key>::iterator;
    std::list<Key> order_;
    std::set<Key> keys_;
    std::vector<Iterator> buckets_;
    void rehash(size_t capacity){buckets_.assign(capacity,order_.end());for(auto i=order_.begin();i!=order_.end();){const auto next=std::next(i);auto &first=buckets_[hash(*i)&(capacity-1)];if(first!=order_.end())order_.splice(first,order_,i);first=i;i=next;}}
public:
    NativeSet():buckets_(8,order_.end()){}
    bool contains(const Key &key)const{return keys_.contains(key);}
    void insert(const Key &key){if(!keys_.insert(key).second)return;const auto count=keys_.size();if(buckets_.size()<count){auto capacity=std::max<size_t>(8,count);if(buckets_.size()<512)capacity=std::max(capacity,buckets_.size()*8);rehash(std::bit_ceil(capacity));}
        auto &first=buckets_[hash(key)&(buckets_.size()-1)];first=order_.insert(first,key);}
    const std::list<Key> &values()const{return order_;}
};
int64_t dot(EscherIntegerPoint a,EscherIntegerPoint b){return std::bit_cast<int64_t>(uint64_t(a[0])*uint64_t(b[0])+uint64_t(a[1])*uint64_t(b[1]));}
std::array<int64_t,4> key(EscherIntegerPoint p,EscherIntegerPoint first,EscherIntegerPoint second){return {dot(p,first),dot(p,second),p[0],p[1]};}
struct Distance {double value=0.;int32_t source=0,padding=0;};
}

FuzzyPolygonMaximum fuzzyPolygonMaximum(const FuzzyPolygonRegion &root){
    FuzzyPolygonMaximum best{&root,root.weight};
    for(const auto &child:root.children){require(bool(child),PN2S_INVALID,"Fuzzy region child is missing");
        const auto candidate=fuzzyPolygonMaximum(*child);const auto weight=candidate.weight+root.weight;
        if(best.weight<weight)best={candidate.region,weight};
    }
    return best;
}
EscherIntegerPoint fuzzyPolygonDirectionalPoint(const FuzzyPolygonRegion &region,
    EscherIntegerPoint primary,EscherIntegerPoint secondary){
    bool first=true;EscherIntegerPoint best{};std::array<int64_t,4> score{};
    const auto consider=[&](EscherIntegerPoint p){const auto candidate=key(p,primary,secondary);if(first||score<candidate){first=false;best=p;score=candidate;}};
    if(region.geometryPresent){for(const auto &edge:region.boundary)for(const auto point:edge)consider(point);}
    else for(const auto &child:region.children){require(bool(child),PN2S_INVALID,"Fuzzy region child is missing");consider(fuzzyPolygonDirectionalPoint(*child,primary,secondary));}
    require(!first,PN2S_INVALID,"Fuzzy directional query requires boundary edges or children");
    return best;
}
std::vector<EscherIntegerPoint> fuzzyPolygonNearbyVertices(
    const std::map<EscherIntegerPoint,std::vector<EscherIntegerPoint>> &adjacency,EscherIntegerPoint point,int32_t depth){
    NativeSet<EscherIntegerPoint> visited;
    const auto visit=[&](auto &&self,EscherIntegerPoint p,int32_t remaining)->void{
        visited.insert(p);if(!remaining)return;const auto found=adjacency.find(p);if(found==adjacency.end())return;
        for(const auto next:found->second)if(!visited.contains(next))self(self,next,std::bit_cast<int32_t>(uint32_t(remaining)-1U));
    };
    visit(visit,point,depth);return {visited.values().begin(),visited.values().end()};
}
FuzzyPolygonSelection fuzzyPolygonBestPoint(const FuzzyPolygonPrepared &input,
    EscherIntegerPoint primary,EscherIntegerPoint secondary){
    FuzzyPolygonSelection result;result.success=true;
    require(input.polygons.size()==input.weights.size(),PN2S_INVALID,"Fuzzy polygon weights do not match their source contours");
    if(input.polygons.empty())return result;
    require(bool(input.root),PN2S_INVALID,"Fuzzy weighted region tree is missing");
    const auto maximum=fuzzyPolygonMaximum(*input.root);
    const auto point=fuzzyPolygonDirectionalPoint(*maximum.region,primary,secondary);
    result.selection.origin=point;result.selection.weight=maximum.weight;
    const auto radius=int32_t(std::log2(double(input.polygons.size())+1.));
    NativeSet<int32_t> included,excluded;
    for(const auto vertex:fuzzyPolygonNearbyVertices(input.adjacency,point,radius)){
        const auto found=input.incidentSegments.find(vertex);if(found==input.incidentSegments.end())continue;
        for(const auto segment:found->second)for(const auto source:input.segmentSources.at(size_t(segment))){
            if(input.weights.at(size_t(source))<=0.)excluded.insert(source);else included.insert(source);
        }
    }
    for(size_t n=0;n<input.polygons.size();++n){const auto source=int32_t(n);
        if(!included.contains(source)&&!excluded.contains(source)&&escherBoundaryPointContains({input.polygons[n]},point))included.insert(source);
    }
    double weight=0.;for(const auto source:included.values())weight+=input.weights.at(size_t(source));
    if(weight<maximum.weight){
        std::vector<Distance> distances;
        for(size_t n=0;n<input.polygons.size();++n)if(!included.contains(int32_t(n))&&!excluded.contains(int32_t(n)))
            distances.push_back({convolutionOffsetDistance(point,{input.polygons[n]}),int32_t(n)});
        // 3a9890 orders finite distances, then the unique original source ID.
        // The generated records have distinct IDs, giving a total order.
        std::sort(distances.begin(),distances.end(),[](const auto &a,const auto &b){
            if(a.value<b.value)return true;if(b.value<a.value)return false;return a.source<b.source;
        });
        for(size_t n=0;weight<maximum.weight&&n<distances.size();++n){const auto &candidate=distances[n];if(candidate.value>double(radius))break;
            weight+=input.weights.at(size_t(candidate.source));included.insert(candidate.source);
        }
    }
    if(weight!=maximum.weight)result.success=false;
    result.selection.indices.assign(included.values().begin(),included.values().end());return result;
}
}
