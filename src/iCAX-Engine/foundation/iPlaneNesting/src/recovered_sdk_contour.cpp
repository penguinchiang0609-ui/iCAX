#include "recovered_sdk_contour.hpp"
#include <immintrin.h>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace study::recovered {
namespace {
double quiet(double v) { return std::bit_cast<double>(std::bit_cast<uint64_t>(v) | 0x8000000000000ULL); }
double add(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a + b; }
double sub(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a - b; }
double mul(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a * b; }
double div(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a / b; }
double absBits(double v) { return std::bit_cast<double>(std::bit_cast<uint64_t>(v) & 0x7fffffffffffffffULL); }
double neg(double v) { return std::bit_cast<double>(std::bit_cast<uint64_t>(v) ^ 0x8000000000000000ULL); }
RichConstructedSegment straight(ShakerPoint first, ShakerPoint last) {
    const auto segment = richStraightSegment(first, last);
    return {{}, {0., segment.length}, segment.endpoints};
}
struct Bisector { ShakerPoint origin, direction; };
Bisector bisector(ShakerPoint first, ShakerPoint last) {
    const double x = neg(sub(last[1], first[1])), y = sub(last[0], first[0]);
    const double length = std::sqrt(add(mul(x, x), mul(y, y)));
    ShakerPoint direction{1., 0.};
    if (length != 0.) direction = {div(x, length), div(y, length)};
    return {{{mul(add(first[0], last[0]), .5), mul(add(first[1], last[1]), .5)}}, direction};
}
bool near(ShakerPoint first, ShakerPoint last, double tolerance, bool inclusive = false) {
    const double x = absBits(sub(first[0], last[0])), y = absBits(sub(first[1], last[1]));
    return inclusive ? x <= tolerance && y <= tolerance : x < tolerance && y < tolerance;
}
RichNode *appendSegment(RichGraph &graph, RichNode &curve, ShakerPoint point, const RichConstructedSegment &segment) {
    auto &edge = graph.add(RichKind::Edge);
    edge.line = !segment.circle.has_value();
    edge.circle = segment.circle;
    edge.parameters = segment.parameters;
    edge.endpoints = segment.endpoints;
    auto &vertex = graph.add(RichKind::Vertex);
    vertex.point = point;
    if (!near(edge.endpoints[1], point, curve.tolerance, true)) return nullptr;
    richAttach(edge, &vertex);
    if (!near(edge.endpoints[0], curve.last->point, curve.tolerance, true)) return nullptr;
    richAttach(*const_cast<RichNode *>(curve.last), &edge);
    curve.last = &vertex;
    return &edge;
}
}
RichConstructedSegment richThreePointSegment(ShakerPoint first, ShakerPoint middle, ShakerPoint last) {
    // The original JB test takes the closed-circle branch for an unordered
    // middle coordinate too; zero-chord sagittas depend on that distinction.
    if (near(first, last, 1e-6, true) &&
        !(absBits(sub(first[0], middle[0])) <= 1e-6 && absBits(sub(first[1], middle[1])) <= 1e-6)) {
        const ShakerPoint start{mul(add(first[0], last[0]), .5), mul(add(first[1], last[1]), .5)};
        RichCircle circle;
        circle.center = {mul(add(start[0], middle[0]), .5), mul(add(start[1], middle[1]), .5)};
        circle.radial = {sub(start[0], circle.center[0]), sub(start[1], circle.center[1])};
        circle.radius = std::sqrt(add(mul(circle.radial[0], circle.radial[0]), mul(circle.radial[1], circle.radial[1])));
        const std::array<double, 2> parameters{0., richCirclePeriod(circle)};
        return {circle, parameters, {richCircleSample(circle, parameters[0]).point, richCircleSample(circle, parameters[1]).point}};
    }
    const double x = sub(last[0], first[0]), y = sub(last[1], first[1]);
    const double middleX = sub(middle[0], first[0]), middleY = sub(middle[1], first[1]);
    const double lastX = sub(middle[0], last[0]), lastY = sub(middle[1], last[1]);
    if (!(add(mul(middleY, y), mul(middleX, x)) > 0.) ||
        !(add(mul(lastY, y), mul(lastX, x)) < 0.)) return straight(first, last);
    const double firstLength = std::sqrt(add(mul(middleY, middleY), mul(middleX, middleX)));
    const double lastLength = std::sqrt(add(mul(y, y), mul(x, x)));
    if (firstLength == 0. || lastLength == 0.) return straight(first, last);
    const double cross = sub(mul(y, middleX), mul(x, middleY));
    const double shorter = lastLength < firstLength ? lastLength : firstLength;
    if (!(div(absBits(cross), shorter) > 1e-6)) return straight(first, last);
    const auto a = bisector(first, middle), b = bisector(middle, last);
    const double denominator = sub(mul(b.direction[1], a.direction[0]), mul(b.direction[0], a.direction[1]));
    if (denominator == 0.) return straight(first, last);
    const double dx = sub(b.origin[0], a.origin[0]), dy = sub(b.origin[1], a.origin[1]);
    const double parameter = div(sub(mul(dx, b.direction[1]), mul(dy, b.direction[0])), denominator);
    const ShakerPoint center{add(mul(a.direction[0], parameter), a.origin[0]),
                            add(mul(a.direction[1], parameter), a.origin[1])};
    if ((center[0] == first[0] && center[1] == first[1]) ||
        (center[0] == last[0] && center[1] == last[1])) return straight(first, last);
    const auto arc = richArcSegment(first, last, center, cross > 0.);
    return {arc.circle, arc.parameters, arc.endpoints};
}
RichNode *richAppendSagittaArc(RichGraph &graph, RichNode &curve, ShakerPoint point, double sagitta) {
    if (curve.kind != RichKind::Curve) throw std::invalid_argument("Rich append requires a curve");
    if (!curve.last || curve.last->next) return nullptr;
    const auto start = curve.last->point;
    const double x = sub(point[0], start[0]), negativeY = neg(sub(point[1], start[1]));
    const double length = std::sqrt(add(mul(negativeY, negativeY), mul(x, x)));
    const ShakerPoint middle{add(div(mul(negativeY, sagitta), length), mul(add(point[0], start[0]), .5)),
                             add(div(mul(x, sagitta), length), mul(add(point[1], start[1]), .5))};
    return appendSegment(graph, curve, point, richThreePointSegment(start, middle, point));
}
RichNode &sdkBuildContour(RichGraph &graph, const SdkContourInput &input) {
    if (input.points.empty() || input.points.size() > size_t(INT32_MAX))
        throw std::invalid_argument("Invalid SDK contour point count");
    const size_t count = input.points.size();
    if ((!input.values.empty() && input.values.size() < count) ||
        (!input.centers.empty() && input.centers.size() < count) ||
        (!input.directions.empty() && input.directions.size() < count))
        throw std::invalid_argument("Incomplete SDK contour edge arrays");
    const int geometryMode = _mm_cvttsd_si32(_mm_set_sd(input.geometryMode));
    auto &curve = richStartCurve(graph, input.points.front(), 1e-6);
    for (size_t index = 0; index < count; ++index) {
        auto point = input.points[index + 1 == count ? 0 : index + 1];
        if (index + 2 == count && near(curve.first->point, point, .001)) point = curve.first->point;
        if (geometryMode == 2 && near(curve.last->point, point, .001)) continue;
        if (input.kind == SdkContourKind::Centers && !input.centers.empty() && !input.directions.empty()) {
            const auto center = input.centers[index];
            if (center[0] != point[0] || center[1] != point[1]) {
                richAppendArc(graph, curve, point, center, input.directions[index] != 0);
                continue;
            }
        } else if ((input.kind == SdkContourKind::Bulges || input.kind == SdkContourKind::Sagittas) && !input.values.empty()) {
            const double value = input.values[index];
            if (!(1e-6 > absBits(sub(value, 0.)))) {
                double sagitta = value;
                if (input.kind == SdkContourKind::Bulges) {
                    const double x = sub(point[0], curve.last->point[0]), y = sub(point[1], curve.last->point[1]);
                    sagitta = mul(mul(std::sqrt(add(mul(y, y), mul(x, x))), .5), value);
                }
                richAppendSagittaArc(graph, curve, point, neg(sagitta));
                continue;
            }
        }
        richAppendLine(graph, curve, point);
    }
    richClose(curve);
    if (richStraightCurveArea(curve) < 0. && input.positive) richReverse(curve);
    if (.0001 > absBits(richStraightCurveArea(curve))) throw std::invalid_argument("SDK contour area is below tolerance");
    return curve;
}
}
