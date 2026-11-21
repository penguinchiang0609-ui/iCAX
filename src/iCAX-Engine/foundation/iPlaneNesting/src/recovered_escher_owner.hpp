#pragma once
#include "recovered_escher_job.hpp"
#include "recovered_escher_period.hpp"
#include "recovered_convolution_hierarchy.hpp"
#include <memory>
#include <optional>

namespace study::recovered {
// An explicit boundary: construct this only from the completed 7f8490 stage.
// Raw scaled/rounded rings are not normalized job geometry.
struct EscherNormalizedContours { ConvolutionIntegerPolygon rings; };
class EscherIntegerShapeOwner {
    struct Caches;
    ConvolutionIntegerPolygon contours_;
    std::unique_ptr<Caches> caches_;
public:
    explicit EscherIntegerShapeOwner(EscherNormalizedContours = {}); //7f9bc0
    ~EscherIntegerShapeOwner();
    EscherIntegerShapeOwner(const EscherIntegerShapeOwner &); //7fa8a0
    EscherIntegerShapeOwner &operator=(const EscherIntegerShapeOwner &);
    EscherIntegerShapeOwner(EscherIntegerShapeOwner &&) noexcept;
    EscherIntegerShapeOwner &operator=(EscherIntegerShapeOwner &&) noexcept;
    const ConvolutionIntegerPolygon &normalContours() const { return contours_; }
    const ConvolutionHierarchy &hierarchy() const; //7f9cc0 /7fc000
    const ConvolutionIntegerPolygon &outerContours() const; //7f9d50 /7fc490
    const EscherSupportTable &support() const; //7f9ce0 /7fbbe0
    std::array<bool, 3> cacheState() const;
};
ConvolutionIntegerPolygon escherHierarchyOuterContours(const ConvolutionHierarchy &); //824f40
std::array<ConvolutionIntegerPolygon, 2> escherSplitShapeContours(const ConvolutionIntegerPolygon &); //7f9a30
struct EscherIntegerShapeRecord {
    bool metadata0 = false; //raw +0, supplied input bitmap
    int32_t metadata4 = 0; //raw +4, supplied input integer array+78
    uint32_t shapeId = 0; //raw record +14, copied from Part+250
    uint64_t quantity = 0; //raw record +18, actual eligible instance count
    ShakerPoint reference{}; //raw record +20/+28, pre-scale floating center
    EscherIntegerShapeOwner normal; //raw record +30
    std::optional<EscherIntegerShapeOwner> alternate; //raw record +1c8, flag+360
};
struct EscherIntegerShapeIdentity {
    uint32_t shapeId = 0;
    uint64_t quantity = 0;
    size_t geometryIndex = 0;
    bool operator==(const EscherIntegerShapeIdentity &) const = default;
};
std::vector<EscherIntegerShapeIdentity> escherIntegerShapeIdentities(std::span<const uint32_t> registeredIds,
    std::span<const uint32_t> geometryIds, std::span<const uint32_t> instanceIds); //568170 metadata prefix
struct EscherIntegerPartBinding {
    uintptr_t context = 0;
    const EscherIntegerShapeRecord *primary = nullptr, *secondary = nullptr;
};
// 387d40. A null context clears only primary. With a live context a missing
// primary ID retains its previous pointer; a missing secondary ID clears it.
void escherAttachIntegerPart(EscherIntegerPartBinding &, uintptr_t context, uint32_t shapeId,
    std::span<const EscherIntegerShapeRecord> primary, std::span<const EscherIntegerShapeRecord> secondary);
EscherRecordGeometry escherOwnerRecordGeometry(const EscherIntegerShapeRecord &);
ConvolutionIntegerPolygon escherPositiveShapeContours(const EscherIntegerShapeRecord &); //7f9db0
EscherPatternRecord escherOwnerPatternRecord(const EscherIntegerShapeRecord &, uint32_t reflected, int32_t angle);
}
