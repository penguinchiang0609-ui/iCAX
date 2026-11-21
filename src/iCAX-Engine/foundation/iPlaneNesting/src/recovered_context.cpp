#include "recovered_context.hpp"
#include "recovered_strategy.hpp"
#include <bit>
#include <cmath>

namespace study::recovered {
double moduleAverageWidth(std::span<const ContextPartMeasure> parts, int32_t population) {
    if (parts.empty()) return 1.;
    double sum = 0.;
    for (const auto &part : parts) sum = sum + part.width * (part.optional ? 0 : part.required);
    return sum / double(population);
}
OrdinaryContextMeasures ordinaryContextMeasures(std::span<const ContextPartMeasure> parts,
    int32_t population, double totalArea, double sheetWidth, double sheetHeight) {
    OrdinaryContextMeasures result;
    result.width218 = moduleAverageWidth(parts, population);
    if (!parts.empty()) {
        double height = 0., area = 0.;
        for (const auto &part : parts)
            height = height + part.height * (double(part.optional ? 0 : part.required) * part.area);
        for (const auto &part : parts)
            area = area + (double(part.optional ? 0 : part.required) * part.area) * part.area;
        result.height220 = height / totalArea;
        result.area228 = area / totalArea;
    }
    if (result.height220 > 0.) result.batch238 = truncateInt32(std::round(sheetHeight / result.height220));
    if (result.width218 > 0.) {
        const double byArea = totalArea / (result.width218 * sheetHeight), byWidth = sheetWidth / result.width218;
        result.multiplicity23c = truncateInt32(std::round(byArea < byWidth ? byArea : byWidth));
    }
    return result;
}
namespace {
struct BoundarySegment { Point first, second; };
Point closestBoundaryPoint(Point point, const BoundarySegment &segment, bool vertical) {
    const double dx = segment.second.x - segment.first.x;
    const double dy = segment.second.y - segment.first.y;
    // The horizontal path calls 522a00; the vertical path is inlined with
    // the original x/y summation order retained.
    const double dot = vertical
        ? (point.x - segment.first.x) * dx + (point.y - segment.first.y) * dy
        : (point.y - segment.first.y) * dy + (point.x - segment.first.x) * dx;
    if (!(dot > 0.)) return segment.first;
    const double length = dy * dy + dx * dx;
    if (!(dot < length)) return segment.second;
    const double parameter = dot / length;
    return {segment.first.x + parameter * dx, segment.first.y + parameter * dy};
}
double nearestBoundaryDistance(Point point, std::span<const BoundarySegment> segments, bool vertical) {
    double result = INFINITY;
    for (const auto &segment : segments) {
        const auto closest = closestBoundaryPoint(point, segment, vertical);
        const double dx = point.x - closest.x, dy = point.y - closest.y;
        const double squared = (dx * dx + 0.) + dy * dy;
        if (squared < result) result = squared;
    }
    return std::sqrt(result);
}
}
ShapeAxisSymmetry shapeAxisSymmetry(std::span<const Polygon> rings, Box box, double tolerance) {
    std::vector<BoundarySegment> segments;
    for (const auto &ring : rings) {
        if (ring.empty()) continue;
        Point previous = ring.back();
        for (const auto point : ring) { segments.push_back({previous, point}); previous = point; }
    }
    ShapeAxisSymmetry result;
    const double centerX = (box.x0 + box.x1) * .5, centerY = (box.y0 + box.y1) * .5;
    for (const auto &ring : rings) {
        std::vector<Point> samples = ring;
        if (!ring.empty()) {
            Point previous = ring.back();
            for (const auto point : ring) {
                samples.push_back({(previous.x + point.x) * .5, (previous.y + point.y) * .5});
                previous = point;
            }
        }
        for (const auto point : samples) {
            if (result.horizontal) {
                const auto distance = nearestBoundaryDistance({point.x, (centerY + centerY) - point.y}, segments, false);
                if (!(distance <= result.maximumDistance[0])) result.maximumDistance[0] = distance;
                result.horizontal = result.maximumDistance[0] <= tolerance;
            }
            if (result.vertical) {
                const auto distance = nearestBoundaryDistance({(centerX + centerX) - point.x, point.y}, segments, true);
                if (!(distance <= result.maximumDistance[1])) result.maximumDistance[1] = distance;
                result.vertical = result.maximumDistance[1] <= tolerance;
            }
            if (!result.horizontal && !result.vertical) return result;
        }
    }
    return result;
}
std::vector<PlacementOrientation> partOrientationCache(std::span<const SdkOrientation> source,
    bool simplifySameSymmetry, bool horizontalSymmetry, bool verticalSymmetry) {
    auto catalog = sdkOrientationCatalog(source);
    if (simplifySameSymmetry)
        catalog = canonicalOrientationList(catalog, horizontalSymmetry, verticalSymmetry);
    // 551960 indexes the created modules by +190 (the signed angle only).
    std::set<int32_t> keys;
    std::vector<PlacementOrientation> result;
    for (const auto orientation : catalog)
        if (keys.insert(orientation.angle).second) result.push_back(orientation);
    return result;
}
int32_t orientationCachePopulation(std::span<const size_t> perPartCacheSizes) {
    uint32_t count = 0;
    for (const auto size : perPartCacheSizes) count += uint32_t(size);
    return std::bit_cast<int32_t>(count);
}
}
