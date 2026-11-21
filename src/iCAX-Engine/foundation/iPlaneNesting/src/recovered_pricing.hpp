#pragma once
#include "recovered_chris6.hpp"
#include "recovered_offset.hpp"
#include "recovered_placement.hpp"
#include <map>

namespace study::recovered {
// Deprecated PN2_PricedNest's integer prices belong to SDK part records +18.
// They are independent of the SDK shape +2b8 / native Part +220 value.
struct PricedSdkPart {
    size_t nativeIndex = 0;
    int32_t price = 1;
};
bool pricedEntryValid(std::span<const PricedSdkPart>); // 664c18..664c39
std::optional<UserEvaluation> pricedProblem(size_t nativePartCount,
                                          std::span<const PricedSdkPart>);
struct PricedShapeSdkPart {
    uintptr_t shapeIdentity = 0;
    std::vector<PlacementOrientation> orientations;
    int32_t price = 1;
};
struct PricedShapeGroup {
    uintptr_t shapeIdentity = 0;
    int32_t quantity = 0;
    std::vector<PlacementOrientation> orientations;
    double profit = 0;
};
// Ordinary no-predefined producer: all SDK records with the same shape ID
// accumulate quantity and union orientations in one native Part. Their last
// record supplies its UserEvaluation profit, even when poses differ.
std::vector<PricedShapeGroup> pricedShapeGroups(std::span<const PricedShapeSdkPart>);
struct PricedEntryPolicy {
    uint64_t initialPreset = 100000, reversePreset = 0;
    double seconds = 0;
    double usePostop9b0 = 0, usePostop9b8 = 0;
    bool flag8 = true;
};
// a00 is converted to signed int32 by CVTTSD2SI before the positive test.
PricedEntryPolicy pricedEntryPolicy(double requestedSeconds, double configA00);
double sdkShapePriceDefault(); // 59386b..59387c
double nativePartInitialValue(double area58, bool marked); // 382ef4,3834d2
// 5cf129..5cf188. COMISD/JBE sends NaN and nonpositive shape prices through
// the fallback. The marked fallback multiplies geometry +58 by config +8d0.
double nativePartPrice(double initialValue220, double shapePrice2b8,
                       double geometryArea58, bool marked, double config8d0);

struct PricingInformer {
    double area10 = 0, value18 = 0, process20 = 0, edge28 = 0;
    std::map<int32_t, double> valuesByKey70;
    bool stripCache80 = false;
};
struct PricingPlacement {
    ValuePart part;
    double process60 = 0, edgeAdd = 0, edgeRemove = 0;
    int32_t keyEC = 0;
};
// Actual NestingInformer callbacks 3d1c10/3d2050, without the independent
// geometric bound trees or common-cut group-owner bookkeeping.
void pricingInformerAdded(PricingInformer &, const PricingPlacement &);
void pricingInformerRemoved(PricingInformer &, const PricingPlacement &);
}
