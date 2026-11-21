#pragma once
#include "core.hpp"
#include "recovered_extremal.hpp"
#include <span>
#include <functional>
#include <optional>
namespace study::recovered {
// Native closed signed-int32 intervals; Row's wider storage does not change
// the original arithmetic. Geometry rasterization remains a separate producer.
bool noMapErodeInterval(const Row &base,Interval footprint,Row &allowed); //3a4e20
bool noMapApplyGeometryRow(const Row &base,const Row &footprint,Row &allowed); //3a5120
struct NoMapFootprint {
    Rows rows;
    std::vector<int32_t> priorityRows; //native +48; producer order is significant
    std::vector<int32_t> scanBoundaries; //native +60; [front,back) scan bounds
    int32_t maximumY=INT32_MIN,minimumY=INT32_MAX; //native +40/+44
};
NoMapFootprint noMapFootprint(Rows); //3a7b20 metadata after native rasterization
struct NoMapRaster {
    NoMapFootprint footprint;
    int64_t offsetX=0;
    int32_t offsetY=0;
};
//3a7e00 modes0/1. mode0 keeps actual minX; mode1 aligns to reference.x.
NoMapRaster noMapRaster(std::span<const Polygon>,Point reference,int64_t pitch,
    bool align,double interpolationWeight=0.);
struct NoMapContourLayers {
    int32_t available=1,maximum=1;
    double layerX=0.,layerY=0.;
    int64_t pitch=1;
    std::function<std::span<const Polygon>(bool slot2,int32_t layer)> geometry;
};
//3a3830: exact module slot2/slot3 choice, layer shifts and optional whole-pitch
//duplicate. This does not infer the Part's raw/alternate geometry producer.
std::vector<Polygon> noMapContours(const NoMapContourLayers &,bool slot2,bool duplicatePitch);
struct NoMapRowState {
    int32_t lastFailure=-1; //native temporary +40
};
// 3a5570. Uses the original sampled maximum-width rejection, priority rows,
// failed-row neighborhood, and alternating low/middle-to-high traversal.
// allowed is an existing native accumulator (normally empty at entry).
// maximumWidths mirrors the base-map observer's lazy per-strip cache: INT_MIN
// means unevaluated and INT_MIN+1 means an empty row. Invalidate after mutation.
bool noMapComputeRow(NoMapRowState &,const NoMapFootprint &,const Rows &base,
    std::vector<int32_t> &maximumWidths,int32_t x,Row &allowed);
struct NoMapEndpointCache {
    bool perStrip=true,perStripKey=true;
    int32_t lastQuery=-1;
    std::vector<int32_t> lower,upper;
    std::vector<uintptr_t> keys;
    std::vector<uint32_t> flags;
    uint64_t flagCount=0;
};
//3a62a0 /3dca10. Cached hits contain the outer envelope, with separate native
//not-evaluated/empty sentinels and exact mode-bit/key matching.
void noMapStoreRow(NoMapEndpointCache &,int32_t,const Row &,bool success,bool mode);
std::optional<Row> noMapCachedRow(const NoMapEndpointCache &,int32_t,uintptr_t key,bool mode);
//3a69c0. Invalidate the candidate range affected by changed physical blocks;
//the mode bits can include the preceding candidate exactly as in the original.
void noMapInvalidate(NoMapEndpointCache &,int32_t &firstPossible,int32_t blockSize,
    int32_t footprintRows,int32_t baseRows,int32_t firstBlock,int32_t endBlock,uint32_t flag);
//3a6ad0/3a6c40 ordinary initial range tightening. Return first/end-1 style
//sentinels and cache the successful endpoint pair as the original does.
int32_t noMapFindFirst(NoMapEndpointCache &,int32_t &firstPossible,const NoMapFootprint &,
    const Rows &,std::vector<int32_t> &widthCache,int32_t first,int32_t end);
int32_t noMapFindLast(NoMapEndpointCache &,int32_t firstPossible,const NoMapFootprint &,
    const Rows &,std::vector<int32_t> &widthCache,int32_t first,int32_t end);
struct NoMapRange {int32_t first=0,end=0;};
struct NoMapRangeOptions {
    int32_t first=0,end=-1;
    bool requestRightToLeft=false,enableRightToLeft=false,nestingMode14b0=false;
};
//3dc470 ordinary rectangular-sheet branch with no horizontal/vertical/type5
//constraints. The real row engines above tighten both requested boundaries.
NoMapRange noMapUnconstrainedRange(NoMapEndpointCache &,int32_t &firstPossible,
    const NoMapFootprint &,const Rows &,std::vector<int32_t> &widthCache,NoMapRangeOptions);
// 3dcda0's ordinary unwrapped branch visits exactly one boundary strip.
std::vector<StripPoint> noMapExtremalPoints(const Rows &allowed,const ExtremalInput &);
//3e3620 ->3e3680/3e38d0. Same strict projection/bound control as map scans,
//using NoMap rows and native integer bounds. Duplicate points retain first.
std::vector<StripPoint> noMapDirectionPoints(const Rows &,NoMapRange,int64_t pitch,
    std::span<const Point> directions,int32_t lowerY,int32_t upperY);
}
