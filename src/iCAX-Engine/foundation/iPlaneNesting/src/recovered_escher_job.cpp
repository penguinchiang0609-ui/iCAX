#include "recovered_escher_job.hpp"
#include "recovered_convolution_hierarchy.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>

namespace study::recovered {
namespace {
void cleanup(ConvolutionIntegerPolygon &rings) {
    for (auto &ring : rings) convolutionNormalizeIntegerRing(ring);
    convolutionRemoveEmptyIntegerRings(rings);
}
void append(ConvolutionIntegerPolygon &to, const ConvolutionIntegerPolygon &from) {
    to.insert(to.end(), from.begin(), from.end());
}
void reverse(ConvolutionIntegerRing &ring) { std::reverse(ring.begin(), ring.end()); }
}
void escherNormalizeIntegerContours(ConvolutionIntegerPolygon &exteriors,
    ConvolutionIntegerPolygon &holes, double hullAreaTolerance) {
    for (auto &ring : exteriors) {
        if (convolutionIntegerArea(ring) < 0) reverse(ring);
        escherRepairIntegerRing(ring);
        auto sorted = ring;
        const int orientation = convolutionIntegerOrientation(sorted);
        std::sort(sorted.begin(), sorted.end());
        auto hull = escherIntegerHull(sorted);
        if (orientation == 1) reverse(hull);
        const double tolerance = std::abs(double(convolutionIntegerArea(hull))) * hullAreaTolerance;
        convolutionNormalizeIntegerRing(ring);
        escherSimplifyIntegerRing(ring, tolerance);
        convolutionNormalizeIntegerRing(ring);
    }
    for (size_t index = 0; index < holes.size();) {
        if (!escherRepairIntegerRing(holes[index])) holes.erase(holes.begin() + index);
        else {convolutionNormalizeIntegerRing(holes[index]);++index;}
    }
    while (escherIntegerRingsNeedUnion(holes)) escherUnionIntegerRings(holes);
    if (holes.empty())
        while (escherIntegerRingsNeedUnion(exteriors)) escherUnionIntegerRings(exteriors);
    auto combined = exteriors;
    append(combined, holes);
    if (escherIntegerRingsNeedUnion(combined)) {
        std::sort(exteriors.begin(), exteriors.end(), [](const auto &a, const auto &b) {
            return convolutionIntegerArea(a) < convolutionIntegerArea(b);
        });
        const auto selected = exteriors.back();
        ConvolutionIntegerPolygon remaining(exteriors.begin(), exteriors.end() - 1);
        escherUnionIntegerRings(holes);
        const auto difference = escherDifferenceIntegerRings({selected}, holes);
        ConvolutionIntegerPolygon nextExteriors, nextHoles, islands;
        for (auto ring : difference) {
            if (convolutionIntegerArea(ring) < 0) {
                reverse(ring);
                for (auto part : escherDifferenceIntegerRings({ring}, remaining)) {
                    if (convolutionIntegerArea(part) < 1) {reverse(part);islands.push_back(std::move(part));}
                    else nextHoles.push_back(std::move(part));
                }
            } else nextExteriors.push_back(std::move(ring));
        }
        append(nextExteriors, remaining);
        escherUnionIntegerRings(nextExteriors);
        append(nextExteriors, islands);
        exteriors = std::move(nextExteriors);
        holes = std::move(nextHoles);
    }
    cleanup(exteriors);cleanup(holes);
    bool repeated = false;
    for (const auto *rings : {&exteriors, &holes})
        for (const auto &ring : *rings) repeated |= escherIntegerRingHasRepeatedPoint(ring);
    if (!repeated) {
        std::vector<ConvolutionIntegerRing *> rings;
        for (auto *group : {&exteriors, &holes})
            for (auto &ring : *group) rings.push_back(&ring);
        escherResolveSharedIntegerVertex(rings);
    }
}
ConvolutionIntegerPolygon escherNormalizeIntegerShape(ConvolutionIntegerPolygon exteriors,
    ConvolutionIntegerPolygon holes, int32_t maximumHoles) {
    const uint64_t limit = uint64_t(int64_t(maximumHoles));
    if (uint64_t(holes.size()) > limit) {
        std::vector<std::pair<int64_t,size_t>> order;
        order.reserve(holes.size());
        for (size_t index=0;index<holes.size();++index) {
            const int64_t area = convolutionIntegerArea(holes[index]);
            const int64_t negativeMagnitude = area > 0 ? std::bit_cast<int64_t>(uint64_t(0)-uint64_t(area)) : area;
            order.emplace_back(negativeMagnitude,index);
        }
        std::sort(order.begin(),order.end());
        ConvolutionIntegerPolygon selected;
        for (size_t index=0;index<size_t(limit);++index) selected.push_back(holes[order[index].second]);
        holes = std::move(selected);
    }
    cleanup(exteriors);cleanup(holes);
    escherNormalizeIntegerContours(exteriors,holes);
    for (auto &ring : holes) reverse(ring);
    append(exteriors,holes);
    cleanup(exteriors);
    return exteriors;
}
}
