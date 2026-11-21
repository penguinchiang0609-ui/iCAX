#include "recovered_convolution_offset.hpp"
#include "recovered_escher_normalization.hpp"
#include <boost/polygon/isotropy.hpp>
#include <boost/multiprecision/cpp_int.hpp>

namespace boost::polygon {
// 80e540 uses exact rational evaluation, integer truncation and a separate
// downward correction. A double-backed union changes integral crossings by
// one unit when cancellation leaves a value just below the true integer.
template <> struct high_precision_type<int64_t> {
    using type = boost::multiprecision::cpp_rational;
};
template <> inline int64_t convert_high_precision_type<int64_t>(
    const high_precision_type<int64_t>::type &value) {
    if (value < INT64_MIN)
        return INT64_MIN;
    if (value > INT64_MAX)
        return INT64_MAX;
    return value.convert_to<int64_t>();
}
}

#include <boost/polygon/polygon.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace study::recovered {
namespace {
using IntegerPoint = boost::polygon::point_data<int64_t>;
using IntegerPolygon = boost::polygon::polygon_data<int64_t>;
using IntegerPart = boost::polygon::polygon_with_holes_data<int64_t>;
using IntegerSet = boost::polygon::polygon_set_data<int64_t>;

ConvolutionIntegerBox bounds(const ConvolutionIntegerMultiPolygon &polygons) {
    ConvolutionIntegerBox box{INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
    for (const auto &polygon : polygons)
        for (const auto &ring : polygon)
            for (const auto &point : ring)
                for (size_t axis = 0; axis < 2; ++axis) {
                    box[axis] = std::min(box[axis], point[axis]);
                    box[axis + 2] = std::max(box[axis + 2], point[axis]);
                }
    return box;
}
IntegerPolygon toBoost(const ConvolutionIntegerRing &ring) {
    std::vector<IntegerPoint> points;
    points.reserve(ring.size());
    for (const auto &point : ring)
        points.emplace_back(point[0], point[1]);
    return IntegerPolygon(points.begin(), points.end());
}
ConvolutionIntegerRing fromBoost(const IntegerPolygon &polygon, bool hole) {
    ConvolutionIntegerRing result;
    for (const auto &point : polygon)
        result.push_back({point.x(), point.y()});
    if (result.size() > 1 && result.front() == result.back())
        result.pop_back();
    if (!result.empty() && (convolutionIntegerOrientation(result) < 0) != hole)
        std::reverse(result.begin(), result.end());
    return result;
}
IntegerSet mergeParts(std::span<const IntegerPart> parts) {
    using namespace boost::polygon::operators;
    // 801c10 uses the binary's threshold 5. Each intermediate union is
    // materialized, so rounded intersection vertices feed the next operation.
    if (parts.size() >= 5) {
        const auto middle = parts.size() / 2;
        auto result = mergeParts(parts.first(middle));
        result |= mergeParts(parts.subspan(middle));
        return result;
    }
    IntegerSet result;
    for (const auto &part : parts)
        result |= part;
    return result;
}
}

void convolutionMergeIntegerOffsets(ConvolutionIntegerMultiPolygon &polygons,
                                    const ConvolutionHierarchyDiagnostic &diagnostic) {
    if (polygons.empty())
        return;
    if (polygons.front().empty() || polygons.front().front().empty())
        throw std::invalid_argument("Offset merging requires a nonempty first ring");
    const auto inputBounds = bounds(polygons);
    std::vector<IntegerPart> inputParts;
    for (const auto &polygon : polygons) {
        if (polygon.empty())
            throw std::invalid_argument("Offset merging requires nonempty polygon groups");
        const int outerOrientation = convolutionIntegerOrientation(polygon.front());
        std::optional<IntegerPart> current;
        std::vector<IntegerPolygon> holes;
        std::vector<IntegerPart> group;
        const auto appendCurrent = [&] {
            if (current) {
                current->set_holes(holes.begin(), holes.end());
                group.push_back(*current);
            }
        };
        for (const auto &ring : polygon) {
            if (convolutionIntegerOrientation(ring) == outerOrientation) {
                appendCurrent();
                const auto outer = toBoost(ring);
                current.emplace(outer.begin(), outer.end());
                holes.clear();
            } else if (current) {
                holes.push_back(toBoost(ring));
            } else if (diagnostic) {
                diagnostic("ToBoostPolygonWithHolesSet invalid input: first polygon is a hole and not an external. We decide to ignore this hole and hope for the best.");
            }
        }
        appendCurrent();
        // 807460 inserts each polygon's converted group at the beginning.
        inputParts.insert(inputParts.begin(), group.begin(), group.end());
    }
    auto merged = mergeParts(inputParts);
    std::vector<IntegerPart> output;
    merged.get(output);
    ConvolutionIntegerMultiPolygon result;
    for (const auto &part : output) {
        IntegerPolygon outer(part.begin(), part.end());
        ConvolutionIntegerPolygon polygon{fromBoost(outer, false)};
        for (auto hole = part.begin_holes(); hole != part.end_holes(); ++hole)
            polygon.push_back(fromBoost(*hole, true));
        if (!polygon.front().empty())
            result.push_back(std::move(polygon));
    }
    // The original drops a nonempty union if it changes any outer bound.
    if (!result.empty() && bounds(result) != inputBounds)
        result.clear();
    polygons = std::move(result);
}
bool escherRepairIntegerRing(ConvolutionIntegerRing &ring) {
    // 807200 accumulates trapezoids relative to the initial Y coordinate.
    // Its sign is opposite the shoelace area and controls restored winding.
    double originalArea = 0.;
    if (!ring.empty()) {
        const double reference = double(ring.front()[1]);
        auto previous = ring.front();
        for (size_t index = 1; index < ring.size(); ++index) {
            const auto current = ring[index];
            const double previousX = double(previous[0]);
            if (previousX != double(current[0]))
                originalArea = originalArea + ((double(current[1]) - reference) +
                    (double(previous[1]) - reference)) * (double(current[0]) - previousX) * .5;
            previous = current;
        }
        if (previous != ring.front())
            originalArea = originalArea + ((double(previous[1]) - reference) + (reference - reference)) *
                (double(ring.front()[0]) - double(previous[0])) * .5;
    }
    IntegerSet shape;
    shape.insert(toBoost(ring));
    std::vector<IntegerPart> parts;
    shape.get(parts);
    if (parts.size() != 1) return false;
    const IntegerPolygon outer(parts.front().begin(), parts.front().end());
    auto result = fromBoost(outer, originalArea >= 0.);
    ring = std::move(result);
    return true;
}
namespace {
ConvolutionIntegerPolygon escherSetRings(const IntegerSet &set) {
    std::vector<IntegerPart> parts;
    set.get(parts);
    ConvolutionIntegerPolygon result;
    for (const auto &part : parts) {
        result.push_back(fromBoost(IntegerPolygon(part.begin(), part.end()), false));
        for (auto hole = part.begin_holes(); hole != part.end_holes(); ++hole)
            result.push_back(fromBoost(IntegerPolygon(hole->begin(), hole->end()), true));
    }
    return result;
}
IntegerSet escherOrientedRingSet(std::span<const ConvolutionIntegerRing> rings) {
    using namespace boost::polygon::operators;
    if (rings.size() >= 5) {
        const size_t middle = rings.size() / 2;
        auto result = escherOrientedRingSet(rings.first(middle));
        result |= escherOrientedRingSet(rings.subspan(middle));
        return result;
    }
    IntegerSet positive, negative;
    for (const auto &ring : rings) {
        if (convolutionIntegerOrientation(ring) == 1) positive |= toBoost(ring);
        else negative |= toBoost(ring);
    }
    positive -= negative;
    return positive;
}
}
void escherUnionIntegerRings(ConvolutionIntegerPolygon &rings) {
    if (rings.empty()) return;
    const auto originalBounds = bounds(ConvolutionIntegerMultiPolygon{rings});
    const auto shape = escherOrientedRingSet(rings);
    auto result = escherSetRings(shape);
    if (!result.empty() && bounds(ConvolutionIntegerMultiPolygon{result}) != originalBounds)
        result.clear();
    rings = std::move(result);
}
ConvolutionIntegerPolygon escherDifferenceIntegerRings(
    const ConvolutionIntegerPolygon &outer, const ConvolutionIntegerPolygon &holes) {
    if (holes.empty()) return outer;
    using namespace boost::polygon::operators;
    IntegerSet shape;
    for (const auto &ring : outer) shape |= toBoost(ring);
    for (const auto &ring : holes) shape -= toBoost(ring);
    return escherSetRings(shape);
}
}
