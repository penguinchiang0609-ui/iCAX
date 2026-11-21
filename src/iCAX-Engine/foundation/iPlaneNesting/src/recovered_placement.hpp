#pragma once
#include "recovered_warm.hpp"
namespace study::recovered {
// Original 12-byte orientation record. Negative angle encodes reflection;
// reflected zero is -36000000. The two remaining fields are kept verbatim.
struct PlacementOrientation {
    int32_t angle=0,field4=0,field8=0;
    auto operator<=>(const PlacementOrientation &)const=default;
};
struct SdkOrientation {
    int32_t reflected = 0;
    double minimum = 0, maximum = 0;
};
PlacementOrientation quantizeSdkOrientation(SdkOrientation, bool includeZero); // 526ca0
void appendSdkOrientationInterval(std::vector<PlacementOrientation> &, SdkOrientation,
                                  double anchor, bool includeZero); // 527050
void mergeZeroOrientationIntervals(std::vector<PlacementOrientation> &, bool reflected); // 374960 / 375260
std::vector<PlacementOrientation> sdkOrientationCatalog(std::span<const SdkOrientation>); // 527730
PlacementOrientation composeOrientation(PlacementOrientation,int32_t rotation,bool reflected); // 372a30
bool orientationLess(int32_t first, int32_t second); // 368d00 / 527bd0, zero precedes signed values
bool insertOrientation(std::vector<PlacementOrientation> &, PlacementOrientation); // 368d00
PlacementOrientation canonicalOrientation(PlacementOrientation, bool symmetryX, bool symmetryY); // 372fc0
std::vector<PlacementOrientation> equivalentOrientations(PlacementOrientation, bool symmetryX, bool symmetryY); // 373490
std::vector<std::array<int32_t, 2>> orientationIntervals(PlacementOrientation,
    const std::function<void(bool)> &diagnostic = {}); // 527860, ordered wrapped intervals
std::optional<PlacementOrientation> matchOrientationInterval(std::array<int32_t, 2>,
    std::span<const PlacementOrientation>, bool reflected); // 5279e0
struct PlacementOrientationMatch {
    PlacementOrientation orientation;
    bool found = false;
};
PlacementOrientationMatch equivalentOrientation(std::span<const PlacementOrientation>,
    PlacementOrientation, bool symmetryX, bool symmetryY,
    const std::function<void(bool)> &diagnostic = {}); // 527bd0
struct PartOrientationFilter {
    uint64_t rotationLock = 0;
    std::function<std::vector<PlacementOrientation>(std::span<const PlacementOrientation>, uint8_t, uint8_t)> apply;
};
struct NestingOrientationFilter {
    uint64_t rotationLock = 0;
    std::function<std::vector<PlacementOrientation>(uintptr_t, std::span<const PlacementOrientation>, uint8_t, uint8_t)> apply;
};
// 384bd0 / 38a170 / 38a380. Inputs follow the constraint trees' iteration order.
std::vector<PlacementOrientation> placementOrientationList(std::span<const PlacementOrientation>,
    std::span<const PartOrientationFilter>, std::span<const NestingOrientationFilter>,
    uintptr_t nesting, uint8_t symmetryX, uint8_t symmetryY);
bool placementRotationLocked(std::span<const PartOrientationFilter>,
                             std::span<const NestingOrientationFilter>); // 38c650
int32_t orientationTilt(PlacementOrientation); // 372cf0
bool orientationHasTilt(std::span<const PlacementOrientation>); // 527f80
std::vector<PlacementOrientation> canonicalOrientationList(std::span<const PlacementOrientation>,
                                                          bool symmetryX, bool symmetryY); // 373b20
std::vector<PlacementOrientation> expandOrientationRanges(std::span<const PlacementOrientation>,
                                                         bool symmetryX, bool symmetryY); // 52a9f0
bool orientationContainsRange(PlacementOrientation, std::span<const PlacementOrientation>); // 527ff0
bool orientationConstraintContains(PlacementOrientation, std::span<const PlacementOrientation>,
                                   bool symmetryX, bool symmetryY, bool allowTilt); // 4334d0
bool mandatoryOrientationContains(PlacementOrientation, std::span<const PlacementOrientation>,
                                 bool symmetryX, bool symmetryY, bool hasTilt); // 4342a0
bool relativeOrientationContains(PlacementOrientation sheet, PlacementOrientation part,
    std::span<const PlacementOrientation>, bool symmetryX, bool symmetryY); // 433b90
std::vector<std::array<int32_t, 2>> splitOrientationRange(PlacementOrientation); // 5292e0
std::vector<PlacementOrientation> intersectOrientationRanges(std::span<const PlacementOrientation>,
                                                            std::span<const PlacementOrientation>); // 52a0f0
std::vector<PlacementOrientation> intersectOrientations(std::span<const PlacementOrientation>,
    std::span<const PlacementOrientation>, bool symmetryX, bool symmetryY, bool allowTilt); // 52b100
std::vector<PlacementOrientation> mandatoryOrientationList(std::span<const PlacementOrientation>,
    std::span<const PlacementOrientation>, bool symmetryX, bool symmetryY, bool hasTilt); // 434230
std::vector<PlacementOrientation> relativeOrientationList(PlacementOrientation sheet,
    std::span<const PlacementOrientation>, std::span<const PlacementOrientation>,
    bool symmetryX, bool symmetryY); // 4338c0
bool relativeOrientationAllowed(PlacementOrientation sheet,PlacementOrientation part,
    std::span<const PlacementOrientation> relative,bool hasOwner); // 3c4ff0
struct OrientationAdmission {
    std::function<bool()> optional;
    std::function<bool(const PlacementOrientation&,uint8_t,uint8_t)> accepts;
};
using NestingOrientationAdmission=std::function<bool(uintptr_t,const PlacementOrientation&,uint8_t,uint8_t)>;
// 389fd0, tree-order callbacks; concrete constraint families remain providers.
bool acceptsOrientation(std::span<const OrientationAdmission>,std::span<const NestingOrientationAdmission>,
    uintptr_t nesting,const PlacementOrientation &,uint8_t field1e0,uint8_t field1e1,bool includeOptional);
// 3941e0. Three state buckets per part type. Removal swaps with the tail;
// both the swapped instance and moved instance retain their new slot indices.
struct PlacementStateCatalog {
    std::vector<size_t> groups,positions;
    std::vector<int32_t> states;
    std::array<std::vector<std::vector<size_t>>,3> buckets;
    explicit PlacementStateCatalog(std::span<const size_t> instanceGroups);
    void transition(size_t instance,int32_t from,int32_t to);
};
struct PlacementObserver {
    std::function<void(const WarmPlacementPtr&)> added,removed;
};
struct PlacementInstanceState {
    WarmNesting *owner08=nullptr;
    std::weak_ptr<WarmPlacement> current00;
    std::vector<PlacementObserver> observers;
};
enum class PlacementDiagnostic { NonFreeInstance,ConstraintViolation,ExistingPlacement };
// 3c74a0 / 3ca940 / 38c7f0 / 3cab20. Geometry checks and observers are
// explicit host dependencies. This class owns the catalog and instance state,
// while the nesting vector is the strong owner of committed placements.
class PlacementLifecycle {
    WarmNesting &nesting_;
public:
    PlacementStateCatalog catalog;
    std::vector<PlacementInstanceState> instances;
    std::vector<PlacementObserver> observers;
    bool validateOnCommit=true;
    std::function<bool(const WarmPlacement&,double)> sheetConstraint;
    std::function<bool(const WarmPlacement&)> instanceConstraint;
    std::function<void(PlacementDiagnostic,const WarmPlacement&)> diagnostic;
    PlacementLifecycle(WarmNesting &,std::span<const size_t> instanceGroups);
    bool commit(const WarmPlacementPtr &,int32_t index=-1);
    bool remove(const WarmPlacementPtr &);
    void clearInstance(size_t instance);
    void removeTrailing();
};
}
