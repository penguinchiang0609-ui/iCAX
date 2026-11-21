#include "recovered_placement.hpp"
#include <bit>
#include <immintrin.h>
namespace study::recovered {
namespace {
constexpr int32_t fullTurn=36000000;
int32_t wrapAdd(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t wrapSub(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int32_t wrappedAbs(int32_t a){return a<0?wrapSub(0,a):a;}
int32_t orientationMagnitude(int32_t a){a=wrappedAbs(a);return a>=fullTurn?wrapSub(a,fullTurn):a;}
int32_t positiveReduction(int32_t a){while(a>=fullTurn)a=wrapSub(a,fullTurn);return a;}
void limitOrientationRange(PlacementOrientation &orientation) {
    if (wrapAdd(orientation.field4, orientation.field8) > 36001000)
        orientation.field8 = wrapSub(36000500, orientation.field4);
}
int32_t quantizeOrientationDegrees(double value) {
    const double scaled = value * 100000.;
    return _mm_cvttsd_si32(_mm_set_sd(scaled < 0 ? scaled - .5 : scaled + .5));
}
PlacementOrientation reflectedEquivalent(PlacementOrientation source, int32_t axis) {
    const auto rotation = std::bit_cast<int32_t>(uint32_t(axis) - 2u * uint32_t(source.angle)) % fullTurn;
    auto result = composeOrientation(source, rotation, true);
    result.field4 = source.field4;
    result.field8 = source.field8;
    limitOrientationRange(result);
    return result;
}
}
PlacementOrientation quantizeSdkOrientation(SdkOrientation source, bool includeZero) {
    const auto first = quantizeOrientationDegrees(source.minimum);
    auto angle = first % fullTurn;
    if (source.reflected)
        angle = angle == 0 ? -fullTurn : wrapSub(0, angle);
    double maximum = source.maximum;
    if (source.minimum != maximum && includeZero && maximum > 359.8)
        maximum = 360.;
    const auto magnitude = orientationMagnitude(angle);
    const auto remaining = wrapSub(quantizeOrientationDegrees(maximum), magnitude);
    PlacementOrientation result{angle, remaining, 0};
    if (source.reflected) {
        result.field4 = wrapSub(magnitude, first);
        result.field8 = remaining;
    }
    limitOrientationRange(result);
    return result;
}
void appendSdkOrientationInterval(std::vector<PlacementOrientation> &orientations, SdkOrientation source,
                                  double anchor, bool includeZero) {
    auto result = quantizeSdkOrientation({source.reflected, anchor, anchor}, includeZero);
    double maximum = source.maximum;
    if (source.minimum != maximum && includeZero && maximum > 359.8)
        maximum = 360.;
    const auto before = wrapSub(quantizeOrientationDegrees(anchor), quantizeOrientationDegrees(source.minimum));
    const auto after = wrapSub(quantizeOrientationDegrees(maximum), quantizeOrientationDegrees(anchor));
    result.field4 = source.reflected ? before : after;
    result.field8 = source.reflected ? after : before;
    limitOrientationRange(result);
    insertOrientation(orientations, result);
}
void mergeZeroOrientationIntervals(std::vector<PlacementOrientation> &orientations, bool reflected) {
    PlacementOrientation combined{reflected ? -fullTurn : 0, 0, 0};
    const bool containsZero = orientationContainsRange(combined, orientations);
    std::vector<int32_t> remove;
    for (const auto &candidate : orientations) {
        if ((candidate.angle < 0) != reflected)
            continue;
        const auto angle = orientationMagnitude(candidate.angle);
        auto ranges = candidate;
        if (angle != 0) {
            if (!containsZero || angle <= 35980000 || (candidate.field4 == 0 && candidate.field8 == 0))
                continue;
            const auto delta = wrapSub(fullTurn, angle);
            ranges.field4 = reflected ? wrapAdd(candidate.field4, delta) : wrapSub(candidate.field4, delta);
            ranges.field8 = reflected ? wrapSub(candidate.field8, delta) : wrapAdd(candidate.field8, delta);
        }
        remove.push_back(candidate.angle);
        combined.field4 = std::max(combined.field4, ranges.field4);
        combined.field8 = std::max(combined.field8, ranges.field8);
        limitOrientationRange(combined);
    }
    if (remove.empty())
        return;
    for (const auto angle : remove) {
        const auto found = std::lower_bound(orientations.begin(), orientations.end(), angle,
            [](const auto &candidate, int32_t key) { return orientationLess(candidate.angle, key); });
        if (found != orientations.end() && found->angle == angle)
            orientations.erase(found);
    }
    insertOrientation(orientations, combined);
    const auto lower = wrapSub(0, reflected ? combined.field4 : combined.field8);
    const auto upper = reflected ? combined.field8 : combined.field4;
    std::vector<PlacementOrientation> result;
    for (auto candidate : orientations) {
        if ((candidate.angle < 0) == reflected && candidate.angle != combined.angle) {
            const auto angle = orientationMagnitude(candidate.angle);
            int32_t before = 0, after = 0;
            if (angle >= lower && angle <= upper) {
                before = wrapSub(angle, lower);
                after = wrapSub(upper, angle);
            } else if (angle >= wrapAdd(lower, fullTurn) && angle <= wrapAdd(upper, fullTurn)) {
                before = wrapSub(angle, wrapAdd(lower, fullTurn));
                after = wrapSub(wrapAdd(upper, fullTurn), angle);
            } else {
                insertOrientation(result, candidate);
                continue;
            }
            candidate.field4 = std::max(candidate.field4, reflected ? before : after);
            candidate.field8 = std::max(candidate.field8, reflected ? after : before);
            limitOrientationRange(candidate);
        }
        insertOrientation(result, candidate);
    }
    orientations = std::move(result);
}
std::vector<PlacementOrientation> sdkOrientationCatalog(std::span<const SdkOrientation> source) {
    std::array<bool, 2> containsZero{};
    for (const auto &orientation : source)
        containsZero[orientation.reflected != 0] = containsZero[orientation.reflected != 0] || orientation.minimum == 0;
    std::vector<PlacementOrientation> result;
    for (const auto &orientation : source)
        appendSdkOrientationInterval(result, orientation,
            orientation.maximum <= 359. ? orientation.minimum : orientation.maximum,
            containsZero[orientation.reflected != 0]);
    mergeZeroOrientationIntervals(result, false);
    mergeZeroOrientationIntervals(result, true);
    return result;
}
PlacementOrientation composeOrientation(PlacementOrientation source,int32_t rotation,bool reflected) {
    rotation=positiveReduction(rotation);
    const auto magnitude=orientationMagnitude(source.angle);
    int32_t angle=positiveReduction(wrapAdd(wrapAdd(magnitude,fullTurn),rotation));
    if(source.angle<0)angle=wrapSub(wrapAdd(magnitude,fullTurn),rotation);
    angle=positiveReduction(angle);
    if((source.angle<0)!=reflected)angle=angle==0?-fullTurn:wrapSub(0,angle);
    PlacementOrientation out{angle,reflected?source.field8:source.field4,reflected?source.field4:source.field8};
    if(wrapAdd(out.field4,out.field8)>36001000)out.field8=wrapSub(36000500,out.field4);
    return out;
}
bool orientationLess(int32_t first, int32_t second) {
    return second != 0 && (first == 0 || first < second);
}
bool insertOrientation(std::vector<PlacementOrientation> &orientations, PlacementOrientation value) {
    const auto found = std::lower_bound(orientations.begin(), orientations.end(), value.angle,
        [](const auto &candidate, int32_t angle) { return orientationLess(candidate.angle, angle); });
    if (found == orientations.end() || found->angle != value.angle) {
        orientations.insert(found, value);
        return true;
    }
    if (found->field4 < value.field4 || found->field8 < value.field8) {
        found->field4 = std::max(found->field4, value.field4);
        found->field8 = std::max(found->field8, value.field8);
        limitOrientationRange(*found);
    }
    return false;
}
PlacementOrientation canonicalOrientation(PlacementOrientation source, bool symmetryX, bool symmetryY) {
    auto result = source;
    const auto consider = [&](PlacementOrientation candidate) {
        if (orientationLess(candidate.angle, result.angle))
            result = candidate;
    };
    if (symmetryX)
        consider(reflectedEquivalent(source, 72000000));
    if (symmetryY) {
        consider(reflectedEquivalent(source, 90000000));
        if (symmetryX)
            consider(composeOrientation(source, 18000000, false));
    }
    return result;
}
std::vector<PlacementOrientation> equivalentOrientations(PlacementOrientation source,
                                                         bool symmetryX, bool symmetryY) {
    std::vector<PlacementOrientation> result;
    insertOrientation(result, source);
    if (symmetryX)
        insertOrientation(result, reflectedEquivalent(source, 72000000));
    if (symmetryY) {
        insertOrientation(result, reflectedEquivalent(source, 90000000));
        if (symmetryX)
            insertOrientation(result, composeOrientation(source, 18000000, false));
    }
    return result;
}
std::vector<std::array<int32_t, 2>> orientationIntervals(PlacementOrientation orientation,
                                                       const std::function<void(bool)> &diagnostic) {
    const auto angle = orientationMagnitude(orientation.angle);
    if (diagnostic)
        diagnostic(angle >= 0 && angle <= fullTurn);
    const auto lower = wrapSub(angle, orientation.angle < 0 ? orientation.field4 : orientation.field8);
    const auto upper = wrapAdd(angle, orientation.angle < 0 ? orientation.field8 : orientation.field4);
    if (lower < 0)
        return {{wrapAdd(lower, fullTurn), wrapAdd(upper, fullTurn)}, {lower, upper}};
    if (upper > fullTurn)
        return {{lower, upper}, {wrapSub(lower, fullTurn), wrapSub(upper, fullTurn)}};
    return {{lower, upper}};
}
std::optional<PlacementOrientation> matchOrientationInterval(std::array<int32_t, 2> interval,
    std::span<const PlacementOrientation> equivalents, bool reflected) {
    for (const auto &candidate : equivalents) {
        const auto angle = orientationMagnitude(candidate.angle);
        if ((candidate.angle < 0) != reflected || angle < interval[0] || angle > interval[1])
            continue;
        auto result = candidate;
        result.field4 = reflected ? wrapSub(angle, interval[0]) : wrapSub(interval[1], angle);
        result.field8 = reflected ? wrapSub(interval[1], angle) : wrapSub(angle, interval[0]);
        limitOrientationRange(result);
        return result;
    }
    return {};
}
PlacementOrientationMatch equivalentOrientation(std::span<const PlacementOrientation> orientations,
    PlacementOrientation requested, bool symmetryX, bool symmetryY,
    const std::function<void(bool)> &diagnostic) {
    const auto exact = std::lower_bound(orientations.begin(), orientations.end(), requested.angle,
        [](const auto &candidate, int32_t angle) { return orientationLess(candidate.angle, angle); });
    if (exact != orientations.end() && exact->angle == requested.angle)
        return {*exact, true};
    const auto canonical = canonicalOrientation(requested, symmetryX, symmetryY);
    for (const auto &candidate : orientations)
        if (canonicalOrientation(candidate, symmetryX, symmetryY).angle == canonical.angle)
            return {candidate, true};
    const auto equivalents = equivalentOrientations(requested, symmetryX, symmetryY);
    for (const auto &candidate : orientations)
        for (const auto &interval : orientationIntervals(candidate, diagnostic))
            if (auto matched = matchOrientationInterval(interval, equivalents, candidate.angle < 0))
                return {*matched, true};
    return {requested, false};
}
std::vector<PlacementOrientation> placementOrientationList(std::span<const PlacementOrientation> shape,
    std::span<const PartOrientationFilter> partConstraints,
    std::span<const NestingOrientationFilter> nestingConstraints,
    uintptr_t nesting, uint8_t symmetryX, uint8_t symmetryY) {
    std::vector<PlacementOrientation> result(shape.begin(), shape.end());
    for (const auto &constraint : partConstraints)
        result = constraint.apply(result, symmetryX, symmetryY);
    for (const auto &constraint : nestingConstraints)
        result = constraint.apply(nesting, result, symmetryX, symmetryY);
    return result;
}
bool placementRotationLocked(std::span<const PartOrientationFilter> partConstraints,
                             std::span<const NestingOrientationFilter> nestingConstraints) {
    for (const auto &constraint : nestingConstraints)
        if (constraint.rotationLock != 0)
            return true;
    for (const auto &constraint : partConstraints)
        if (constraint.rotationLock != 0)
            return true;
    return false;
}
int32_t orientationTilt(PlacementOrientation orientation) {
    return std::min(fullTurn, wrapAdd(orientation.field4, orientation.field8));
}
bool orientationHasTilt(std::span<const PlacementOrientation> orientations) {
    return std::any_of(orientations.begin(), orientations.end(),
        [](const auto &orientation) { return orientationTilt(orientation) > 0; });
}
std::vector<PlacementOrientation> canonicalOrientationList(std::span<const PlacementOrientation> orientations,
                                                          bool symmetryX, bool symmetryY) {
    std::vector<PlacementOrientation> result;
    for (const auto &orientation : orientations)
        insertOrientation(result, canonicalOrientation(orientation, symmetryX, symmetryY));
    return result;
}
std::vector<PlacementOrientation> expandOrientationRanges(std::span<const PlacementOrientation> orientations,
                                                         bool symmetryX, bool symmetryY) {
    std::vector<PlacementOrientation> result;
    for (const auto &orientation : orientations) {
        insertOrientation(result, canonicalOrientation(orientation, symmetryX, symmetryY));
        const auto angle = orientationMagnitude(orientation.angle);
        const bool reflected = orientation.angle < 0;
        const auto backward = std::min(angle, reflected ? orientation.field4 : orientation.field8);
        const auto forwardLimit = wrapSub(std::max(35999000, wrapAdd(angle, fullTurn) / 2), angle);
        const auto forward = std::min(forwardLimit, reflected ? orientation.field8 : orientation.field4);
        const auto shifted = [&](int32_t delta) {
            auto value = orientation;
            value.angle = wrapAdd(angle, delta);
            if (reflected)
                value.angle = value.angle == 0 ? -fullTurn : wrapSub(0, value.angle);
            const auto signedDelta = reflected ? wrapSub(0, delta) : delta;
            value.field4 = wrapSub(value.field4, signedDelta);
            value.field8 = wrapAdd(value.field8, signedDelta);
            return canonicalOrientation(value, symmetryX, symmetryY);
        };
        insertOrientation(result, shifted(forward));
        insertOrientation(result, shifted(wrapSub(0, backward)));
    }
    return result;
}
bool orientationContainsRange(PlacementOrientation requested,
                              std::span<const PlacementOrientation> orientations) {
    const auto angle = orientationMagnitude(requested.angle);
    for (const auto &candidate : orientations) {
        if ((candidate.angle < 0) != (requested.angle < 0))
            continue;
        const auto center = orientationMagnitude(candidate.angle);
        const auto lower = wrapSub(center, candidate.angle < 0 ? candidate.field4 : candidate.field8);
        const auto upper = wrapAdd(center, candidate.angle < 0 ? candidate.field8 : candidate.field4);
        for (const int32_t shift : {-fullTurn, 0, fullTurn}) {
            const auto shifted = wrapAdd(angle, shift);
            if (shifted >= lower && shifted <= upper)
                return true;
        }
    }
    return false;
}
bool orientationConstraintContains(PlacementOrientation requested,
    std::span<const PlacementOrientation> orientations, bool symmetryX, bool symmetryY, bool allowTilt) {
    requested = canonicalOrientation(requested, symmetryX, symmetryY);
    std::vector<PlacementOrientation> canonical;
    if (symmetryX || symmetryY) {
        canonical = canonicalOrientationList(orientations, symmetryX, symmetryY);
        orientations = canonical;
    }
    const auto found = std::lower_bound(orientations.begin(), orientations.end(), requested.angle,
        [](const auto &candidate, int32_t angle) { return orientationLess(candidate.angle, angle); });
    return (found != orientations.end() && found->angle == requested.angle) ||
           (allowTilt && orientationContainsRange(requested, orientations));
}
bool mandatoryOrientationContains(PlacementOrientation requested,
    std::span<const PlacementOrientation> orientations, bool symmetryX, bool symmetryY, bool hasTilt) {
    return orientationConstraintContains(requested, orientations, symmetryX, symmetryY,
                                         hasTilt || orientationTilt(requested) > 0);
}
bool relativeOrientationContains(PlacementOrientation sheet, PlacementOrientation part,
    std::span<const PlacementOrientation> orientations, bool symmetryX, bool symmetryY) {
    if (sheet.angle != 0) {
        const auto inverse = sheet.angle < 0 ? sheet.angle :
            wrapSub(fullTurn, orientationMagnitude(sheet.angle)) % fullTurn;
        part = composeOrientation(part, orientationMagnitude(inverse), inverse < 0);
    }
    return orientationConstraintContains(part, orientations, symmetryX, symmetryY, true);
}
std::vector<std::array<int32_t, 2>> splitOrientationRange(PlacementOrientation orientation) {
    const auto angle = orientationMagnitude(orientation.angle);
    auto lower = wrapSub(angle, orientation.angle < 0 ? orientation.field4 : orientation.field8);
    auto upper = wrapAdd(angle, orientation.angle < 0 ? orientation.field8 : orientation.field4);
    const double offset = -std::floor((double(lower) / 100000.) / 360.) * 360. * 100000.;
    const auto shift = _mm_cvttsd_si32(_mm_set_sd(offset < 0 ? offset - .5 : offset + .5));
    lower = wrapAdd(lower, shift);
    upper = wrapAdd(upper, shift);
    if (lower < 0) {
        lower = wrapAdd(lower, fullTurn);
        upper = wrapAdd(upper, fullTurn);
    }
    if (lower > fullTurn) {
        lower = wrapSub(lower, fullTurn);
        upper = wrapSub(upper, fullTurn);
    }
    if (upper > fullTurn)
        return {{lower, fullTurn}, {0, wrapSub(upper, fullTurn)}};
    return {{lower, upper}};
}
namespace {
PlacementOrientation toggleOrientationReflection(PlacementOrientation orientation) {
    const auto magnitude = orientationMagnitude(orientation.angle);
    return {orientation.angle < 0 ? magnitude : (magnitude == 0 ? -fullTurn : wrapSub(0, magnitude)),
            orientation.field8, orientation.field4};
}
std::vector<PlacementOrientation> intersectPositiveOrientationRanges(
    std::span<const PlacementOrientation> first, std::span<const PlacementOrientation> second) {
    std::array<std::vector<std::array<int32_t, 2>>, 2> intervals;
    const std::array sources{first, second};
    for (size_t side = 0; side < sources.size(); ++side) {
        for (const auto &orientation : sources[side]) {
            auto pieces = splitOrientationRange(orientation);
            intervals[side].insert(intervals[side].end(), pieces.begin(), pieces.end());
        }
        std::sort(intervals[side].begin(), intervals[side].end());
    }
    std::vector<PlacementOrientation> result;
    size_t left = 0, right = 0;
    while (left < intervals[0].size() && right < intervals[1].size()) {
        const auto a = intervals[0][left], b = intervals[1][right];
        if (a[0] > b[1]) {
            ++right;
            continue;
        }
        if (b[0] <= a[1]) {
            const auto lower = std::max(a[0], b[0]), upper = std::min(a[1], b[1]);
            const auto middle = wrapAdd(lower, upper) / 2;
            insertOrientation(result, {middle, wrapSub(upper, middle), wrapSub(middle, lower)});
            if (b[1] < a[1]) {
                ++right;
                continue;
            }
        }
        ++left;
    }
    return result;
}
}
std::vector<PlacementOrientation> intersectOrientationRanges(std::span<const PlacementOrientation> first,
                                                            std::span<const PlacementOrientation> second) {
    std::array<std::array<std::vector<PlacementOrientation>, 2>, 2> separated;
    const std::array sources{first, second};
    for (size_t side = 0; side < sources.size(); ++side) {
        std::vector<PlacementOrientation> ordered(sources[side].begin(), sources[side].end());
        size_t front = 0, back = ordered.size();
        while (front < back) {
            if (ordered[front].angle < 0) {
                ++front;
                continue;
            }
            do {
                --back;
            } while (front < back && ordered[back].angle >= 0);
            if (front < back) {
                std::swap(ordered[front], ordered[back]);
                ++front;
            }
        }
        for (const auto &orientation : ordered) {
            const bool reflected = orientation.angle < 0;
            insertOrientation(separated[side][reflected],
                              reflected ? toggleOrientationReflection(orientation) : orientation);
        }
    }
    auto result = intersectPositiveOrientationRanges(separated[0][0], separated[1][0]);
    for (const auto &orientation : intersectPositiveOrientationRanges(separated[0][1], separated[1][1]))
        insertOrientation(result, toggleOrientationReflection(orientation));
    return result;
}
std::vector<PlacementOrientation> intersectOrientations(std::span<const PlacementOrientation> first,
    std::span<const PlacementOrientation> second, bool symmetryX, bool symmetryY, bool allowTilt) {
    std::vector<PlacementOrientation> canonical;
    if (symmetryX || symmetryY) {
        canonical = canonicalOrientationList(second, symmetryX, symmetryY);
        second = canonical;
    }
    if (allowTilt) {
        const auto expandedSecond = expandOrientationRanges(second, symmetryX, symmetryY);
        const auto expandedFirst = expandOrientationRanges(first, symmetryX, symmetryY);
        return canonicalOrientationList(intersectOrientationRanges(expandedSecond, expandedFirst), symmetryX, symmetryY);
    }
    std::vector<PlacementOrientation> result;
    size_t left = 0, right = 0;
    while (left < first.size() && right < second.size()) {
        if (first[left].angle == second[right].angle) {
            insertOrientation(result, second[right]);
            ++left;
            ++right;
        } else if (orientationLess(second[right].angle, first[left].angle)) {
            ++right;
        } else {
            ++left;
        }
    }
    return result;
}
std::vector<PlacementOrientation> mandatoryOrientationList(std::span<const PlacementOrientation> input,
    std::span<const PlacementOrientation> constraint, bool symmetryX, bool symmetryY, bool hasTilt) {
    return intersectOrientations(input, constraint, symmetryX, symmetryY, hasTilt || orientationHasTilt(input));
}
std::vector<PlacementOrientation> relativeOrientationList(PlacementOrientation sheet,
    std::span<const PlacementOrientation> input, std::span<const PlacementOrientation> constraint,
    bool symmetryX, bool symmetryY) {
    std::vector<PlacementOrientation> transformed;
    if (sheet.angle != 0) {
        for (const auto &orientation : constraint)
            insertOrientation(transformed, composeOrientation(orientation, orientationMagnitude(sheet.angle), sheet.angle < 0));
        constraint = transformed;
    }
    return intersectOrientations(input, constraint, symmetryX, symmetryY, true);
}
bool relativeOrientationAllowed(PlacementOrientation sheet,PlacementOrientation part,
    std::span<const PlacementOrientation> relative,bool hasOwner) {
    if(relative.empty() || !hasOwner)return true;
    bool accepted=false;
    for(const auto &constraint:relative) {
        const auto transformed=composeOrientation(constraint,orientationMagnitude(sheet.angle),sheet.angle<0);
        const auto inverse=part.angle<0?part:PlacementOrientation{wrapSub(fullTurn,orientationMagnitude(part.angle))%fullTurn,0,0};
        const auto result=composeOrientation(inverse,orientationMagnitude(transformed.angle),transformed.angle<0);
        // The original uses a signed, strict < 10000 comparison, not <=.
        if(wrappedAbs(result.angle)<10000)accepted=true;
    }
    return accepted;
}
bool acceptsOrientation(std::span<const OrientationAdmission> partConstraints,std::span<const NestingOrientationAdmission> nestingConstraints,
    uintptr_t nesting,const PlacementOrientation &orientation,uint8_t field1e0,uint8_t field1e1,bool includeOptional) {
    for(const auto &constraint:partConstraints) {
        if(!includeOptional) {
            if(!constraint.optional)throw std::invalid_argument("missing orientation optional predicate");
            if(constraint.optional())continue;
        }
        if(!constraint.accepts)throw std::invalid_argument("missing orientation admission predicate");
        if(!constraint.accepts(orientation,field1e0,field1e1))return false;
    }
    if(nesting)for(const auto &constraint:nestingConstraints) {
        if(!constraint)throw std::invalid_argument("missing nesting orientation predicate");
        if(!constraint(nesting,orientation,field1e0,field1e1))return false;
    }
    return true;
}
PlacementStateCatalog::PlacementStateCatalog(std::span<const size_t> instanceGroups)
    :groups(instanceGroups.begin(),instanceGroups.end()),positions(groups.size()),states(groups.size(),0) {
    size_t count=0;for(auto group:groups)count=std::max(count,group+1);
    for(auto &state:buckets)state.resize(count);
    for(size_t i=0;i<groups.size();++i){auto &bucket=buckets[0][groups[i]];positions[i]=bucket.size();bucket.push_back(i);}
}
void PlacementStateCatalog::transition(size_t instance,int32_t from,int32_t to) {
    if(from<0 || from>2 || to<0 || to>2 || states.at(instance)!=from)throw std::invalid_argument("invalid placement catalog transition");
    auto &source=buckets[from].at(groups[instance]);auto &target=buckets[to].at(groups[instance]);
    const auto slot=positions[instance];
    if(slot>=source.size() || source[slot]!=instance)throw std::logic_error("invalid placement catalog slot");
    source[slot]=source.back();positions[source[slot]]=slot;source.pop_back();
    target.push_back(instance);positions[instance]=target.size()-1;states[instance]=to;
}
PlacementLifecycle::PlacementLifecycle(WarmNesting &nesting,std::span<const size_t> instanceGroups)
    :nesting_(nesting),catalog(instanceGroups),instances(instanceGroups.size()) {}
bool PlacementLifecycle::commit(const WarmPlacementPtr &placement,int32_t index) {
    if(!placement || placement->instance18<0)throw std::invalid_argument("invalid placement instance");
    const auto id=size_t(placement->instance18);auto &instance=instances.at(id);
    const auto report=[&](PlacementDiagnostic d){if(diagnostic)diagnostic(d,*placement);};
    if(catalog.states[id]!=0){report(PlacementDiagnostic::NonFreeInstance);return false;}
    if(validateOnCommit) {
        if(!sheetConstraint || !instanceConstraint)throw std::logic_error("placement constraints require host providers");
        // The original calls BOTH checks even if the first one fails, and
        // diagnoses failure without suppressing the subsequent insertion.
        const bool sheet=sheetConstraint(*placement,0.0001);
        const bool part=instanceConstraint(*placement);
        if(!(sheet & part))report(PlacementDiagnostic::ConstraintViolation);
    }
    const auto at=index<0?nesting_.placements.size():size_t(index);
    if(at>nesting_.placements.size())throw std::out_of_range("placement insertion index");
    nesting_.placedCount13d8=std::bit_cast<int32_t>(uint32_t(nesting_.placedCount13d8)+1U);
    nesting_.placements.insert(nesting_.placements.begin()+at,placement);
    instance.owner08=&nesting_;placement->group08=reinterpret_cast<uintptr_t>(&nesting_);
    for(const auto &observer:observers)if(observer.added)observer.added(placement);
    if(!instance.current00.expired())report(PlacementDiagnostic::ExistingPlacement);
    catalog.transition(id,0,1);instance.current00=placement;
    for(const auto &observer:instance.observers)if(observer.added)observer.added(placement);
    return true;
}
void PlacementLifecycle::clearInstance(size_t id) {
    auto &instance=instances.at(id);
    catalog.transition(id,1,0);
    // Keep this owner alive throughout callbacks, even when the nesting has
    // already erased its reference. Expired inputs are outside the original
    // valid domain (the original throws bad_weak_ptr at this boundary).
    const auto placement=instance.current00.lock();if(!placement)throw std::bad_weak_ptr();
    instance.current00.reset();
    for(const auto &observer:instance.observers)if(observer.removed)observer.removed(placement);
    instance.owner08=nullptr;placement->group08=0;
}
bool PlacementLifecycle::remove(const WarmPlacementPtr &placement) {
    if(!placement)return false;
    // Own the argument independently of a possibly invalidated vector slot.
    const auto retained=placement;
    const auto it=std::find(nesting_.placements.rbegin(),nesting_.placements.rend(),retained);
    if(it==nesting_.placements.rend())return false;
    const auto index=nesting_.placements.size()-1-size_t(it-nesting_.placements.rbegin());
    for(const auto &observer:observers)if(observer.removed)observer.removed(retained);
    nesting_.placedCount13d8=std::bit_cast<int32_t>(uint32_t(nesting_.placedCount13d8)-1U);
    nesting_.placements.erase(nesting_.placements.begin()+index);
    clearInstance(size_t(retained->instance18));return true;
}
void PlacementLifecycle::removeTrailing() {
    while(!nesting_.placements.empty())if(!remove(nesting_.placements.back()))break;
}
}
