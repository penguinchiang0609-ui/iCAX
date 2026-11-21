#include "recovered_sdk_points.hpp"
#include "recovered_sdk_sampling.hpp"
#include "recovered_boolean_arrangement.hpp"
#include "recovered_shaker_broadphase.hpp"
#include <immintrin.h>
#include <algorithm>
#include <bit>
#include <cmath>

namespace study::recovered {
namespace {
double quiet(double v) { return std::bit_cast<double>(std::bit_cast<uint64_t>(v) | 0x8000000000000ULL); }
double add(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a + b; }
double sub(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a - b; }
double mul(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a * b; }
double div(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a / b; }
double absBits(double a) { return std::bit_cast<double>(std::bit_cast<uint64_t>(a) & 0x7fffffffffffffffULL); }
ShakerPoint evaluate(const SdkLinearSegment &line, double parameter) {
    return {add(mul(line.direction[0], parameter), line.origin[0]), add(mul(line.direction[1], parameter), line.origin[1])};
}
double parameter(const SdkLinearSegment &line, ShakerPoint point) {
    return add(mul(sub(point[1], line.origin[1]), line.direction[1]), mul(sub(point[0], line.origin[0]), line.direction[0]));
}
double distance(ShakerPoint first, ShakerPoint last) {
    const double x = sub(first[0], last[0]), y = sub(first[1], last[1]);
    return std::sqrt(add(mul(y, y), mul(x, x)));
}
bool contains(const SdkLinearSegment &line, ShakerPoint point, bool excludeFirst, bool excludeLast) {
    const double cross = sub(mul(sub(line.origin[0], point[0]), line.direction[1]),
                             mul(sub(line.origin[1], point[1]), line.direction[0]));
    if (!(absBits(sub(cross, 0.)) <= 1e-6)) return false;
    const double p = parameter(line, point);
    const double lower = line.parameters[1] < line.parameters[0] ? line.parameters[1] : line.parameters[0];
    const double upper = line.parameters[0] < line.parameters[1] ? line.parameters[1] : line.parameters[0];
    const bool inside = lower <= p && p <= upper;
    const double firstDistance = distance(line.endpoints[0], point), lastDistance = distance(line.endpoints[1], point);
    const bool nearFirst = firstDistance < 1e-6, nearLast = lastDistance < 1e-6;
    return (inside || nearFirst || nearLast) && (!excludeFirst || !nearFirst) && (!excludeLast || !nearLast);
}
double clampParameter(const SdkLinearSegment &line, double value) {
    const double lower = line.parameters[1] <= line.parameters[0] ? line.parameters[1] : line.parameters[0];
    const double upper = line.parameters[1] <= line.parameters[0] ? line.parameters[0] : line.parameters[1];
    if (value < lower) value = lower;
    if (upper < value) value = upper;
    return value;
}
bool near(ShakerPoint a, ShakerPoint b, double tolerance) {
    return absBits(sub(a[0], b[0])) <= tolerance && absBits(sub(a[1], b[1])) <= tolerance;
}
struct ModelEdge {
    SdkLinearSegment line;
    std::array<ShakerPoint, 2> topology{};
};
struct Model {
    std::vector<ModelEdge> edges;
    bool closed = false;
};
Model buildModel(std::span<const ShakerPoint> points) {
    Model model;
    if (points.empty()) return model;
    ShakerPoint cursor = points.front();
    for (size_t index = 1; index <= points.size(); ++index) {
        const auto end = points[index == points.size() ? 0 : index];
        const auto segment = richStraightSegment(cursor, end);
        if (!near(segment.endpoints[0], cursor, 1e-6) || !near(segment.endpoints[1], end, 1e-6)) continue;
        model.edges.push_back({{segment.origin, segment.direction, {0., segment.length}, segment.endpoints}, {cursor, end}});
        cursor = end;
    }
    if (!model.edges.empty() && near(model.edges.back().line.endpoints[1], points.front(), 1e-6)) {
        model.closed = true;
        model.edges.back().topology[1] = points.front();
    }
    return model;
}
std::vector<SdkPointIntersection> intersections(const Model &model) {
    ShakerBroadTree index;
    std::vector<SdkPointIntersection> found;
    for (size_t current = 0; current < model.edges.size(); ++current) {
        const auto &line = model.edges[current].line;
        const auto &a = line.endpoints[0], &b = line.endpoints[1];
        const ShakerBox box{std::min(a[0], b[0]), std::min(a[1], b[1]), std::max(a[0], b[0]), std::max(a[1], b[1])};
        for (auto previous : index.query(box)) {
            const bool forward = current + 1 == previous || (model.closed && current + 1 == model.edges.size() && previous == 0);
            const bool backward = previous + 1 == current || (model.closed && previous + 1 == model.edges.size() && current == 0);
            const std::array<bool, 4> excluded{backward || (!model.closed && current == 0),
                forward || (!model.closed && current + 1 == model.edges.size()),
                forward || (!model.closed && previous == 0),
                backward || (!model.closed && previous + 1 == model.edges.size())};
            for (auto entry : sdkLinePairIntersections(line, model.edges[previous].line, excluded)) {
                entry.first = current;
                entry.second = previous;
                found.push_back(entry);
            }
        }
        index.insert({box, current});
    }
    return found;
}
std::vector<ShakerPoint> topologyPoints(const Model &model) {
    std::vector<ShakerPoint> points;
    for (const auto &edge : model.edges) points.push_back(edge.topology[0]);
    if (!model.closed && !model.edges.empty()) points.push_back(model.edges.back().topology[1]);
    return points;
}
ModelEdge part(ModelEdge edge, double first, double last, ShakerPoint start, ShakerPoint end) {
    edge.line.parameters = {first, last};
    edge.line.endpoints = {evaluate(edge.line, first), evaluate(edge.line, last)};
    edge.topology = {start, end};
    return edge;
}
struct Cut { double parameter = 0.; ShakerPoint point{}; bool first = false, last = false; };
Cut classify(const ModelEdge &edge, ShakerPoint point) {
    const double firstDistance = distance(point, edge.line.endpoints[0]), lastDistance = distance(point, edge.line.endpoints[1]);
    const double p = parameter(edge.line, point);
    const bool inside = std::min(edge.line.parameters[0], edge.line.parameters[1]) <= p && p <= std::max(edge.line.parameters[0], edge.line.parameters[1]);
    const auto projection = evaluate(edge.line, p);
    const double projectionDistance = distance(projection, point);
    if (inside && projectionDistance < firstDistance && projectionDistance < lastDistance) return {p, projection, false, false};
    const bool degenerate = near(edge.line.endpoints[0], edge.line.endpoints[1], 1e-6);
    if (lastDistance <= firstDistance) return {edge.line.parameters[1], edge.topology[1], degenerate, true};
    return {edge.line.parameters[0], edge.topology[0], true, degenerate};
}
std::optional<std::array<Model, 2>> split(const Model &model, const SdkPointIntersection &intersection) {
    const auto first = classify(model.edges[intersection.first], intersection.point);
    const auto second = classify(model.edges[intersection.second], intersection.point);
    std::vector<ModelEdge> edges;
    std::array<size_t, 2> positions{};
    const std::array<Cut, 2> cuts{first, second};
    for (size_t index = 0; index < model.edges.size(); ++index) {
        const auto &edge = model.edges[index];
        const size_t which = index == intersection.first ? 0 : 1;
        if (index != intersection.first && index != intersection.second) { edges.push_back(edge); continue; }
        const auto cut = cuts[which];
        if (cut.last) { edges.push_back(edge); positions[which] = edges.size(); }
        else if (cut.first) { positions[which] = edges.size(); edges.push_back(edge); }
        else {
            edges.push_back(part(edge, edge.line.parameters[0], cut.parameter, edge.topology[0], cut.point));
            positions[which] = edges.size();
            edges.push_back(part(edge, cut.parameter, edge.line.parameters[1], cut.point, edge.topology[1]));
        }
    }
    if (edges.empty()) return {};
    positions[0] %= edges.size(); positions[1] %= edges.size();
    if (positions[0] == positions[1]) return {};
    std::array<Model, 2> result;
    for (size_t side = 0; side < 2; ++side) {
        auto &curve = result[side];
        for (size_t index = positions[side]; index != positions[1 - side]; index = (index + 1) % edges.size())
            curve.edges.push_back(edges[index]);
        if (curve.edges.empty() || !near(curve.edges.back().line.endpoints[1], curve.edges.front().topology[0], 1e-6)) return {};
        curve.closed = true;
        curve.edges.back().topology[1] = curve.edges.front().topology[0];
    }
    return result;
}
bool splitRecursively(const Model &model, std::vector<Model> &result) {
    if (!model.closed || model.edges.size() == 2) { result.push_back(model); return true; }
    const auto found = intersections(model);
    if (found.empty()) { result.push_back(model); return true; }
    if (found.size() > 100) return false;
    const auto halves = split(model, found.front());
    if (!halves) return false;
    return splitRecursively((*halves)[0], result) && splitRecursively((*halves)[1], result);
}
double modelArea(const Model &model) {
    if (model.edges.empty()) return 0.;
    double area = 0.;
    const auto reference = model.edges.front().topology[0];
    for (const auto &edge : model.edges) area = add(area, richStraightSegmentArea(edge.line.endpoints, reference));
    return area;
}
bool booleanRepair(std::vector<ShakerPoint> &points) {
    if (points.empty()) return false;
    RichGraph graph;
    auto &curve = richStartCurve(graph, points.front(), 1e-6);
    for (size_t index = 1; index <= points.size(); ++index) richAppendLine(graph, curve, points[index == points.size() ? 0 : index]);
    richClose(curve);
    auto &polygon = graph.add(RichKind::Polygon);
    richSetOuter(polygon, &curve);
    ConvolutionCurves curves;
    auto paths = richStraightTreeToBoolean(polygon, curves);
    if (paths.empty() || !paths.front().outer) return false;
    const auto &path = *paths.front().outer;
    const BooleanEdgePair anchor{path.firstEdge, path.lastEdge};
    auto result = booleanArrangeStraightParts(path, std::span<const BooleanEdgePair>(&anchor, 1), 1e-6, curves, 0, true, false);
    if (!result) return false;
    booleanFinishStraightParts(*result, 1e-6, curves.edgeFactory(), curves.arcFactory());
    RichGraph recovered;
    auto &root = booleanStraightPartsToRich(recovered, *result, 1e-6);
    if (root.children.size() != 1 || !root.children.front()->outer) return false;
    points.clear();
    richVisit(*root.children.front()->outer, {[&](RichEvent event, const RichNode &node) {
        if (event == RichEvent::Vertex) points.push_back(node.point);
    }, {}});
    return true;
}
void cleanCollinear(std::vector<ShakerPoint> &points) {
    if (points.empty()) return;
    RichGraph graph;
    auto &curve = richStartCurve(graph, points.front(), 1e-6);
    for (size_t index = 1; index <= points.size(); ++index) richAppendLine(graph, curve, points[index == points.size() ? 0 : index]);
    richClose(curve);
    sdkCleanSampledCurve(graph, curve, 1e-8);
    points.clear();
    richVisit(curve, {[&](RichEvent event, const RichNode &node) { if (event == RichEvent::Vertex) points.push_back(node.point); }, {}});
}
}
SdkPointError::SdkPointError(int32_t value, std::optional<ShakerPoint> point)
    : std::invalid_argument(value == -979 ? "SDK contour self-intersection" : "Invalid SDK contour points"), code(value), intersection(point) {}
bool sdkPointsWithinLimits(std::span<const ShakerPoint> points) {
    for (const auto point : points)
        if (point[0] > 9223372036854.775 || point[0] < -9223372036854.775 || point[1] > 2147483.647 || point[1] < -2147483.648) return false;
    return true;
}
double sdkPointsSignedArea(std::span<const ShakerPoint> points) {
    if (points.empty()) return 0.;
    auto previous = points.back();
    double area = 0.;
    for (const auto point : points) {
        area = add(area, mul(sub(mul(previous[0], point[1]), mul(previous[1], point[0])), .5));
        previous = point;
    }
    return area;
}
void sdkOrientPoints(std::vector<ShakerPoint> &points, bool positive) {
    if ((sdkPointsSignedArea(points) > 0.) != positive) std::reverse(points.begin(), points.end());
}
std::vector<SdkPointIntersection> sdkLinePairIntersections(const SdkLinearSegment &first, const SdkLinearSegment &second,
                                                         std::array<bool, 4> excluded) {
    const double denominator = sub(mul(second.direction[1], first.direction[0]), mul(second.direction[0], first.direction[1]));
    if (denominator == 0.) return {};
    const double x = sub(second.origin[0], first.origin[0]), y = sub(second.origin[1], first.origin[1]);
    const double a = div(sub(mul(x, second.direction[1]), mul(y, second.direction[0])), denominator);
    const double b = div(sub(mul(x, first.direction[1]), mul(y, first.direction[0])), denominator);
    const auto point = evaluate(first, a);
    if (!contains(first, point, excluded[0], excluded[1]) || !contains(second, point, excluded[2], excluded[3])) return {};
    return {{point, 0, 0, {clampParameter(first, a), clampParameter(second, b)}}};
}
std::vector<SdkPointIntersection> sdkPointSelfIntersections(std::span<const ShakerPoint> points) { return intersections(buildModel(points)); }
std::vector<SdkPointLoop> sdkSplitPointLoops(std::span<const ShakerPoint> points) {
    std::vector<Model> models;
    splitRecursively(buildModel(points), models);
    std::vector<SdkPointLoop> result;
    for (const auto &model : models) {
        SdkPointLoop loop;
        loop.points = topologyPoints(model); loop.closed = model.closed;
        for (const auto &edge : model.edges) loop.segments.push_back(edge.line);
        result.push_back(std::move(loop));
    }
    return result;
}
std::optional<size_t> sdkSelectDominantPointLoop(std::span<const double> signedAreas, double areaRatio) {
    double maximum = 0.;
    for (const auto value : signedAreas) { const double area = absBits(value); maximum = maximum < area ? area : maximum; }
    const double threshold = mul(maximum, areaRatio);
    std::optional<size_t> selected;
    for (size_t index = 0; index < signedAreas.size(); ++index) if (absBits(signedAreas[index]) > threshold) {
        if (selected) return {};
        selected = index;
    }
    return selected;
}
bool sdkBooleanRepairPointSelfIntersections(std::vector<ShakerPoint> &points) { return booleanRepair(points); }
bool sdkRepairPointSelfIntersections(std::vector<ShakerPoint> &points, double areaRatio) {
    const double originalArea = sdkPointsSignedArea(points);
    auto output = points;
    bool repaired = booleanRepair(output);
    if (!repaired) {
        std::vector<Model> loops;
        splitRecursively(buildModel(points), loops);
        std::vector<double> areas;
        for (const auto &loop : loops) areas.push_back(modelArea(loop));
        const auto selected = sdkSelectDominantPointLoop(areas, areaRatio);
        if (!selected) return false;
        output = topologyPoints(loops[*selected]);
        repaired = true;
    }
    if (repaired) {
        if ((sdkPointsSignedArea(output) > 0.) != (originalArea > 0.)) std::reverse(output.begin(), output.end());
        points = std::move(output);
    }
    return repaired;
}
void sdkNormalizePoints(std::vector<ShakerPoint> &points, bool exterior, double geometryMode, double repairAreaRatio) {
    const int mode = _mm_cvttsd_si32(_mm_set_sd(geometryMode));
    const auto valid = [&]() { if (!sdkPointsWithinLimits(points)) throw SdkPointError(-996); };
    const auto count = [&]() { if (points.size() < 3) { if (exterior) throw SdkPointError(-996); return false; } return true; };
    if (points.size() < 3) throw SdkPointError(-996);
    valid();
    sdkOrientPoints(points, true);
    if (!count()) return;
    valid();
    if (mode != 0) sdkRemoveDuplicatePoints(points);
    if (!count()) return;
    valid();
    cleanCollinear(points);
    if (!count()) return;
    valid();
    if (mode != 0) {
        auto found = sdkPointSelfIntersections(points);
        if (!found.empty()) {
            auto point = found.front().point;
            if (mode == 1 || !sdkRepairPointSelfIntersections(points, repairAreaRatio)) throw SdkPointError(-979, point);
            found = sdkPointSelfIntersections(points);
            if (!found.empty()) throw SdkPointError(-979, found.front().point);
        }
    }
    if (.0001 > absBits(sdkPointsSignedArea(points))) { if (exterior) throw SdkPointError(-996); return; }
    sdkOrientPoints(points, exterior);
}
}
