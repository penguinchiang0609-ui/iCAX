#pragma once
#include "core.hpp"
namespace study::recovered {
using GammadionCounts = std::array<int32_t, 8>;
struct GammadionRatio { int32_t first = 0, second = 0; double score = 0; };
struct GammadionMember { bool second = false; Point translation{}; };
struct GammadionPlan { std::vector<GammadionMember> members; Point u{}, v{}; };
struct RectangleTilingBranchInput {
    double gap = 0., width = 0., height = 0., area = 0., totalStockArea = 0., maximumStockArea = 0.;
    int32_t required = 0, minimumParts = 10;
    bool protectedGeometry = false, optional = false, forceB0 = false, forceAll = false, contextDisabled = false;
};
struct RectangleTilingBranch {
    bool potential = false, obvious = false, rectanglePatterns = false, general = false, extensive = false;
};
RectangleTilingBranch rectangleTilingBranch(const RectangleTilingBranchInput &); //49da40 / 498580
std::vector<std::array<int32_t, 2>> rectangleTilingRepetitionLimits(int32_t maximumX, int32_t maximumY,
                                                                int32_t maximumTotal); //497030
int32_t gammadionOccupancy(double shortSide, double longSide, double width, double height); //49ebf0
std::vector<GammadionRatio> gammadionRatios(double shortSide, double longSide, Budget * = nullptr); //49ed40
void gammadionSortRatios(std::vector<GammadionRatio> &); //4a2a90 + 4a2910
GammadionCounts gammadionSheetFit(double width, double height, double longSide, double shortSide,
                                 int32_t quantity, Budget * = nullptr); //49ef20
GammadionPlan gammadionPlan(const GammadionCounts &, double longSide, double shortSide,
                            Point firstMinimum, Point secondMinimum); //49f180
}
