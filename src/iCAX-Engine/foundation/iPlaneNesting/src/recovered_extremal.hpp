#pragma once
#include "core.hpp"
#include <optional>
#include <functional>
#include <memory>
#include <span>
namespace study::recovered {
using StripPoint=std::array<int64_t,2>;
struct ExtremalInput {
    int32_t first=0,end=0; // Exclusive end, positionner +20/+24.
    int32_t count=4,maxSteps=1000000000,supplementEnd=0;
    bool lower=true,fromLow=true;
};
// 3d9d00, unwrapped map path, including 3dbf60/3dbb00 and 3da510/3da900.
// Rows contain signed 32-bit endpoints; rank arithmetic wraps at 32 bits.
std::vector<StripPoint> extremalPoints(const Rows &,const ExtremalInput &);
struct EvaluatedStripMask {
    int32_t first=0,end=0,evaluatedUntil=INT32_MAX;
    std::vector<uint32_t> accepted,keepRow;
};
struct IntervalConstraint {
    bool optional24=false;
    std::function<void(uintptr_t sheet,Row &,uintptr_t geometry)> filter;
};
// 38d110: ordered shared constraints, optional filtering, stop on empty.
void filterConstraintIntervals(std::span<const std::shared_ptr<IntervalConstraint>>,uintptr_t sheet,Row &,uintptr_t geometry,bool includeOptional);
struct StripConstraintCache {
    EvaluatedStripMask mask;
    int32_t first18=0,limit1c=0,batch24=0;
    int64_t pitch=1,originX=0,moduleLeft=0;
    uintptr_t sheet=0,geometry=0;
    std::vector<std::shared_ptr<IntervalConstraint>> constraints;
};
struct StripIntervalResult {Row intervals;bool unchanged=false;};
StripIntervalResult buildStripIntervals(const StripConstraintCache &); // 3e3030
void refreshStripMask(StripConstraintCache &,int32_t requestedEnd); // 3e3270
bool acceptsStrip(StripConstraintCache &,int32_t strip); // 3e2cc0
struct MovingVerticalConstraint {
    std::function<void(uintptr_t sheet,Row &,uintptr_t geometry,const std::array<int64_t,2>&)> filter;
};
struct VerticalConstraintCache {
    Row baseIntervals;
    int32_t yOffset=0;
    int64_t pitch=1,originX=0,moduleLeft=0;
    uintptr_t sheet=0,geometry=0;
    std::vector<std::shared_ptr<MovingVerticalConstraint>> constraints;
};
// 3e2920: translate vertical intervals, apply strip-dependent filters, undo
// translation, then intersect the candidate row. Inputs are original int32 Y.
Row refineVerticalConstraints(const VerticalConstraintCache &,int32_t strip,const Row &);
struct EvaluatedMapConstraints {
    const EvaluatedStripMask *horizontal=nullptr;
    const Row *vertical=nullptr;
    StripConstraintCache *lazyHorizontal=nullptr;
    const VerticalConstraintCache *movingVertical=nullptr;
};
// 3e2e80/398f20/3e3320. Builds both masks from closed X intervals. Resizing
// preserves existing words, including the original OR/update semantics.
void buildStripMask(EvaluatedStripMask &,const Row &xIntervals,int64_t pitch,int64_t originX,int64_t moduleLeft);
// 3db640/3db700/3db8a0/3db960 with evaluated or lazy strip masks and fixed or
// moving vertical intervals. Concrete geometry constraint families are explicit
// callbacks. Pre-evaluated-only masks reject requests needing a refresh.
int32_t constrainedEndpoint(const Rows &,int32_t strip,bool lower,const EvaluatedMapConstraints &);
// 3da360/3da750 -> 3dc1c0/3dbd60. Wrapped scans do not run supplements.
std::vector<StripPoint> constrainedExtremalPoints(const Rows &,const ExtremalInput &,const EvaluatedMapConstraints &);
struct FirstPlacementInput {
    double width=0,height=0;
    std::optional<Box> sheetBounds; // Evaluated provider 39cf20, when present.
    Box partBounds{}; // Virtual bbox slot 10, queried twice by the original.
    int64_t pitch=1,originX=0;
    int32_t originY=0;
};
// 3d8f60. Caller enforces empty candidate list, zero placed count and no
// instance constraints (3d8d60); this routine only performs the fallback math.
std::optional<StripPoint> firstPlacement(const FirstPlacementInput &);
}
