#include "recovered_escher_owner.hpp"
#include <mutex>
#include <map>
#include <set>
#include <utility>

namespace study::recovered {
namespace {
template<class T> class OwnerCache {
    mutable std::mutex mutex_;
    mutable std::optional<T> value_;
public:
    OwnerCache() = default;
    OwnerCache(const OwnerCache &source) {
        const std::scoped_lock lock(source.mutex_); value_ = source.value_;
    }
    template<class Factory> const T &get(Factory factory) const {
        const std::scoped_lock lock(mutex_);
        if (!value_) value_.emplace(factory());
        return *value_;
    }
    bool ready() const { const std::scoped_lock lock(mutex_); return value_.has_value(); }
};
}
struct EscherIntegerShapeOwner::Caches {
    OwnerCache<EscherSupportTable> support;
    OwnerCache<ConvolutionHierarchy> hierarchy;
    OwnerCache<ConvolutionIntegerPolygon> outer;
};
EscherIntegerShapeOwner::EscherIntegerShapeOwner(EscherNormalizedContours contours)
    : contours_(std::move(contours.rings)), caches_(std::make_unique<Caches>()) {}
EscherIntegerShapeOwner::~EscherIntegerShapeOwner() = default;
EscherIntegerShapeOwner::EscherIntegerShapeOwner(const EscherIntegerShapeOwner &source)
    : contours_(source.contours_), caches_(source.caches_ ? std::make_unique<Caches>(*source.caches_) : std::make_unique<Caches>()) {}
EscherIntegerShapeOwner &EscherIntegerShapeOwner::operator=(const EscherIntegerShapeOwner &source) {
    if (this != &source) {
        EscherIntegerShapeOwner copy(source); contours_.swap(copy.contours_); caches_.swap(copy.caches_);
    }
    return *this;
}
EscherIntegerShapeOwner::EscherIntegerShapeOwner(EscherIntegerShapeOwner &&) noexcept = default;
EscherIntegerShapeOwner &EscherIntegerShapeOwner::operator=(EscherIntegerShapeOwner &&) noexcept = default;
std::array<bool, 3> EscherIntegerShapeOwner::cacheState() const {
    return caches_ ? std::array{caches_->support.ready(), caches_->hierarchy.ready(), caches_->outer.ready()} : std::array<bool, 3>{};
}
const ConvolutionHierarchy &EscherIntegerShapeOwner::hierarchy() const {
    require(caches_ != nullptr && !contours_.empty(), PN2S_INVALID, "Escher shape hierarchy requires normalized nonempty contours");
    return caches_->hierarchy.get([&] { return convolutionHierarchyFromRings(contours_, false); });
}
ConvolutionIntegerPolygon escherHierarchyOuterContours(const ConvolutionHierarchy &hierarchy) {
    if (hierarchy.ring) return {*hierarchy.ring};
    ConvolutionIntegerPolygon result;
    for (const auto &child : hierarchy.children) {
        require(child && child->ring, PN2S_INVALID, "Escher hierarchy root must have a contour");
        result.push_back(*child->ring);
    }
    return result;
}
const ConvolutionIntegerPolygon &EscherIntegerShapeOwner::outerContours() const {
    require(caches_ != nullptr, PN2S_INVALID, "Moved-from Escher shape owner");
    return caches_->outer.get([&] { return escherHierarchyOuterContours(hierarchy()); });
}
const EscherSupportTable &EscherIntegerShapeOwner::support() const {
    require(caches_ != nullptr, PN2S_INVALID, "Moved-from Escher shape owner");
    return caches_->support.get([&] {
        std::vector<EscherIntegerPoint> points;
        for (const auto &ring : outerContours()) points.insert(points.end(), ring.begin(), ring.end());
        require(points.size() >= 2, PN2S_INVALID, "Escher support requires normalized nonempty contours");
        return escherBuildSupportTable(std::move(points));
    });
}
std::vector<EscherIntegerShapeIdentity> escherIntegerShapeIdentities(std::span<const uint32_t> registeredIds,
    std::span<const uint32_t> geometryIds, std::span<const uint32_t> instanceIds) {
    const std::set<uint32_t> registered(registeredIds.begin(), registeredIds.end());
    std::map<uint32_t, size_t> indices;
    std::vector<EscherIntegerShapeIdentity> result;
    for (size_t index = 0; index < geometryIds.size(); ++index) if (registered.contains(geometryIds[index])) {
        require(indices.emplace(geometryIds[index], result.size()).second, PN2S_INVALID, "Duplicate Escher geometry shape ID");
        result.push_back({geometryIds[index], 0, index});
    }
    for (const auto id : instanceIds) {
        const auto found = indices.find(id);
        require(found != indices.end(), PN2S_INVALID, "Escher instance has no registered geometry shape ID");
        ++result[found->second].quantity;
    }
    return result;
}
std::array<ConvolutionIntegerPolygon, 2> escherSplitShapeContours(const ConvolutionIntegerPolygon &source) {
    auto rings = source;
    size_t first = 0, last = rings.size();
    while (first != last) {
        if (convolutionIntegerOrientation(rings[first]) == 1) { ++first; continue; }
        do {
            --last;
            if (first == last) goto partitioned;
        } while (convolutionIntegerOrientation(rings[last]) != 1);
        std::swap(rings[first], rings[last]); ++first;
    }
partitioned:
    return {ConvolutionIntegerPolygon(rings.begin(), rings.begin() + first),
            ConvolutionIntegerPolygon(rings.begin() + first, rings.end())};
}
void escherAttachIntegerPart(EscherIntegerPartBinding &binding, uintptr_t context, uint32_t shapeId,
    std::span<const EscherIntegerShapeRecord> primary, std::span<const EscherIntegerShapeRecord> secondary) {
    binding.context = context;
    if (!context) { binding.primary = nullptr; return; }
    for (const auto &record : primary) if (record.shapeId == shapeId) {
        if (record.quantity) binding.primary = &record;
        break;
    }
    binding.secondary = nullptr;
    for (const auto &record : secondary) if (record.shapeId == shapeId) {
        if (record.quantity) binding.secondary = &record;
        break;
    }
}
EscherRecordGeometry escherOwnerRecordGeometry(const EscherIntegerShapeRecord &record) {
    const auto &intrinsic = record.alternate ? record.alternate->support() : record.normal.support();
    const auto &collision = record.normal.support();
    return {intrinsic, collision};
}
ConvolutionIntegerPolygon escherPositiveShapeContours(const EscherIntegerShapeRecord &record) {
    return escherSplitShapeContours(record.normal.normalContours())[0];
}
EscherPatternRecord escherOwnerPatternRecord(const EscherIntegerShapeRecord &record, uint32_t reflected, int32_t angle) {
    EscherPatternRecord result; result.geometry = reinterpret_cast<uintptr_t>(&record);
    result.reflected = reflected; result.angle = angle;
    const auto geometry = escherOwnerRecordGeometry(record);
    escherUpdateRecordBounds(result, geometry.intrinsic, geometry.collision);
    return result;
}
}
