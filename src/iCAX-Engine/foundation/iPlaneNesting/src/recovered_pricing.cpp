#include "recovered_pricing.hpp"
#include <immintrin.h>

namespace study::recovered {
bool pricedEntryValid(std::span<const PricedSdkPart> parts) {
    if (parts.empty()) return false;
    for (const auto &part : parts) if (part.price <= 0) return false;
    return true;
}
std::optional<UserEvaluation> pricedProblem(size_t nativePartCount,
                                          std::span<const PricedSdkPart> parts) {
    if (!pricedEntryValid(parts)) return {};
    std::vector<UserEvaluationPart> copied;
    copied.reserve(parts.size());
    for (const auto &part : parts) copied.push_back({part.nativeIndex, double(part.price)});
    return userEvaluation(nativePartCount, copied);
}
std::vector<PricedShapeGroup> pricedShapeGroups(std::span<const PricedShapeSdkPart> records) {
    std::map<uintptr_t, PricedShapeGroup> groups;
    for (const auto &record : records) {
        if (record.price <= 0) throw std::invalid_argument("nonpositive SDK integer price");
        auto &group = groups[record.shapeIdentity];
        group.shapeIdentity = record.shapeIdentity;
        if (group.quantity == INT32_MAX) throw std::length_error("SDK shape quantity exceeds int32");
        ++group.quantity;
        for (const auto &orientation : record.orientations) insertOrientation(group.orientations, orientation);
        group.profit = double(record.price);
    }
    std::vector<PricedShapeGroup> result;
    result.reserve(groups.size());
    for (auto &[identity, group] : groups) result.push_back(std::move(group));
    return result;
}
PricedEntryPolicy pricedEntryPolicy(double seconds, double configA00) {
    PricedEntryPolicy result;
    const auto value = _mm_cvttsd_si32(_mm_set_sd(configA00));
    result.seconds = value > 0 ? double(value) : seconds;
    return result;
}
double sdkShapePriceDefault() { return -1.; }
double nativePartInitialValue(double area, bool marked) { return marked ? area * .001 : -1.; }
double nativePartPrice(double initial, double price, double area, bool marked, double multiplier) {
    if (price > 0.) return price;
    return marked ? area * multiplier : initial;
}
void pricingInformerAdded(PricingInformer &state, const PricingPlacement &p) {
    state.area10 = (p.part.optional ? 0. : p.part.area) + state.area10;
    state.edge28 = p.edgeAdd + state.edge28;
    state.process20 = (p.part.optional ? 0. : p.process60) + state.process20;
    const auto value = requiredValue(p.part);
    state.value18 = value + state.value18;
    state.valuesByKey70[p.keyEC] = value + state.valuesByKey70[p.keyEC];
    state.stripCache80 = false;
}
void pricingInformerRemoved(PricingInformer &state, const PricingPlacement &p) {
    state.area10 = state.area10 - (p.part.optional ? 0. : p.part.area);
    state.process20 = state.process20 - (p.part.optional ? 0. : p.process60);
    state.edge28 = state.edge28 - p.edgeRemove;
    const auto value = requiredValue(p.part);
    state.value18 = state.value18 - value;
    state.valuesByKey70[p.keyEC] = state.valuesByKey70[p.keyEC] - value;
    state.stripCache80 = false;
}
}
