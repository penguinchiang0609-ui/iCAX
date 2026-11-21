#pragma once
#include "recovered_general_tiling_producer.hpp"
#include "recovered_amo_components.hpp"
#include "recovered_amo_orientation_range.hpp"
namespace study::recovered {
struct GeneralTilingComponent {
    std::vector<Point> points;
    double signedArea=0; // actual component record +48
};
struct GeneralTilingVariant {
    PlacementOrientation orientation; // actual cached variant +190
    std::vector<GeneralTilingComponent> components; // actual base topology index0
    Box bounds{};
};
struct GeneralTilingAmoSheet {
    GeneralTilingSheet sheet;
    size_t contourRecords=0; // actual96-byte geometry record count at+20..28
    bool doubledHeight68=false;
};
Point generalTilingAmoSheetExtents(std::span<const GeneralTilingAmoSheet>); //36b010/36b0c0
struct GeneralTilingAmoSource {
    uintptr_t part=0;
    std::vector<GeneralTilingAmoSheet> sheets; // actual context sheet catalog
    std::vector<PlacementOrientation> orientations;
    bool symmetryX=false,symmetryY=false,optional=false;
    int32_t required=0,maximumX=0,maximumY=0;
    double gap=0,angleStep988=0;
    Box originalBounds{};
    // Same cache service used for the actual source part's canonical variants.
    std::function<std::shared_ptr<const GeneralTilingVariant>(PlacementOrientation)> variant;
    // 54ec10 searches the current context's module cache by canonical angle.
    // Conversion may construct a further variant through 54b3b0; do not use
    // that wider construction service to pretend a current module exists.
    std::function<std::shared_ptr<const GeneralTilingVariant>(PlacementOrientation)> currentVariant;
    std::function<void()> check;
    AmoTilingModuleOrientations moduleOrientations;
};
struct GeneralTilingAmoPreparation {
    AmoTilingJob job;
    AmoTilingSolveRequest request;
    AmoTilingOrientationGeometry conversion;
    int32_t required=0;
};
using AmoTilingInputFinalizer=std::function<AmoTilingTransformedShape(std::span<const Point>,double,bool)>;
// 4a4e00/4990b0/4992a0/492360 through input preparation. The supplied finalizer
// is the actual 491640 ->8b9710 ->8bb590 component geometry operation.
std::optional<GeneralTilingAmoPreparation> generalTilingPrepareOrientationInput(
    const GeneralTilingAmoSource &,PlacementOrientation,PlacementOrientation,
    const AmoTilingInputFinalizer &);
// Runs the actual input geometry, floating Amo solve, conversion and paired sort.
std::vector<AmoTilingPatternResult> generalTilingOrientationPatterns(
    const GeneralTilingAmoSource &,PlacementOrientation,PlacementOrientation);
}
