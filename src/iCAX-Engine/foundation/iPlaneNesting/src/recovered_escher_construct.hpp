#pragma once
#include "recovered_escher_owner.hpp"
#include <map>
#include <mutex>

namespace study::recovered {
struct EscherIntegerPartInput {
    uint32_t shapeId = 0;
    uint64_t quantity = 0;
    bool metadata0 = false;
    int32_t metadata4 = 0;
    double protection = 0;
    ConvolutionPolygon exteriors, holes;
};
struct EscherIntegerSheetInput {
    uint32_t shapeId = 0;
    uint64_t quantity = 0;
    ConvolutionPolygon contours;
};
struct EscherIntegerSheetRecord {
    ConvolutionIntegerPolygon contours;
    ShakerPoint reference{};
    uint64_t quantity = 0;
    uint32_t shapeId = 0;
};
class EscherIntegerJobCache {
    mutable std::mutex mutex_;
    std::map<std::pair<double,double>,std::map<uint32_t,EscherIntegerShapeRecord>> buckets_;
public:
    std::optional<EscherIntegerShapeRecord> find(double scale,double protection,uint32_t shapeId); //7f9ec0
    void store(double scale,double protection,const EscherIntegerShapeRecord &);
    size_t bucketCount() const;
};
struct EscherIntegerConstructOptions {
    bool ceilProtection = true;
    EscherIntegerJobCache *cache = nullptr;
};
struct EscherIntegerJob {
    std::vector<EscherIntegerShapeRecord> parts;
    std::vector<EscherIntegerSheetRecord> sheets;
    double scale = 0;
    double maximumProtection = 0;
};
ConvolutionIntegerPolygon escherNormalizeIntegerSheet(const ConvolutionIntegerPolygon &); //7f9280
// Typed 7fa900 assembly. Exterior/positive-hole separation and registered-ID /
// eligible-instance production belong to 568170, before this constructor.
EscherIntegerJob escherConstructIntegerJob(std::span<const EscherIntegerPartInput>,
    std::span<const EscherIntegerSheetInput>,const EscherIntegerConstructOptions & = {});
}
