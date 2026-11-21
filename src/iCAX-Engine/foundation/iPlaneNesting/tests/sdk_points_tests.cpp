#include "recovered_sdk_points.hpp"
#include <cmath>
#include <iostream>

using namespace study::recovered;
namespace {
size_t checks = 0;
void check(bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); }
void rejects(std::vector<ShakerPoint> points, int32_t code, bool exterior, double mode) {
    try { sdkNormalizePoints(points, exterior, mode); }
    catch (const SdkPointError &error) { check(error.code == code, "SDK point error code"); return; }
    throw std::runtime_error("SDK points unexpectedly accepted invalid contour");
}
}
int main() {
    try {
        std::vector<ShakerPoint> rectangle{{0., 0.}, {0., 5.}, {10., 5.}, {10., 0.}, {5., 0.}, {0., 0.}};
        sdkNormalizePoints(rectangle, true);
        check(rectangle.size() == 4, "Normalization removes redundant closure and collinear vertices");
        check(sdkPointsSignedArea(rectangle) == 50., "Normalization preserves material area and positive outer direction");
        check(sdkPointSelfIntersections(rectangle).empty(), "Normalized rectangle is simple");
        auto hole = rectangle;
        sdkNormalizePoints(hole, false);
        check(sdkPointsSignedArea(hole) == -50., "Hole normalization uses negative winding");
        const std::vector<ShakerPoint> crossing{{0., 0.}, {5., 5.}, {0., 3.}, {4., 0.}};
        rejects(crossing, -979, true, 1.);
        auto unchecked = crossing;
        sdkNormalizePoints(unchecked, true, 0.);
        check(!sdkPointSelfIntersections(unchecked).empty(), "Disabled geometry checking preserves original self-intersection behavior");
        std::vector<ShakerPoint> tinyLoop{{0., 0.}, {10., 0.}, {10., 10.}, {0., 10.}, {0., 0.}, {-.01, -.01}, {-.02, 0.}};
        sdkNormalizePoints(tinyLoop, true);
        check(sdkPointSelfIntersections(tinyLoop).empty(), "Repair returns an actual simple contour");
        check(std::abs(sdkPointsSignedArea(tinyLoop) - 100.) < 1e-10, "Repair retains the large usable loop");
        std::vector<ShakerPoint> collapsedHole{{0., 0.}, {.1, 0.}, {0., .001}};
        sdkNormalizePoints(collapsedHole, false);
        check(std::abs(sdkPointsSignedArea(collapsedHole)) < .0001, "Small hole follows the nonfatal SDK degeneracy branch");
        rejects({{0., 0.}, {.1, 0.}, {0., .001}}, -996, true, 2.);
        rejects({{1e13, 0.}, {1e13, 1.}, {0., 0.}}, -996, true, 2.);
        rejects({{0., 0.}, {1., 0.}}, -996, false, 0.);
        const std::array<double, 2> cutoff{100., 1.}, above{100., std::nextafter(1., INFINITY)};
        check(sdkSelectDominantPointLoop(cutoff, .01) == 0, "Dominant-loop area cutoff is strict");
        check(!sdkSelectDominantPointLoop(above, .01), "Two significant loops remain ambiguous");
        const auto loops = sdkSplitPointLoops(crossing);
        check(loops.size() == 2, "Crossing segments split into two real closed loops");
        for (const auto &loop : loops) check(loop.closed && loop.points.size() == loop.segments.size(), "Split loop retains complete segment topology");
        std::cout << "SDK point normalization, actual repair, strict checks and original errors: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
