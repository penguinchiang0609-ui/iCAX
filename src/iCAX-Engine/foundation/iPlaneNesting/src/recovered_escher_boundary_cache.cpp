#include "recovered_escher_boundary_cache.hpp"
#include "recovered_escher_boundary_length.hpp"
#include "recovered_convolution_minkowski.hpp"
#include <bit>
#include <mutex>
#include <tuple>
namespace study::recovered {
namespace {
uintptr_t identity(const EscherNfpMember &member){return member.ownerIdentity?member.ownerIdentity:reinterpret_cast<uintptr_t>(member.contours);}
void negate(ConvolutionIntegerPolygon &rings){for(auto &ring:rings)for(auto &point:ring)for(auto &value:point)value=std::bit_cast<int64_t>(uint64_t(0)-uint64_t(value));}
ConvolutionIntegerPolygon finish(ConvolutionIntegerPolygon polygon,const EscherNfpCanonicalPair &pair){escherTransformNfp(polygon,pair.second.angle,pair.second.reflected);if(pair.swapped)negate(polygon);return polygon;}
}
int escherComparePlacementMapKeys(const EscherPlacementMapKey &a,const EscherPlacementMapKey &b){
    const auto first=std::tie(a.firstOwner,a.secondOwner,a.reflection,a.angle),second=std::tie(b.firstOwner,b.secondOwner,b.reflection,b.angle);return first<second?-1:first>second?1:0;
}
std::optional<ConvolutionIntegerPolygon> EscherPlacementMapCache::query(const EscherNfpMember &first,const EscherNfpMember &second,bool cacheOnly){
    require(first.contours&&second.contours,PN2S_INVALID,"Escher placement map requires normalized contours");
    if(first.contours->empty()||second.contours->empty())return ConvolutionIntegerPolygon{};
    const auto pair=escherCanonicalNfpPair(first,second);
    const EscherPlacementMapKey key{identity(pair.first),identity(pair.second),pair.relativeReflection,pair.relativeAngle};
    {std::shared_lock lock(mutex_);const auto found=maps_.find(key);if(found!=maps_.end())return finish(found->second,pair);}
    if(cacheOnly)return std::nullopt;
    auto negative=*pair.first.contours;escherTransformNfp(negative,pair.relativeAngle,pair.relativeReflection);negate(negative);
    for(auto &ring:negative)convolutionNormalizeIntegerRing(ring);convolutionRemoveEmptyIntegerRings(negative);
    auto canonical=convolutionIntegerMinkowski(*pair.second.contours,negative).rings;
    std::unique_lock lock(mutex_);const auto [node,inserted]=maps_.try_emplace(key,std::move(canonical));return finish(node->second,pair);
}
size_t EscherPlacementMapCache::size()const{std::shared_lock lock(mutex_);return maps_.size();}
void EscherPlacementMapCache::clear(){std::unique_lock lock(mutex_);maps_.clear();}
void escherBindBoundaryPlacementCache(EscherBoundaryLengthServices &services,EscherPlacementMapCache &cache){
    services.placementMap=[&cache](size_t,const EscherPatternRecord &record,const ConvolutionIntegerPolygon &sheet,bool cacheOnly){
        const auto *shape=reinterpret_cast<const EscherIntegerShapeRecord *>(record.geometry);require(shape!=nullptr,PN2S_INVALID,"Escher length record has no shape owner");
        return cache.query({&shape->normal.normalContours(),reinterpret_cast<uintptr_t>(&shape->normal),uint8_t(record.reflected&255),record.angle,{}},
            {&sheet,reinterpret_cast<uintptr_t>(&sheet),0,0,{}},cacheOnly);
    };
}
}
