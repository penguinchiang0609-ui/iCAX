#include "recovered_rectangle_tiling.hpp"
namespace study::recovered {
RectangleTilingBranch rectangleTilingBranch(const RectangleTilingBranchInput &input) {
    RectangleTilingBranch result;
    if (input.gap <= 1e-9 && input.protectedGeometry) return result;
    const double boxArea = input.height * input.width;
    const bool rectangle = input.forceB0 || boxArea * .95 < input.area;
    const int32_t required = input.optional ? 0 : input.required;
    result.potential = !rectangle;
    if (double(required) * boxArea < input.totalStockArea * .05 && (input.optional || input.required < 8))
        result.potential = false;
    if (!result.potential && !input.forceAll) {
        result.obvious = rectangle;
        result.rectanglePatterns = rectangle && input.minimumParts <= required && !input.contextDisabled;
    } else if (input.minimumParts <= required || input.forceAll) {
        result.general = true;
        const double demandArea = (double(required) * input.height) * input.width;
        result.extensive = input.totalStockArea * .33 < demandArea || input.maximumStockArea * .9 < demandArea || input.forceAll;
    }
    return result;
}
std::vector<std::array<int32_t, 2>> rectangleTilingRepetitionLimits(int32_t x, int32_t y, int32_t total) {
    if (total == 0) return {{x, y}};
    const int32_t half = total / 2;
    return {{2, y > 0 && y < half ? y : half}, {x > 0 && x < half ? x : half, 2}};
}
int32_t gammadionOccupancy(double a, double b, double width, double height) {
    if ((a > width && b > width) || (a > height && b > height)) return 0;
    const int32_t nx = int32_t(std::trunc(width / a)), ny = int32_t(std::trunc(height / b));
    const int32_t base = nx * ny;
    if (!base) return 0;
    const double rx = width - double(nx) * a, ry = height - double(ny) * b;
    const int32_t x = std::max(gammadionOccupancy(a, b, rx, height), gammadionOccupancy(a, b, height, rx));
    const int32_t y = std::max(gammadionOccupancy(a, b, width, ry), gammadionOccupancy(a, b, ry, width));
    return base + std::max(x, y);
}
std::vector<GammadionRatio> gammadionRatios(double shortSide, double longSide, Budget *budget) {
    std::vector<GammadionRatio> result;
    const int32_t limit = int32_t(std::trunc(longSide / shortSide));
    for (int32_t second = 1; second <= limit; ++second)
        for (int32_t first = 1; first <= limit; ++first) {
            if (budget) budget->check();
            const double width = double(second) * shortSide + longSide;
            const double height = double(first) * shortSide + longSide;
            const int32_t grid = std::max(gammadionOccupancy(shortSide, longSide, width, height),
                                         gammadionOccupancy(shortSide, longSide, height, width));
            const int32_t composite = (first + second) * 2;
            if (grid < composite) result.push_back({first, second, double(grid) / double(composite)});
        }
    return result;
}
void gammadionSortRatios(std::vector<GammadionRatio> &result) {
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.score != b.score) return a.score < b.score;
        if (a.second != b.second) return a.second < b.second;
        return a.first < b.first;
    });
    if (result.size() > 30) result.resize(30);
}
GammadionCounts gammadionSheetFit(double width, double height, double longSide, double shortSide,
                                 int32_t quantity, Budget *budget) {
    int32_t nx = int32_t(std::trunc(height / shortSide));
    if (shortSide <= height) width = std::min(double(quantity / nx) * longSide, width);
    if (longSide <= height) width = std::min(double(quantity / int32_t(std::trunc(height / longSide))) * shortSide, width);
    nx = std::min({nx, int32_t(144), quantity});
    const int32_t ny = std::min({int32_t(std::trunc(width / shortSide)), int32_t(144), quantity});
    GammadionCounts result{}; int32_t best = 0;
    for (int32_t a = 1; a <= nx; ++a)
        for (int32_t b = 1; b <= nx; ++b)
            for (int32_t c = 1; c <= ny; ++c)
                for (int32_t d = 1; d <= ny; ++d) {
                    if (budget) budget->check();
                    if (height > double(a + b) * shortSide || width > double(c + d) * shortSide) continue;
                    const int32_t x = int32_t(std::trunc((height - double(a) * shortSide) / longSide));
                    const int32_t y = int32_t(std::trunc((width - double(d) * shortSide) / longSide));
                    const int32_t z = int32_t(std::trunc((height - double(b) * shortSide) / longSide));
                    const int32_t w = int32_t(std::trunc((width - double(c) * shortSide) / longSide));
                    const int32_t total = x * d + w * a + z * c + y * b;
                    if (total > best) { best = total; result = {w, c, z, b, y, d, x, a}; }
                }
    return result;
}
GammadionPlan gammadionPlan(const GammadionCounts &n, double longer, double shorter, Point first, Point second) {
    GammadionPlan result;
    for (int32_t i = 0; i < n[0]; ++i)
        for (int32_t j = 0; j < n[7]; ++j)
            result.members.push_back({false, {double(i) * shorter - first.x, double(j) * longer - first.y}});
    for (int32_t i = 0; i < n[6]; ++i)
        for (int32_t j = 0; j < n[5]; ++j)
            result.members.push_back({true, {double(j) * longer - second.x,
                (double(n[7]) * longer + double(i) * shorter) - second.y}});
    for (int32_t i = 0; i < n[4]; ++i)
        for (int32_t j = 0; j < n[3]; ++j)
            result.members.push_back({false, {(double(n[5]) * longer + double(i) * shorter) - first.x,
                (double(n[2]) * shorter + double(j) * longer) - first.y}});
    for (int32_t i = 0; i < n[2]; ++i)
        for (int32_t j = 0; j < n[1]; ++j)
            result.members.push_back({true, {(double(n[0]) * shorter + double(j) * longer) - second.x,
                double(i) * shorter - second.y}});
    auto counts = n;
    for (const auto pair : {std::array{0, 7}, std::array{6, 5}, std::array{4, 3}, std::array{2, 1}})
        if (counts[pair[0]] * counts[pair[1]] == 0) counts[pair[0]] = counts[pair[1]] = 0;
    result.u = {std::max(double(counts[5]) * longer + double(counts[4]) * shorter,
                         double(counts[0]) * shorter + double(counts[1]) * longer), 0.};
    result.v = {0., std::max(double(counts[3]) * longer + double(counts[2]) * shorter,
                             double(counts[7]) * longer + double(counts[6]) * shorter)};
    return result;
}
}
