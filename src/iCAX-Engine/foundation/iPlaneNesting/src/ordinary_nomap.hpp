#pragma once
#include "recovered_nomap.hpp"
#include "recovered_module_geometry.hpp"
#include <memory>
namespace study {
enum class OrdinaryPositionnerKind { Map,NoMap };
struct OrdinaryPositionnerFactoryInput {
    double forcePositionnerType=-1.;
    int32_t target=0,maximumPoints=5000,maximumInwards=200,maximumExteriors=200;
    bool forceNoMap=false,moduleSlot4=false;
    std::function<bool()> mapEligible,geometryTooComplex;
    uint64_t points=0;
    int32_t inwards=0,exteriors=1;
    bool allPlacedModulesCached=true;
    uint64_t moduleBitCount=0;
    std::span<const uint32_t> moduleBits;
    std::span<const int64_t> placedModuleIds;
};
OrdinaryPositionnerKind ordinaryPositionnerKind(const OrdinaryPositionnerFactoryInput &); //3e2150
struct OrdinaryNoMapModule {
    recovered::PreparedPartModuleGeometry geometry;
    int64_t id=0;
};
struct OrdinaryNoMapPlacement {
    size_t module=0;
    int64_t x=0;
    int32_t y=0;
    bool unaligned=false;
};
struct OrdinaryNoMapGrid {
    double width=0.,height=0.;
    int64_t pitch=1,originX=0;
    int32_t originY=0;
    bool perStripCache=true;
};
struct OrdinaryNoMapTrace {
    size_t baseBuilds=0,processedPlacements=0,cacheHits=0,rowCalculations=0,invalidations=0;
};
//3d51b0 ordinary rectangular, single-layer physical slab count (no +1).
Rows ordinaryNoMapBase(OrdinaryNoMapGrid);
//3aa0b0: subtract open interiors of occupied closed intervals. Original signed
//32-bit shifts and point/boundary retention are separate from row erosion.
Row ordinaryNoMapSubtract(const Row &,int32_t baseY,const Row &,int32_t occupiedY);
//NestingStripRepresentation3d55f0/3d60a0 lifecycle for a single-layer stock.
//Inputs are prepared PartModule geometries; this does not normalize raw SDK
//parts. Append is lazy/incremental. Move/remove mark dirty and rebuild on read.
class OrdinaryNoMapProvider {
public:
    OrdinaryNoMapProvider(OrdinaryNoMapGrid,std::span<const OrdinaryNoMapModule>,Budget &);
    ~OrdinaryNoMapProvider();
    OrdinaryNoMapProvider(const OrdinaryNoMapProvider &)=delete;
    OrdinaryNoMapProvider &operator=(const OrdinaryNoMapProvider &)=delete;
    void append(OrdinaryNoMapPlacement);
    void remove(size_t placement);
    void move(size_t placement,OrdinaryNoMapPlacement);
    void reset(std::span<const OrdinaryNoMapPlacement>);
    recovered::NoMapRange range(size_t module,recovered::NoMapRangeOptions={});
    Row row(size_t module,int32_t strip);
    std::vector<recovered::StripPoint> extrema(size_t module,recovered::ExtremalInput);
    std::vector<recovered::StripPoint> directions(size_t module,recovered::NoMapRange,
        std::span<const Point>,int32_t lowerY,int32_t upperY);
    const Rows &freeRows();
    const std::vector<OrdinaryNoMapPlacement> &placements()const;
    const OrdinaryNoMapTrace &trace()const;
    //Builds the exact four3a3c30 variants; used by native producer validation.
    const recovered::NoMapRaster &footprint(size_t module,bool slot2,bool duplicatePitch);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
