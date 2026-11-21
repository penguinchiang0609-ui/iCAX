#include "recovered_rectangle_groups.hpp"
namespace study::recovered {
bool rectangleUsePrimitiveFallback(const RectangleFallbackInput &input) {
    return input.components7c > 1 || input.optional3c1 || input.required254 < 2 ||
           input.remaining <= 0. || input.specialPreNest;
}
bool rectangleAdmitPrimitiveFallback(const RectangleFallbackInput &input) {
    return input.forceB0 || input.area68 >
        (((input.maximumY - input.minimumY) * (input.maximumX - input.minimumX)) * .95);
}
std::optional<PrimitiveTilingPlan> primitiveTilingPlan(const PrimitiveTilingInput &input) {
    if (!input.variantPresent) return {};
    auto orientation = canonicalOrientation(input.orientation, input.symmetryX, input.symmetryY);
    orientation = canonicalOrientation(orientation, input.symmetryX, input.symmetryY);
    double horizontal = input.gap210, vertical = input.gap210;
    if (input.gap210 <= 1e-9 && input.protectedGeometry200) {
        const double firstX = double(input.right3f8) / 1e6 - input.bounds[2];
        const double secondX = input.bounds[0] - double(input.left3e8) / 1e6;
        horizontal = firstX > secondX ? firstX : secondX;
        const double firstY = double(input.top400) / 1000. - input.bounds[3];
        const double secondY = input.bounds[1] - double(input.bottom3f0) / 1000.;
        vertical = firstY > secondY ? firstY : secondY;
    }
    return PrimitiveTilingPlan{orientation,
        {(input.bounds[2] - input.bounds[0]) + horizontal, 0.},
        {0., (input.bounds[3] - input.bounds[1]) + vertical}, 1};
}
}
