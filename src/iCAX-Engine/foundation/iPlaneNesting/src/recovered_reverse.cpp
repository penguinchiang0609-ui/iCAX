#include "recovered_reverse.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace study::recovered {
namespace {
int32_t add32(int32_t a, int32_t b) { return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b)); }
int64_t add64(int64_t a, int64_t b) { return std::bit_cast<int64_t>(uint64_t(a) + uint64_t(b)); }
int64_t sub64(int64_t a, int64_t b) { return std::bit_cast<int64_t>(uint64_t(a) - uint64_t(b)); }
int64_t truncate64(double value) {
    return std::isfinite(value) && value >= -9223372036854775808. && value < 9223372036854775808.
        ? int64_t(value) : INT64_MIN;
}
int32_t truncate32(double value) {
    return std::isfinite(value) && value >= -2147483648. && value < 2147483648. ? int32_t(value) : INT32_MIN;
}
double roundTerm(double value) { return value < 0. ? value - .5 : value + .5; }
int64_t border(double value) { return truncate64(roundTerm(value * 1e6)); }
}
std::array<double, 6> reverseOrientationMatrix(PlacementOrientation orientation, Point reference) {
    int32_t angle = orientation.angle;
    if (angle < 0) angle = std::bit_cast<int32_t>(0u - uint32_t(angle));
    double radians = 0., cosine = 1., sine = 0.;
    if (angle != 36000000) {
        const auto reduced = angle < 36000000 ? angle : add32(angle, -36000000);
        if (reduced == 18000000) { radians = -pi; cosine = -1.; }
        else if (reduced == 9000000) { radians = pi * .5; cosine = 0.; sine = 1.; }
        else if (reduced == 27000000) { radians = -pi * .5; cosine = 0.; sine = -1.; }
        else if (reduced != 0) {
            radians = ((double(reduced) / 100000.) * pi) / 180.;
            sine = std::sin(radians); cosine = std::cos(radians);
        }
    }
    return {radians, cosine, sine, orientation.angle < 0 ? -1. : 1., reference.x, reference.y};
}
Point reverseVariantOffset(std::array<double, 4> bounds, PlacementOrientation requested,
                            PlacementOrientation actual) {
    if (requested.angle == actual.angle) return {};
    const double y = (bounds[1] + bounds[3]) * .5, x = (bounds[0] + bounds[2]) * .5;
    const auto first = reverseOrientationMatrix(requested), second = reverseOrientationMatrix(actual);
    const double firstX = x - first[4], firstY = y - first[5];
    const double lastY = y - second[5], lastX = x - second[4];
    return {((firstX * first[1] - firstY * first[2]) + first[4]) -
                ((lastX * second[1] - lastY * second[2]) + second[4]),
            ((firstY * first[1] + firstX * first[2]) * first[3] + first[5]) -
                ((lastY * second[1] + lastX * second[2]) * second[3] + second[5])};
}
ReverseCoordinates reversePlacementCoordinates(PlacementOrientation source, int64_t sourceX,
    int32_t sourceY, int64_t pivot, Point offset, int64_t variantLeft, int64_t pitch, int64_t originX) {
    if (!pitch) throw std::invalid_argument("Reverse placement requires nonzero grid pitch");
    ReverseCoordinates result;
    result.requested = composeOrientation(source, 18000000, true);
    result.x = add64(truncate64(roundTerm(offset.x * 1e6)), sub64(pivot, sourceX));
    result.y = add32(truncate32(roundTerm(offset.y * 1000.)), sourceY);
    const auto left = add64(variantLeft, result.x), relative = sub64(left, originX);
    if (relative == INT64_MIN && pitch == -1) throw std::invalid_argument("Reverse placement grid division overflow");
    auto quotient = relative / pitch;
    if (relative < 0 && relative % pitch != 0) --quotient;
    const auto strip = std::bit_cast<int32_t>(uint32_t(quotient));
    const auto aligned = add64(std::bit_cast<int64_t>(uint64_t(int64_t(strip)) * uint64_t(pitch)), originX);
    result.unaligned = aligned != left;
    return result;
}
int32_t reverseTransfer(ReverseHost &host, int64_t pivot) {
    auto &normal = host.side(false), &reversed = host.side(true);
    const auto difference = std::bit_cast<int32_t>(uint32_t(normal.count) - uint32_t(reversed.count));
    const bool fromReversed = difference <= 0;
    auto &source = host.side(fromReversed), &target = host.side(!fromReversed);
    const auto amount = difference < 0 ? std::bit_cast<int32_t>(0u - uint32_t(difference)) : difference;
    if (amount < 0) throw std::invalid_argument("Reverse transfer count exceeds signed range");
    size_t selected = amount == -1 || source.count < amount ? source.placements.size() : size_t(amount);
    if (selected > source.placements.size()) throw std::invalid_argument("Reverse transfer count exceeds layout size");
    const std::vector<ReversePlacement> snapshot(source.placements.end() - selected, source.placements.end());
    for (const auto &placement : snapshot) {
        host.commit(!fromReversed, host.clone(placement, !fromReversed, pivot));
        if (target.placements.empty()) throw std::invalid_argument("Reverse transfer commit produced no placement");
        target.placements.back().attributes = placement.attributes;
    }
    target.shake = source.shake;
    return difference;
}
int32_t runReverse(ReverseHost &host) {
    auto &normal = host.side(false), &reversed = host.side(true);
    resetTrialRandom(reversed.random, host.configuredRandomSeed(), normal.seed);
    reversed.seed = normal.seed;
    resetTrialRandom(normal.random, host.configuredRandomSeed(), normal.seed);
    reversed.trial = normal.trial;
    host.reset(true);
    reversed.bound = normal.bound;
    if (!host.childExists()) host.createChild();
    const auto firstPivot = sub64(normal.maximumRight, border(normal.leftBorder));
    reverseTransfer(host, firstPivot);
    const auto lineage = normal.lineage;
    const auto fraction = normal.fraction;
    const auto fields = normal.lineageFields;
    host.reset(false);
    normal.lineage = lineage; normal.fraction = fraction; normal.lineageFields = fields;
    host.runChild();
    const auto pivot = add64(add64(sub64(reversed.maximumRight, border(reversed.leftBorder)),
                                   std::bit_cast<int64_t>(uint64_t(border(normal.leftBorder)) * 2)),
                            border(normal.rightBorder));
    reverseTransfer(host, pivot);
    return normal.count;
}
}
