#include "recovered_escher_normalization.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace study::recovered {
namespace {
int64_t wrap(uint64_t value) { return std::bit_cast<int64_t>(value); }
ConvolutionIntegerPoint delta(ConvolutionIntegerPoint first, ConvolutionIntegerPoint second) {
    return {wrap(uint64_t(first[0]) - uint64_t(second[0])), wrap(uint64_t(first[1]) - uint64_t(second[1]))};
}
int64_t cross(ConvolutionIntegerPoint first, ConvolutionIntegerPoint second) {
    return wrap(uint64_t(first[0]) * uint64_t(second[1]) - uint64_t(first[1]) * uint64_t(second[0]));
}
ConvolutionIntegerPoint primitive(ConvolutionIntegerPoint point) {
    const int64_t divisor = wrap(convolutionIntegerGcd(point));
    if (divisor == 0) throw std::invalid_argument("Escher simplification requires nonzero consecutive edges");
    return {point[0] / divisor, point[1] / divisor};
}
}
void escherRotateIntegerRing(ConvolutionIntegerRing &ring) {
    if (ring.empty()) return;
    const auto first = std::min_element(ring.begin(), ring.end());
    std::rotate(ring.begin(), first, ring.end());
}
ConvolutionIntegerRing escherIntegerHull(std::span<const ConvolutionIntegerPoint> sorted) {
    if (sorted.size() < 2) throw std::invalid_argument("Original Escher hull requires at least two input points");
    ConvolutionIntegerRing upper, lower;
    for (const auto point : sorted) {
        while (upper.size() > 1 && cross(delta(upper.back(), upper[upper.size() - 2]),
               delta(point, upper[upper.size() - 2])) >= 0) upper.pop_back();
        while (lower.size() > 1 && cross(delta(lower.back(), lower[lower.size() - 2]),
               delta(point, lower[lower.size() - 2])) <= 0) lower.pop_back();
        upper.push_back(point); lower.push_back(point);
    }
    for (size_t index = lower.size() - 1; index > 1; --index) upper.push_back(lower[index - 1]);
    return upper;
}
void escherSimplifyIntegerRing(ConvolutionIntegerRing &ring, double allowedArea) {
    if (ring.size() <= 3) return;
    escherRotateIntegerRing(ring);
    const int orientation = convolutionIntegerOrientation(ring);
    ConvolutionIntegerRing result;
    result.reserve(ring.size());
    size_t first = 0;
    while (first < ring.size()) {
        double lostArea = 0.;
        size_t next = first + 1, after = first + 2;
        for (; after < ring.size(); ++after, ++next) {
            const auto firstDirection = primitive(delta(ring[after], ring[next]));
            const auto previousDirection = primitive(delta(ring[next], ring[next - 1]));
            const auto turn = cross(previousDirection, firstDirection);
            if ((turn > 0) - (turn < 0) == orientation) break;
            const auto triangle = cross(delta(ring[next], ring[after]), delta(ring[first], ring[after]));
            const auto absolute = triangle < 0 ? wrap(uint64_t(0) - uint64_t(triangle)) : triangle;
            lostArea = lostArea + double(absolute) * .5;
        }
        // COMISD/JBE takes the keep branch for unordered tolerances too.
        if (!(allowedArea > lostArea)) result.insert(result.end(), ring.begin() + first, ring.begin() + next);
        else result.push_back(ring[first]);
        first = next;
    }
    ring = std::move(result);
}
bool escherIntegerRingsNeedUnion(const ConvolutionIntegerPolygon &rings) {
    std::vector<ConvolutionIntegerSegment> input;
    for (const auto &ring : rings)
        for (size_t index = 0; index < ring.size(); ++index)
            input.push_back({ring[index], ring[(index + 1) % ring.size()]});
    auto split = convolutionSplitDirectedIntegerSegments(input);
    if (split.size() != input.size()) return true;
    for (auto &edge : split) if (edge[1] < edge[0]) std::swap(edge[0], edge[1]);
    std::sort(split.begin(), split.end());
    return std::adjacent_find(split.begin(), split.end()) != split.end();
}
}
