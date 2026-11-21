#include "recovered_escher_construct.hpp"
#include "recovered_convolution_offset.hpp"
#include "recovered_convolution_integer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace study::recovered {
std::optional<EscherIntegerShapeRecord> EscherIntegerJobCache::find(double scale,double protection,uint32_t shapeId) {
    const std::scoped_lock lock(mutex_);
    const auto &bucket = buckets_[{scale,protection}];
    const auto found = bucket.find(shapeId);
    if (found == bucket.end()) return std::nullopt;
    return found->second;
}
void EscherIntegerJobCache::store(double scale,double protection,const EscherIntegerShapeRecord &record) {
    const std::scoped_lock lock(mutex_);
    buckets_[{scale,protection}][record.shapeId] = record;
}
size_t EscherIntegerJobCache::bucketCount() const {
    const std::scoped_lock lock(mutex_);
    return buckets_.size();
}
ConvolutionIntegerPolygon escherNormalizeIntegerSheet(const ConvolutionIntegerPolygon &input) {
    if (input.size() == 1) return input;
    ConvolutionIntegerPolygon exteriors,holes;
    for (auto ring : input) {
        if (convolutionIntegerOrientation(ring) == 1) holes.push_back(std::move(ring));
        else {
            std::reverse(ring.begin(),ring.end());
            exteriors.push_back(std::move(ring));
        }
    }
    auto result = holes.empty() ? exteriors : escherDifferenceIntegerRings(exteriors,holes);
    for (auto &ring : result) std::reverse(ring.begin(),ring.end());
    return result;
}
EscherIntegerJob escherConstructIntegerJob(std::span<const EscherIntegerPartInput> parts,
    std::span<const EscherIntegerSheetInput> sheets,const EscherIntegerConstructOptions &options) {
    EscherIntegerJob result;
    if (parts.empty()) return result;
    // c52f80 returns the first maximum, using an initial -infinity accumulator.
    size_t maximum = 0;
    double maximumValue = -std::numeric_limits<double>::infinity();
    std::vector<ConvolutionPolygon> exteriors,sheetContours;
    exteriors.reserve(parts.size());sheetContours.reserve(sheets.size());
    for (size_t index=0;index<parts.size();++index) {
        exteriors.push_back(parts[index].exteriors);
        if (maximumValue < parts[index].protection) {
            maximumValue = parts[index].protection;maximum = index;
        }
    }
    result.maximumProtection = parts[maximum].protection;
    for (const auto &sheet : sheets) sheetContours.push_back(sheet.contours);
    const auto raw = escherIntegerGeometry(exteriors,sheetContours,INT32_MAX,result.maximumProtection);
    result.scale = raw.scale;
    const auto convert = [&](const ConvolutionPolygon &input,ShakerPoint reference) {
        ConvolutionIntegerPolygon output;
        output.reserve(input.size());
        for (auto ring : input) {
            for (auto &point : ring) {
                point[0] = (point[0]-reference[0])*result.scale;
                point[1] = (point[1]-reference[1])*result.scale;
            }
            output.push_back(convolutionRoundRing(ring));
        }
        return output;
    };
    result.parts.reserve(parts.size());
    for (size_t index=0;index<parts.size();++index) {
        const auto &part = parts[index];
        EscherIntegerShapeRecord record;
        record.shapeId = part.shapeId;record.quantity = part.quantity;
        record.metadata0 = part.metadata0;record.metadata4 = part.metadata4;
        record.reference = raw.partReferences[index];
        if (options.cache) if (auto cached=options.cache->find(result.scale,result.maximumProtection,record.shapeId)) {
            record.reference = cached->reference;
            record.normal = cached->normal;record.alternate = cached->alternate;
        }
        if (record.normal.normalContours().empty()) {
            auto contours = escherNormalizeIntegerShape(raw.parts[index],convert(part.holes,raw.partReferences[index]));
            record.normal = EscherIntegerShapeOwner(EscherNormalizedContours{std::move(contours)});
        }
        result.parts.push_back(std::move(record));
    }
    result.sheets.reserve(sheets.size());
    for (size_t index=0;index<sheets.size();++index)
        result.sheets.push_back({escherNormalizeIntegerSheet(raw.sheets[index]),raw.sheetReferences[index],sheets[index].quantity,sheets[index].shapeId});
    if (result.maximumProtection > 0.) {
        const double scaled = (result.maximumProtection * result.scale) * .5;
        // cb9090 is ceil; c92c10 is the finite round-away-to-int64 path.
        const double rounded = options.ceilProtection ? std::ceil(scaled) : std::round(scaled);
        const int64_t radius = rounded >= -0x1p63 && rounded < 0x1p63 ? int64_t(rounded) : INT64_MIN;
        for (auto &part : result.parts) if (!part.alternate) {
            auto offset = convolutionIntegerOffset(part.normal.normalContours(),radius,16);
            part.alternate.emplace(EscherNormalizedContours{std::move(offset.rings)});
        }
    }
    // 7fa900 publishes the finished owners after alternate construction, then
    // cleans sheets separately from the 7f9280 normalization helper.
    if (options.cache) for (const auto &part : result.parts)
        options.cache->store(result.scale,result.maximumProtection,part);
    for (auto &sheet : result.sheets) {
        for (auto &ring : sheet.contours) convolutionNormalizeIntegerRing(ring);
        convolutionRemoveEmptyIntegerRings(sheet.contours);
    }
    return result;
}
}
