#pragma once
#include "recovered_escher_prepared.hpp"
#include <map>
#include <shared_mutex>
namespace study::recovered {
struct EscherPlacementMapKey {
    uintptr_t firstOwner=0,secondOwner=0;
    uint8_t reflection=0;
    int32_t angle=0;
    bool operator==(const EscherPlacementMapKey &)const=default;
};
int escherComparePlacementMapKeys(const EscherPlacementMapKey &,const EscherPlacementMapKey &); //863640
class EscherPlacementMapCache {
    struct Less {bool operator()(const EscherPlacementMapKey &a,const EscherPlacementMapKey &b)const{return escherComparePlacementMapKeys(a,b)<0;}};
    mutable std::shared_mutex mutex_;
    std::map<EscherPlacementMapKey,ConvolutionIntegerPolygon,Less> maps_;
public:
    // Owners and their normalized contours must stay unchanged while cached.
    // Each successful query returns a separate polygon; callers may reverse or translate it.
    std::optional<ConvolutionIntegerPolygon> query(const EscherNfpMember &,const EscherNfpMember &,bool cacheOnly=false); //861f80
    size_t size()const;
    void clear();
};
struct EscherBoundaryLengthServices;
// The length controller always uses the record's normal owner, including when
// an alternate owner exists. Source translation is applied by the controller.
void escherBindBoundaryPlacementCache(EscherBoundaryLengthServices &,EscherPlacementMapCache &);
}
