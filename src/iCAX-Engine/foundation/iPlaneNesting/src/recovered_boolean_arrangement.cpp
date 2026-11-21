#include "recovered_boolean_arrangement.hpp"
#include "recovered_convolution_line.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <list>
#include <map>
#include <numbers>
#include <stdexcept>

namespace study::recovered {
namespace {
double quiet(double v) {
    return std::bit_cast<double>(std::bit_cast<uint64_t>(v) | 0x8000000000000ULL);
}
double add(double a, double b) {
    if (std::isnan(a))
        return quiet(a);
    if (std::isnan(b))
        return quiet(b);
    return a + b;
}
double sub(double a, double b) {
    if (std::isnan(a))
        return quiet(a);
    if (std::isnan(b))
        return quiet(b);
    return a - b;
}
double mul(double a, double b) {
    if (std::isnan(a))
        return quiet(a);
    if (std::isnan(b))
        return quiet(b);
    return a * b;
}
double div(double a, double b) {
    if (std::isnan(a)) return quiet(a);
    if (std::isnan(b)) return quiet(b);
    return a / b;
}
double neg(double value) {
    return std::bit_cast<double>(std::bit_cast<uint64_t>(value) ^ 0x8000000000000000ULL);
}
void byte(uint64_t &word, unsigned shift, bool value) {
    word = (word & ~(uint64_t(255) << shift)) | (uint64_t(value) << shift);
}
bool marked(const ConvolutionEdge &e) {
    return ((e.metadata[9] >> 32) & 255) != 0;
}
void mark(ConvolutionEdge &e, bool v) {
    byte(e.metadata[9], 32, v);
}
void visit(ConvolutionVertex &v) {
    byte(v.metadata[5], 40, true);
}
uint64_t group(const ConvolutionVertex &v) {
    return v.metadata[4];
}
int32_t next(const ConvolutionTopology &t, int32_t edge) {
    return t.vertices.at(t.edges.at(edge).last).outgoing;
}
int32_t previous(const ConvolutionTopology &t, int32_t edge) {
    return t.vertices.at(t.edges.at(edge).first).incoming;
}
std::vector<int32_t> pathEdges(const ConvolutionTopology &t, int32_t first, int32_t last) {
    std::vector<int32_t> result;
    for (auto e = first;; e = next(t, e)) {
        if (e < 0 || result.size() >= t.edges.size())
            throw std::invalid_argument("Invalid Boolean path traversal");
        result.push_back(e);
        if (e == last)
            return result;
    }
}
const ConvolutionLine &line(const ConvolutionEdge &e) {
    if (!e.curve.line)
        throw std::invalid_argument("Boolean arrangement stage requires straight curves");
    return *e.curve.line;
}
ShakerPoint midpoint(const ConvolutionEdge &e) {
    return convolutionCurveEvaluate(e.curve, mul(add(e.curve.parameters[1], e.curve.parameters[0]), .5));
}
double length(const ConvolutionEdge &e) {
    if (e.curve.circle) return std::abs(sub(e.curve.parameters[1], e.curve.parameters[0]));
    const auto b = convolutionLineEvaluate(line(e), e.curve.parameters[1]),
               a = convolutionLineEvaluate(line(e), e.curve.parameters[0]);
    const auto dx = sub(b[0], a[0]), dy = sub(b[1], a[1]);
    return std::sqrt(add(mul(dy, dy), mul(dx, dx)));
}
void attachStart(ConvolutionTopology &t, int32_t edge, int32_t vertex) {
    auto &e = t.edges.at(edge);
    if (e.first >= 0)
        t.vertices.at(e.first).outgoing = -1;
    e.first = vertex;
    if (vertex >= 0) {
        auto &v = t.vertices.at(vertex);
        if (v.outgoing >= 0)
            t.edges.at(v.outgoing).first = -1;
        v.outgoing = edge;
    }
}
bool closed(const ConvolutionTopology &t, const BooleanRebuiltPath &p) {
    return t.edges.at(p.firstEdge).first >= 0 &&
           t.edges.at(p.firstEdge).first == t.edges.at(p.lastEdge).last;
}
int32_t eventVertex(const ConvolutionTopology &t, BooleanBoundaryEvent event, bool far) {
    const auto &e = t.edges.at(event.edge);
    return event.outgoing == far ? e.last : e.first;
}
bool sameVertex(const ConvolutionTopology &t, int32_t a, int32_t b) {
    return a == b ||
           (group(t.vertices.at(a)) != 0 && group(t.vertices.at(a)) == group(t.vertices.at(b)));
}
int32_t eventPriority(const ConvolutionTopology &t, BooleanBoundaryEvent event) {
    return std::bit_cast<int32_t>(uint32_t(t.edges.at(event.edge).metadata[3] >> 32));
}
void attachEnd(ConvolutionTopology &t, int32_t edge, int32_t vertex) {
    auto &e = t.edges.at(edge);
    if (e.last >= 0)
        t.vertices.at(e.last).incoming = -1;
    e.last = vertex;
    if (vertex >= 0) {
        auto &v = t.vertices.at(vertex);
        if (v.incoming >= 0)
            t.edges.at(v.incoming).last = -1;
        v.incoming = edge;
    }
}
std::vector<int32_t> containmentMapOrder(std::span<const int32_t> insertionOrder,
                                         std::span<const BooleanContainmentNode> nodes) {
    const auto hash = [&](int32_t index) {
        uint64_t value = nodes[index].identity, result = 0xcbf29ce484222325ULL;
        for (size_t byte = 0; byte < 8; ++byte) {
            result = (result ^ (value & 255)) * 0x100000001b3ULL;
            value >>= 8;
        }
        return result;
    };
    std::list<int32_t> order;
    std::vector<std::list<int32_t>::iterator> buckets(8, order.end());
    for (auto index : insertionOrder) {
        const auto required = size_t(std::ceil(float(order.size() + 1)));
        if (required > buckets.size()) {
            auto capacity = std::max<size_t>(8, required);
            if (buckets.size() < 512)
                capacity = std::max(capacity, buckets.size() * 8);
            buckets.assign(std::bit_ceil(capacity), order.end());
            for (auto it = order.begin(); it != order.end();) {
                auto after = std::next(it);
                auto &first = buckets[hash(*it) & (buckets.size() - 1)];
                if (first != order.end())
                    order.splice(first, order, it);
                first = it;
                it = after;
            }
        }
        auto &first = buckets[hash(index) & (buckets.size() - 1)];
        first = order.insert(first, index);
    }
    return {order.begin(), order.end()};
}
struct HierarchySweepEvent {
    double position = 0.;
    uint64_t sequence = 0;
    int32_t path = -1;
    bool entering = false;
};
template <class Event> bool sweepLess(const Event &a, const Event &b) {
    return a.position < b.position ||
           (a.position == b.position &&
            std::bit_cast<int64_t>(a.sequence) < std::bit_cast<int64_t>(b.sequence));
}
template <class Event> void sweepHeapInsert(std::vector<Event> &heap, size_t hole, Event event) {
    while (hole > 0) {
        const auto parent = (hole - 1) / 2;
        if (!sweepLess(event, heap[parent]))
            break;
        heap[hole] = heap[parent];
        hole = parent;
    }
    heap[hole] = event;
}
template <class Event> Event sweepHeapPop(std::vector<Event> &heap) {
    const auto result = heap.front(), replacement = heap.back();
    heap.pop_back();
    if (heap.empty())
        return result;
    const auto size = heap.size(), lastParent = (size - 1) / 2;
    size_t hole = 0;
    while (hole < lastParent) {
        const auto left = hole * 2 + 1, right = left + 1;
        const auto child = sweepLess(heap[left], heap[right]) ? left : right;
        heap[hole] = heap[child];
        hole = child;
    }
    if (hole == lastParent && size % 2 == 0) {
        heap[hole] = heap[size - 1];
        hole = size - 1;
    }
    sweepHeapInsert(heap, hole, replacement);
    return result;
}
} // namespace
namespace {
const ConvolutionLine &inputLine(const ConvolutionCurveSegment &curve) {
    if (!curve.line)
        throw std::invalid_argument("Boolean input requires straight curves");
    return *curve.line;
}
double pointDistance(ShakerPoint a, ShakerPoint b) {
    const auto x = sub(a[0], b[0]), y = sub(a[1], b[1]);
    return std::sqrt(add(mul(y, y), mul(x, x)));
}
bool intersectionBoundsOverlap(const ConvolutionCurveSegment &a, const ConvolutionCurveSegment &b,
                               double tolerance) {
    return !(a.bounds[0] > add(tolerance, b.bounds[2]) || sub(b.bounds[0], tolerance) > a.bounds[2] ||
             sub(b.bounds[1], tolerance) > a.bounds[3] || a.bounds[1] > add(tolerance, b.bounds[3]));
}
std::vector<ShakerPoint> lineCircleIntersections(const ConvolutionLine &line,
                                                const ConvolutionCircle &circle, double tolerance) {
    const auto &center = circle.center;
    const double distance = add(add(mul(center[0], line.normal[0]), mul(center[1], line.normal[1])), line.offset);
    if (std::abs(distance) > add(circle.radius, tolerance)) return {};
    const double square = sub(mul(circle.radius, circle.radius), mul(distance, distance));
    const double height = square >= 0 ? std::sqrt(square) : 0.;
    const ShakerPoint foot{sub(center[0], mul(distance, line.normal[0])),
                          sub(center[1], mul(distance, line.normal[1]))};
    if (tolerance > add(height, height)) return {foot};
    const ShakerPoint displacement{mul(neg(line.normal[1]), height), mul(height, line.normal[0])};
    return {{add(displacement[0], foot[0]), add(displacement[1], foot[1])},
            {sub(foot[0], displacement[0]), sub(foot[1], displacement[1])}};
}
std::vector<ShakerPoint> circleCircleIntersections(const ConvolutionCircle &a,
                                                  const ConvolutionCircle &b, double tolerance) {
    const double x = sub(b.center[0], a.center[0]), y = sub(b.center[1], a.center[1]);
    const double distance = std::sqrt(add(mul(y, y), mul(x, x)));
    if (add(add(b.radius, a.radius), tolerance) < distance ||
        add(add(distance, a.radius), tolerance) < b.radius ||
        add(add(distance, b.radius), tolerance) < a.radius || distance < tolerance) return {};
    const double diameter = add(distance, distance);
    if (distance < add(b.radius, a.radius)) {
        if (a.radius <= add(distance, b.radius)) {
            if (b.radius <= add(distance, a.radius)) {
                const double along = div(add(sub(mul(distance, distance), mul(b.radius, b.radius)),
                                             mul(a.radius, a.radius)), diameter);
                const double square = sub(mul(a.radius, a.radius), mul(along, along));
                const double height = square > 0 ? std::sqrt(square) : 0.;
                const ShakerPoint foot{add(mul(div(along, distance), x), a.center[0]),
                                      add(mul(div(along, distance), y), a.center[1])};
                if (tolerance > add(height, height)) return {foot};
                const ShakerPoint delta{mul(neg(y), div(height, distance)), mul(div(height, distance), x)};
                return {{add(delta[0], foot[0]), add(delta[1], foot[1])},
                        {sub(foot[0], delta[0]), sub(foot[1], delta[1])}};
            }
            const double scale = div(add(add(distance, a.radius), b.radius), diameter);
            return {{sub(b.center[0], mul(scale, x)), sub(b.center[1], mul(scale, y))}};
        }
        const double scale = div(add(add(distance, a.radius), b.radius), diameter);
        return {{add(a.center[0], mul(scale, x)), add(a.center[1], mul(scale, y))}};
    }
    const double scale = div(sub(add(distance, a.radius), b.radius), diameter);
    return {{add(a.center[0], mul(scale, x)), add(a.center[1], mul(scale, y))}};
}
bool intersectionNearEndpoint(const ConvolutionCurveSegment &curve, double parameter, double tolerance) {
    const auto point = convolutionCurveEvaluate(curve, parameter);
    const double squaredTolerance = mul(tolerance, tolerance);
    for (const auto endpoint : curve.endpoints) {
        const double x = sub(point[0], endpoint[0]), y = sub(point[1], endpoint[1]);
        if (!(squaredTolerance < add(mul(y, y), mul(x, x)))) return true;
    }
    return false;
}
} // namespace
bool booleanStraightEdgeContainsPoint(const ConvolutionCurveSegment &curve, ShakerPoint point,
                                      double tolerance, double &parameter) {
    if (sub(curve.bounds[1], point[1]) > tolerance || sub(point[1], curve.bounds[3]) > tolerance ||
        sub(curve.bounds[0], point[0]) > tolerance || sub(point[0], curve.bounds[2]) > tolerance)
        return false;
    if (curve.circle) {
        if (!convolutionCirclePointOn(*curve.circle, point, tolerance, parameter)) return false;
        parameter = convolutionNormalizeParameter(curve, parameter);
    } else {
        const auto &geometry = inputLine(curve);
        const auto distance =
            add(add(mul(point[1], geometry.normal[1]), mul(point[0], geometry.normal[0])), geometry.offset);
        if (std::abs(distance) > tolerance) return false;
        parameter = convolutionLineParameter(geometry, point);
    }
    const auto first = curve.parameters[0], last = curve.parameters[1];
    const auto lower = first > last ? last : first, upper = last > first ? last : first;
    if (!(parameter >= lower && parameter <= upper))
        return false;
    const auto evaluated = convolutionCurveEvaluate(curve, parameter);
    return !(tolerance > pointDistance(evaluated, curve.endpoints[0]) ||
             tolerance > pointDistance(evaluated, curve.endpoints[1]));
}
double booleanStraightEdgePointParameter(const ConvolutionCurveSegment &curve, ShakerPoint point) {
    const auto parameter = convolutionNormalizeParameter(curve, convolutionCurveParameter(curve, point));
    const auto first = curve.parameters[0], last = curve.parameters[1];
    const auto lower = first > last ? last : first, upper = last > first ? last : first;
    if (parameter >= lower && parameter <= upper)
        return parameter;
    return pointDistance(point, curve.endpoints[0]) < pointDistance(point, curve.endpoints[1]) ? first
                                                                                               : last;
}
std::vector<std::array<double, 2>> booleanStraightEdgeIntersections(const ConvolutionCurveSegment &a,
                                                                    const ConvolutionCurveSegment &b,
                                                                    double tolerance) {
    const auto middleA = convolutionCurveEvaluate(a, mul(add(a.parameters[1], a.parameters[0]), .5)),
               middleB = convolutionCurveEvaluate(b, mul(add(b.parameters[1], b.parameters[0]), .5));
    if (pointDistance(middleB, middleA) < tolerance &&
        ((pointDistance(b.endpoints[0], a.endpoints[0]) < tolerance &&
          pointDistance(b.endpoints[1], a.endpoints[1]) < tolerance) ||
         (pointDistance(b.endpoints[1], a.endpoints[0]) < tolerance &&
          pointDistance(b.endpoints[0], a.endpoints[1]) < tolerance)))
        return {};
    if (a.circle || b.circle) {
        const auto points = a.circle && b.circle
            ? circleCircleIntersections(*a.circle, *b.circle, tolerance)
            : a.circle ? lineCircleIntersections(inputLine(b), *a.circle, tolerance)
                       : lineCircleIntersections(inputLine(a), *b.circle, tolerance);
        std::vector<std::array<double, 2>> result;
        for (const auto point : points) {
            const double first = convolutionNormalizeParameter(a, convolutionCurveParameter(a, point));
            const double second = convolutionNormalizeParameter(b, convolutionCurveParameter(b, point));
            if (first >= std::min(a.parameters[0], a.parameters[1]) && first <= std::max(a.parameters[0], a.parameters[1]) &&
                second >= std::min(b.parameters[0], b.parameters[1]) && second <= std::max(b.parameters[0], b.parameters[1]) &&
                !intersectionNearEndpoint(a, first, tolerance) && !intersectionNearEndpoint(b, second, tolerance))
                result.push_back({first, second});
        }
        return result;
    }
    const auto &first = inputLine(a), &second = inputLine(b);
    const auto denominator =
        sub(mul(second.direction[1], first.direction[0]), mul(second.direction[0], first.direction[1]));
    const auto x = sub(second.origin[0], first.origin[0]), y = sub(second.origin[1], first.origin[1]);
    const auto numeratorA = sub(mul(second.direction[1], x), mul(second.direction[0], y)),
               numeratorB = sub(mul(first.direction[1], x), mul(first.direction[0], y));
    const auto lowA = mul(denominator, a.parameters[0]), highA = mul(denominator, a.parameters[1]),
               lowB = mul(denominator, b.parameters[0]), highB = mul(denominator, b.parameters[1]);
    if (!(std::min(lowA, highA) < numeratorA && numeratorA < std::max(lowA, highA) &&
          std::min(lowB, highB) < numeratorB && numeratorB < std::max(lowB, highB)))
        return {};
    const double parameterA = numeratorA / denominator, parameterB = numeratorB / denominator;
    const auto evaluatedA = convolutionLineEvaluate(first, parameterA),
               evaluatedB = convolutionLineEvaluate(second, parameterB);
    if (std::min(a.parameters[0], a.parameters[1]) > parameterA ||
        parameterA > std::max(a.parameters[0], a.parameters[1]) ||
        std::min(b.parameters[0], b.parameters[1]) > parameterB ||
        parameterB > std::max(b.parameters[0], b.parameters[1]) ||
        tolerance >= pointDistance(a.endpoints[0], evaluatedA) ||
        tolerance >= pointDistance(a.endpoints[1], evaluatedA) ||
        tolerance >= pointDistance(b.endpoints[0], evaluatedB) ||
        tolerance >= pointDistance(b.endpoints[1], evaluatedB) ||
        pointDistance(evaluatedB, evaluatedA) > tolerance)
        return {};
    return {{parameterA, parameterB}};
}
bool booleanStraightEdgesIntersect(const ConvolutionCurveSegment &a, const ConvolutionCurveSegment &b,
                                   double tolerance) {
    return intersectionBoundsOverlap(a, b, tolerance) &&
           !booleanStraightEdgeIntersections(a, b, tolerance).empty();
}
namespace {
enum class InputEventKind { vertexEnter, vertexLeave, edgeEnter, edgeLeave };
struct InputSweepEvent {
    double position;
    uint64_t sequence;
    int32_t index;
    InputEventKind kind;
    bool first = false;
};
struct InputSweep {
    ConvolutionTopology &graph;
    ConvolutionCurves &curves;
    double tolerance;
    BooleanInputArrangement output;
    std::vector<InputSweepEvent> heap;
    std::vector<int32_t> vertices, edges;

    void queue(int32_t index, InputEventKind kind, bool first = false) {
        double position;
        if (kind == InputEventKind::vertexEnter || kind == InputEventKind::vertexLeave)
            position = graph.vertices.at(index).point[0];
        else
            position = graph.edges.at(index).curve.bounds[kind == InputEventKind::edgeEnter ? 0 : 2];
        position = kind == InputEventKind::vertexEnter || kind == InputEventKind::edgeEnter
                       ? sub(position, tolerance)
                       : add(position, tolerance);
        InputSweepEvent event{position, ++output.state.sequence, index, kind, first};
        heap.push_back(event);
        sweepHeapInsert(heap, heap.size() - 1, event);
    }
    size_t createGroup(int32_t vertex) {
        const auto index = output.groups.size();
        output.groups.push_back({{vertex}, {}, tolerance, index + 1});
        graph.vertices.at(vertex).metadata[4] = index + 1;
        return index;
    }
    void groupNearby(int32_t vertex, bool first) {
        std::optional<size_t> current;
        if (first)
            current = createGroup(vertex);
        for (auto other : vertices) {
            const auto a = graph.vertices.at(other).point, b = graph.vertices.at(vertex).point;
            const auto x = sub(a[0], b[0]), y = sub(a[1], b[1]);
            if (!(std::sqrt(add(mul(x, x), mul(y, y))) < tolerance))
                continue;
            if (!current)
                current = createGroup(vertex);
            const auto previous = graph.vertices.at(other).metadata[4];
            auto &target = output.groups[*current].vertices;
            if (!previous) {
                graph.vertices[other].metadata[4] = *current + 1;
                target.push_back(other);
            } else if (previous != *current + 1) {
                auto &source = output.groups.at(previous - 1).vertices;
                for (auto member : source) {
                    graph.vertices[member].metadata[4] = *current + 1;
                    target.push_back(member);
                }
                source.clear();
            }
        }
    }
    void activate() {
        output.state.active = std::bit_cast<int32_t>(uint32_t(output.state.active) + 1);
        output.state.maximum = std::max(output.state.maximum, output.state.active);
    }
    void deactivate() {
        output.state.active = std::bit_cast<int32_t>(uint32_t(output.state.active) - 1);
    }
    int32_t splitVertex(const ConvolutionCurveSegment &curve, double parameter) {
        const auto index = int32_t(graph.vertices.size());
        ConvolutionVertex vertex{convolutionCurveEvaluate(curve, parameter)};
        vertex.metadata = {0, 0, UINT32_MAX, UINT32_MAX, 0, UINT32_MAX};
        graph.vertices.push_back(vertex);
        return index;
    }
    int32_t splitSegment(const ConvolutionEdge &source, int32_t first, int32_t last, double start,
                         double end) {
        const auto index = int32_t(graph.edges.size());
        graph.edges.push_back({first, last, curves.range(source.curve, start, end), source.metadata});
        if (first >= 0) {
            auto &vertex = graph.vertices.at(first);
            if (vertex.outgoing >= 0)
                graph.edges.at(vertex.outgoing).first = -1;
            vertex.outgoing = index;
        }
        if (last >= 0) {
            auto &vertex = graph.vertices.at(last);
            if (vertex.incoming >= 0)
                graph.edges.at(vertex.incoming).last = -1;
            vertex.incoming = index;
        }
        return index;
    }
    void split(int32_t edge, double parameter, bool multiple) {
        // Straight intersections supply at most one parameter. Preserve the
        // two original producers' different enqueue order for that same input.
        const auto source = graph.edges.at(edge);
        const auto vertex = splitVertex(source.curve, parameter);
        const auto first =
            splitSegment(source, source.first, vertex, source.curve.parameters[0], parameter);
        if (multiple) {
            queue(first, InputEventKind::edgeEnter);
            queue(vertex, InputEventKind::vertexEnter);
        }
        const auto last =
            splitSegment(source, vertex, source.last, parameter, source.curve.parameters[1]);
        if (!multiple)
            queue(first, InputEventKind::edgeEnter);
        queue(last, InputEventKind::edgeEnter);
        if (!multiple)
            queue(vertex, InputEventKind::vertexEnter);
    }
    void split(int32_t edge, std::span<const double> parameters) {
        const auto source = graph.edges.at(edge);
        auto firstVertex = source.first;
        auto firstParameter = source.curve.parameters[0];
        for (const auto parameter : parameters) {
            const auto vertex = splitVertex(source.curve, parameter);
            const auto segment = splitSegment(source, firstVertex, vertex, firstParameter, parameter);
            queue(segment, InputEventKind::edgeEnter);
            queue(vertex, InputEventKind::vertexEnter);
            firstVertex = vertex;
            firstParameter = parameter;
        }
        const auto finalSegment = splitSegment(source, firstVertex, source.last, firstParameter,
                                                source.curve.parameters[1]);
        queue(finalSegment, InputEventKind::edgeEnter);
    }
    bool pointOnEdge(int32_t edge, int32_t vertex) {
        double parameter;
        return booleanStraightEdgeContainsPoint(
            graph.edges.at(edge).curve, graph.vertices.at(vertex).point, tolerance / 3., parameter);
    }
    void enterVertex(int32_t vertex, bool first) {
        groupNearby(vertex, first);
        activate();
        vertices.push_back(vertex);
        const auto incoming = graph.vertices.at(vertex).incoming,
                   outgoing = graph.vertices.at(vertex).outgoing;
        for (size_t n = 0; n < edges.size();) {
            const auto edge = edges[n];
            ++output.state.comparisons;
            if (edge == incoming || edge == outgoing || !pointOnEdge(edge, vertex)) {
                ++n;
                continue;
            }
            split(edge,
                  booleanStraightEdgePointParameter(graph.edges.at(edge).curve,
                                                    graph.vertices.at(vertex).point),
                  false);
            edges.erase(edges.begin() + n);
            deactivate();
        }
        queue(vertex, InputEventKind::vertexLeave);
    }
    void enterEdge(int32_t edge) {
        const auto first = graph.edges.at(edge).first, last = graph.edges.at(edge).last;
        for (auto vertex : vertices) {
            ++output.state.comparisons;
            if (vertex != first && vertex != last && pointOnEdge(edge, vertex)) {
                split(edge,
                      booleanStraightEdgePointParameter(graph.edges.at(edge).curve,
                                                        graph.vertices.at(vertex).point),
                      false);
                output.removedEdges.push_back(edge);
                return;
            }
        }
        for (auto other : edges) {
            ++output.state.comparisons;
            const auto &a = graph.edges.at(other).curve, &b = graph.edges.at(edge).curve;
            if (!booleanStraightEdgesIntersect(a, b, mul(tolerance, .25)))
                continue;
            const auto intersections = booleanStraightEdgeIntersections(a, b, mul(tolerance, .25));
            std::vector<double> parameters;
            for (const auto intersection : intersections) parameters.push_back(intersection[1]);
            if (b.parameters[0] < b.parameters[1]) std::sort(parameters.begin(), parameters.end());
            else std::sort(parameters.begin(), parameters.end(), std::greater<double>());
            split(edge, parameters);
            output.removedEdges.push_back(edge);
            return;
        }
        activate();
        edges.push_back(edge);
        queue(edge, InputEventKind::edgeLeave);
    }
    void run() {
        while (!heap.empty()) {
            const auto event = sweepHeapPop(heap);
            ++output.state.events;
            switch (event.kind) {
            case InputEventKind::vertexEnter:
                enterVertex(event.index, event.first);
                break;
            case InputEventKind::edgeEnter:
                enterEdge(event.index);
                break;
            case InputEventKind::vertexLeave: {
                const auto found = std::find(vertices.begin(), vertices.end(), event.index);
                if (found == vertices.end())
                    throw std::invalid_argument("Boolean vertex leave precedes entry");
                vertices.erase(found);
                deactivate();
                break;
            }
            case InputEventKind::edgeLeave: {
                const auto found = std::find(edges.begin(), edges.end(), event.index);
                if (found == edges.end())
                    output.removedEdges.push_back(event.index);
                else {
                    edges.erase(found);
                    deactivate();
                }
                break;
            }
            }
        }
    }
};
} // namespace
BooleanInputArrangement booleanProduceStraightGroups(ConvolutionTopology &graph,
                                                     std::span<const BooleanEdgePair> paths,
                                                     double tolerance, ConvolutionCurves &curves,
                                                     BooleanInputSweepState initial) {
    if (!(tolerance > 0.) || !std::isfinite(tolerance))
        throw std::invalid_argument("Boolean input sweep requires finite positive tolerance");
    InputSweep sweep{graph, curves, tolerance};
    sweep.output.state = initial;
    for (const auto &path : paths) {
        if (path[0] < 0)
            continue;
        const auto edges = pathEdges(graph, path[0], path[1]);
        for (auto edge : edges) {
            const auto &curve = graph.edges.at(edge).curve;
            if (!curve.line && !curve.circle)
                throw std::invalid_argument("Boolean input requires a supported curve");
            sweep.queue(graph.edges.at(edge).first, InputEventKind::vertexEnter, edge == path[0]);
        }
        const auto last = graph.edges.at(path[1]).last;
        if (last != graph.edges.at(path[0]).first)
            sweep.queue(last, InputEventKind::vertexEnter);
        for (auto edge : edges)
            sweep.queue(edge, InputEventKind::edgeEnter);
    }
    sweep.run();
    return std::move(sweep.output);
}
BooleanHierarchySweepState booleanResolveDetachedHierarchy(
    const ConvolutionTopology &t, std::span<const BooleanArrangementGroup> groups,
    std::span<BooleanContainmentNode> nodes, double tolerance, BooleanHierarchySweepState state) {
    for (auto &node : nodes)
        node.descendants = -1;
    const auto markRoot = [&](auto &&self, int32_t index) -> int32_t {
        auto &node = nodes[index];
        node.descendants = 0;
        int32_t descendants = 0;
        for (auto child : node.children) {
            if (nodes[child].descendants != -1) {
                node.descendants = -1;
                return 0;
            }
            const auto count = self(self, child);
            descendants = std::bit_cast<int32_t>(uint32_t(descendants) + 1U + uint32_t(count));
        }
        node.descendants = descendants;
        return descendants;
    };
    for (size_t index = 0; index < nodes.size(); ++index)
        if (nodes[index].parent < 0)
            markRoot(markRoot, int32_t(index));
    const auto contained = [&](int32_t child, int32_t parent) {
        const auto &vertex = t.vertices.at(t.edges.at(nodes[child].path.firstEdge).first);
        const auto point = vertex.point;
        const auto &bounds = nodes[parent].path.bounds;
        if (sub(bounds[1], point[1]) > tolerance || sub(point[1], bounds[3]) > tolerance ||
            sub(bounds[0], point[0]) > tolerance || sub(point[0], bounds[2]) > tolerance)
            return false;
        if (group(vertex)) {
            const auto entry = std::find_if(groups.begin(), groups.end(), [&](const auto &candidate) {
                return candidate.identity == group(vertex);
            });
            if (entry == groups.end())
                throw std::invalid_argument("Missing Boolean boundary group identity");
            for (auto event : entry->events)
                if (t.edges.at(event.edge).metadata[7] == nodes[parent].identity)
                    return false;
        }
        const auto &path = nodes[parent].path;
        return booleanStraightPathContainsPoint(t, path.firstEdge, path.lastEdge, point);
    };
    std::vector<HierarchySweepEvent> heap;
    for (size_t index = 0; index < nodes.size(); ++index) {
        const auto &path = nodes[index].path;
        if (!closed(t, path))
            continue;
        for (bool entering : {true, false}) {
            const auto position =
                entering ? sub(path.bounds[0], tolerance) : add(path.bounds[2], tolerance);
            const HierarchySweepEvent event{position, ++state.sequence, int32_t(index), entering};
            heap.push_back(event);
            sweepHeapInsert(heap, heap.size() - 1, event);
        }
    }
    std::vector<int32_t> active;
    while (!heap.empty()) {
        const auto event = sweepHeapPop(heap);
        auto &node = nodes[event.path];
        if (event.entering) {
            auto position = active.begin();
            if (node.parent < 0) {
                for (; position != active.end(); ++position) {
                    if (node.parent == *position)
                        break;
                    if (contained(event.path, *position)) {
                        nodes[*position].children.push_back(event.path);
                        node.parent = *position;
                        break;
                    }
                }
            } else {
                for (; position != active.end(); ++position)
                    if (node.descendants < nodes[*position].descendants ||
                        contained(event.path, *position))
                        break;
            }
            active.insert(position, event.path);
            state.active = std::bit_cast<int32_t>(uint32_t(state.active) + 1);
            state.maximum = std::max(state.maximum, state.active);
        } else {
            std::erase(active, event.path);
            state.active = std::bit_cast<int32_t>(uint32_t(state.active) - 1);
        }
    }
    return state;
}
bool booleanResolvePathHierarchy(const ConvolutionTopology &t,
                                 std::span<const BooleanArrangementGroup> groups,
                                 std::span<BooleanContainmentNode> nodes) {
    std::map<uint64_t, int32_t> identities;
    for (size_t index = 0; index < nodes.size(); ++index)
        if (!nodes[index].identity || !identities.emplace(nodes[index].identity, int32_t(index)).second)
            throw std::invalid_argument("Boolean containment requires unique nonzero path identities");
    std::vector<std::vector<int32_t>> candidates(nodes.size());
    std::vector<int32_t> insertionOrder;
    for (const auto &entry : groups) {
        std::vector<int32_t> stack;
        for (size_t pass = 0; pass < 2; ++pass) {
            for (auto event : entry.events) {
                const auto path = identities.at(t.edges.at(event.edge).metadata[7]);
                if (!closed(t, nodes[path].path))
                    continue;
                if (event.outgoing == nodes[path].path.positive) {
                    if (pass == 1 && !stack.empty()) {
                        const auto parent = stack.back();
                        if (path != parent && std::abs(mul(nodes[path].path.signedArea, .5)) <=
                                                  std::abs(nodes[parent].path.signedArea)) {
                            auto &parents = candidates[path];
                            if (parents.empty())
                                insertionOrder.push_back(path);
                            if (std::find(parents.begin(), parents.end(), parent) == parents.end())
                                parents.push_back(parent);
                        }
                    }
                    stack.push_back(path);
                } else if (!stack.empty()) {
                    stack.pop_back();
                } else if (pass == 1) {
                    throw std::invalid_argument("Unbalanced Boolean containment boundary events");
                }
            }
        }
    }
    for (auto path : containmentMapOrder(insertionOrder, nodes)) {
        const auto &parents = candidates[path];
        int32_t selected = -1;
        if (parents.size() == 1) {
            selected = parents.front();
        } else {
            const auto &child = nodes[path].path;
            const auto point = booleanStraightPathInteriorPoint(t, child.firstEdge, child.lastEdge);
            for (auto parent : parents) {
                const auto &candidate = nodes[parent].path;
                if (booleanStraightPathContainsPoint(t, candidate.firstEdge, candidate.lastEdge,
                                                     point)) {
                    if (selected >= 0)
                        return false;
                    selected = parent;
                }
            }
            if (selected < 0)
                return false;
        }
        auto &node = nodes[path];
        if (node.parent < 0) {
            nodes[selected].children.push_back(path);
            node.parent = selected;
        } else if (node.parent != selected) {
            return false;
        }
    }
    return true;
}
std::array<double, 2> booleanPointLineAngles(const ConvolutionCurveSegment &curve, ShakerPoint point,
                                             double tolerance, std::array<double, 2> accumulated) {
    if (curve.circle) {
        const auto &circle = *curve.circle;
        const double centerX = sub(circle.center[0], point[0]), centerY = sub(circle.center[1], point[1]);
        const double distance = std::sqrt(add(mul(centerY, centerY), mul(centerX, centerX)));
        auto a = convolutionCircleEvaluate(circle, curve.parameters[0]);
        auto b = convolutionCircleEvaluate(circle, curve.parameters[1]);
        for (size_t axis = 0; axis < 2; ++axis) {
            a[axis] = sub(a[axis], point[axis]);
            b[axis] = sub(b[axis], point[axis]);
        }
        const bool ascending = curve.parameters[0] < curve.parameters[1];
        bool crossing = 0. < sub(mul(b[1], a[0]), mul(b[0], a[1]));
        if (ascending) crossing = !crossing;
        const bool unwrap = crossing && !(circle.radius <= distance) &&
                            !(tolerance <= std::abs(sub(distance, circle.radius)));
        if (std::sqrt(add(mul(a[1], a[1]), mul(a[0], a[0]))) < 1e-9) a = {1., 0.};
        if (std::sqrt(add(mul(b[1], b[1]), mul(b[0], b[0]))) < 1e-9) b = {1., 0.};
        const double angle = std::atan2(sub(mul(a[0], b[1]), mul(a[1], b[0])),
                                        add(mul(b[1], a[1]), mul(a[0], b[0])));
        const double complement = angle <= 0. ? add(angle, 2. * std::numbers::pi)
                                              : sub(angle, 2. * std::numbers::pi);
        if (unwrap)
            return {angle <= complement ? angle : complement, complement <= angle ? angle : complement};
        const double increment = circle.radius <= distance || ascending == (0. < angle) ? angle : complement;
        return {add(accumulated[0], increment), add(accumulated[1], increment)};
    }
    if (!curve.line)
        throw std::invalid_argument("Point angle requires a straight curve");
    const auto &geometry = *curve.line;
    auto a = convolutionLineEvaluate(geometry, curve.parameters[0]);
    auto b = convolutionLineEvaluate(geometry, curve.parameters[1]);
    for (size_t axis = 0; axis < 2; ++axis) {
        a[axis] = sub(a[axis], point[axis]);
        b[axis] = sub(b[axis], point[axis]);
    }
    const double distance =
        add(add(mul(point[1], geometry.normal[1]), mul(point[0], geometry.normal[0])), geometry.offset);
    const bool unwrap = tolerance > std::abs(distance) && 0. > add(mul(b[0], a[0]), mul(b[1], a[1]));
    if (std::sqrt(add(mul(a[0], a[0]), mul(a[1], a[1]))) < 1e-9)
        a = {1., 0.};
    if (std::sqrt(add(mul(b[0], b[0]), mul(b[1], b[1]))) < 1e-9)
        b = {1., 0.};
    double low =
        std::atan2(sub(mul(a[0], b[1]), mul(a[1], b[0])), add(mul(a[0], b[0]), mul(a[1], b[1])));
    double high = low;
    if (unwrap) {
        double other = low <= 0. ? add(low, 2. * std::numbers::pi) : sub(low, 2. * std::numbers::pi);
        high = other > low ? other : low;
        low = other < low ? other : low;
    }
    return {add(low, accumulated[0]), add(high, accumulated[1])};
}
bool booleanBoundarySourceLess(const ConvolutionTopology &t, BooleanBoundaryEvent a,
                               BooleanBoundaryEvent b) {
    if (a.outgoing != b.outgoing)
        return b.outgoing;
    const auto &x = t.edges.at(a.edge).curve, &y = t.edges.at(b.edge).curve;
    const auto left = x.source == y.source ? std::bit_cast<int64_t>(x.identity) : x.source;
    const auto right = x.source == y.source ? std::bit_cast<int64_t>(y.identity) : y.source;
    return a.outgoing ? left < right : right < left;
}
bool booleanStraightPathContainsPoint(const ConvolutionTopology &t, int32_t first, int32_t last,
                                      ShakerPoint point) {
    if (first < 0)
        return false;
    double angle = 0.;
    auto edge = first;
    size_t visited = 0;
    do {
        if (++visited > t.edges.size())
            throw std::invalid_argument("Invalid Boolean containment traversal");
        angle = add(angle, booleanPointLineAngles(t.edges.at(edge).curve, point, 0.)[0]);
        edge = next(t, edge);
    } while (edge != last);
    angle = add(angle, booleanPointLineAngles(t.edges.at(last).curve, point, 0.)[0]);
    return std::abs(angle) >= std::numbers::pi;
}
ShakerPoint booleanStraightPathInteriorPoint(const ConvolutionTopology &t, int32_t first, int32_t last) {
    double longest = 0.;
    int32_t selected = -1;
    for (auto index : pathEdges(t, first, last)) {
        const auto &endpoints = t.edges.at(index).curve.endpoints;
        const double dx = sub(endpoints[1][0], endpoints[0][0]),
                     dy = sub(endpoints[1][1], endpoints[0][1]);
        const double distance = std::sqrt(add(mul(dy, dy), mul(dx, dx)));
        if (longest < distance) {
            longest = distance;
            selected = index;
        }
    }
    if (selected < 0)
        throw std::invalid_argument("Boolean interior point requires a nonzero cached edge length");
    const auto &edge = t.edges.at(selected);
    const auto middle = mul(add(edge.curve.parameters[0], edge.curve.parameters[1]), .5);
    const auto center = convolutionCurveEvaluate(edge.curve, middle);
    auto direction = edge.curve.circle ? convolutionCircleTangent(*edge.curve.circle, middle)
                                       : line(edge).direction;
    if (edge.curve.parameters[1] <= edge.curve.parameters[0]) {
        direction[0] = -direction[0];
        direction[1] = -direction[1];
    }
    const double norm = std::sqrt(add(mul(direction[1], direction[1]), mul(direction[0], direction[0])));
    if (norm >= 1e-9) {
        const double factor = 1. / norm;
        direction[0] = mul(direction[0], factor);
        direction[1] = mul(direction[1], factor);
    } else {
        direction = {1., 0.};
    }
    const double sign = booleanStraightPathArea(t, first, last) >= 0. ? 1. : -1.;
    double distance = .1;
    do {
        ShakerPoint candidate{add(mul(mul(sign, -direction[1]), distance), center[0]),
                              add(mul(mul(sign, direction[0]), distance), center[1])};
        if (booleanStraightPathContainsPoint(t, first, last, candidate))
            return candidate;
        distance = mul(distance, .5);
    } while (1e-12 < distance);
    return center;
}
bool booleanBoundaryCoincidentLess(const ConvolutionTopology &t, BooleanBoundaryEvent a,
                                   BooleanBoundaryEvent b, double tolerance) {
    if (booleanStraightEdgesCoincide(t, a.edge, b.edge, tolerance))
        return booleanBoundarySourceLess(t, a, b);
    const auto &x = t.edges.at(a.edge), &y = t.edges.at(b.edge);
    auto angleB = booleanPointLineAngles(y.curve, midpoint(x), 0.)[0];
    auto angleA = booleanPointLineAngles(x.curve, midpoint(y), 0.)[0];
    if (!b.outgoing)
        angleB = -angleB;
    if (!a.outgoing)
        angleA = -angleA;
    return angleB < angleA;
}
double booleanBoundaryRelativeAngle(const ConvolutionTopology &t, BooleanBoundaryEvent a,
                                    BooleanBoundaryEvent b) {
    const auto farA = t.vertices.at(eventVertex(t, a, true)).point;
    const auto farB = t.vertices.at(eventVertex(t, b, true)).point;
    auto angleB = booleanPointLineAngles(t.edges.at(b.edge).curve, farA, 0.)[0];
    auto angleA = booleanPointLineAngles(t.edges.at(a.edge).curve, farB, 0.)[0];
    if (!b.outgoing)
        angleB = -angleB;
    if (!a.outgoing)
        angleA = -angleA;
    const auto nearA = t.vertices.at(eventVertex(t, a, false)).point;
    const auto nearB = t.vertices.at(eventVertex(t, b, false)).point;
    const double dx = sub(nearA[0], farB[0]), dy = sub(nearA[1], farB[1]);
    const double bx = sub(nearB[0], farB[0]), by = sub(nearB[1], farB[1]);
    const double angle = std::atan2(sub(mul(bx, dy), mul(by, dx)), add(mul(by, dy), mul(bx, dx)));
    return sub(add(angleB, std::numbers::pi), add(angle, angleA));
}
bool booleanBoundaryBetween(const ConvolutionTopology &t, BooleanBoundaryEvent candidate,
                            BooleanBoundaryEvent before, BooleanBoundaryEvent after, double tolerance) {
    const auto a = eventVertex(t, candidate, true), b = eventVertex(t, before, true),
               c = eventVertex(t, after, true);
    const auto less = [&](BooleanBoundaryEvent x, BooleanBoundaryEvent y) {
        return booleanBoundaryCoincidentLess(t, x, y, tolerance);
    };
    if (!sameVertex(t, b, c)) {
        if (sameVertex(t, b, a))
            return less(before, candidate);
        if (sameVertex(t, a, c))
            return less(candidate, after);
        const auto first = booleanBoundaryRelativeAngle(t, before, candidate);
        const auto second = booleanBoundaryRelativeAngle(t, after, candidate);
        const auto between = booleanBoundaryRelativeAngle(t, before, after);
        return std::numbers::pi < add(sub(second, first), between);
    }
    if (!sameVertex(t, b, a))
        return !less(before, after);
    if (less(before, after))
        return less(before, candidate) && less(candidate, after);
    return less(before, candidate) || less(candidate, after);
}
void booleanInsertBoundaryEvent(const ConvolutionTopology &t, BooleanBoundaryGroups &groups,
                                BooleanBoundaryEvent event, double tolerance) {
    std::vector<BooleanBoundaryEvent> merged{event};
    size_t index = 0;
    while (index < groups.size()) {
        const auto &old = groups[index];
        const bool equivalent = std::any_of(old.begin(), old.end(), [&](BooleanBoundaryEvent other) {
            return sameVertex(t, eventVertex(t, event, false), eventVertex(t, other, false)) &&
                   sameVertex(t, eventVertex(t, event, true), eventVertex(t, other, true)) &&
                   booleanStraightEdgesCoincide(t, event.edge, other.edge, tolerance);
        });
        if (equivalent) {
            merged.insert(merged.end(), old.begin(), old.end());
            groups.erase(groups.begin() + index);
        } else {
            ++index;
        }
    }
    groups.push_back(std::move(merged));
}
std::vector<BooleanBoundaryEvent> booleanMergeBoundaryGroups(const ConvolutionTopology &t,
                                                             BooleanBoundaryGroups groups,
                                                             double tolerance) {
    std::vector<size_t> order;
    for (size_t index = 0; index < groups.size(); ++index) {
        auto &events = groups[index];
        if (events.empty())
            throw std::invalid_argument("Empty Boolean boundary group");
        std::stable_sort(events.begin(), events.end(),
                         [&](auto a, auto b) { return booleanBoundarySourceLess(t, a, b); });
        size_t insert = order.size();
        if (order.size() > 1) {
            size_t current = 0;
            do {
                insert = (current + 1) % order.size();
                if (booleanBoundaryBetween(t, events.front(), groups[order[current]].front(),
                                           groups[order[insert]].front(), tolerance))
                    break;
                current = insert;
            } while (current != 0);
        }
        order.insert(order.begin() + insert, index);
    }
    std::vector<BooleanBoundaryEvent> result;
    for (auto index : order)
        result.insert(result.end(), groups[index].begin(), groups[index].end());
    return result;
}
BooleanRebuiltCollection booleanSortBoundaryEvents(ConvolutionTopology &t,
                                                   BooleanArrangementGroup &entry) {
    BooleanRebuiltCollection removed;
    BooleanBoundaryGroups groups;
    entry.events.clear();
    std::vector<int32_t> surviving;
    for (auto vertex : entry.vertices) {
        const auto incoming = t.vertices.at(vertex).incoming, outgoing = t.vertices.at(vertex).outgoing;
        bool retained = false;
        for (const auto event :
             {BooleanBoundaryEvent{incoming, false}, BooleanBoundaryEvent{outgoing, true}}) {
            if (event.edge < 0)
                continue;
            const auto &edge = t.edges.at(event.edge);
            if (group(t.vertices.at(edge.first)) == group(t.vertices.at(edge.last))) {
                attachStart(t, event.edge, -1);
                attachEnd(t, event.edge, -1);
                removed.removedEdges.push_back(event.edge);
            } else {
                booleanInsertBoundaryEvent(t, groups, event, entry.tolerance);
                retained = true;
            }
        }
        if (retained)
            surviving.push_back(vertex);
        else
            removed.removedVertices.push_back(vertex);
    }
    entry.vertices = std::move(surviving);
    for (auto &events : groups) {
        std::stable_sort(events.begin(), events.end(),
                         [&](auto a, auto b) { return eventPriority(t, a) < eventPriority(t, b); });
        const auto highest = events.back();
        for (auto event : events) {
            if (eventPriority(t, event) < eventPriority(t, highest)) {
                auto &word = t.edges.at(event.edge).metadata[3];
                word = (word & 0xffffffff00000000ULL) | uint32_t(t.edges.at(highest.edge).metadata[3]);
            }
        }
    }
    entry.events = booleanMergeBoundaryGroups(t, std::move(groups), entry.tolerance);
    return removed;
}
BooleanRebuiltCollection booleanPrepareBoundaryEvents(ConvolutionTopology &t,
                                                      std::span<BooleanArrangementGroup> groups) {
    BooleanRebuiltCollection removed;
    for (auto &entry : groups) {
        if (entry.vertices.size() > 1) {
            auto result = booleanSortBoundaryEvents(t, entry);
            removed.removedEdges.insert(removed.removedEdges.end(), result.removedEdges.begin(),
                                        result.removedEdges.end());
            removed.removedVertices.insert(removed.removedVertices.end(), result.removedVertices.begin(),
                                           result.removedVertices.end());
        }
    }
    for (const auto &entry : groups) {
        for (auto event : entry.events) {
            auto vertex = eventVertex(t, event, false);
            auto edge = event.outgoing ? t.vertices.at(vertex).incoming : t.vertices.at(vertex).outgoing;
            size_t traversed = 0;
            while (edge >= 0 && group(t.vertices.at(t.edges.at(edge).first)) ==
                                    group(t.vertices.at(t.edges.at(edge).last))) {
                if (++traversed > t.edges.size())
                    throw std::invalid_argument("Invalid Boolean boundary skip cycle");
                edge = event.outgoing ? previous(t, edge) : next(t, edge);
            }
            if (event.outgoing) {
                auto old = t.vertices.at(vertex).incoming;
                if (old >= 0)
                    attachEnd(t, old, -1);
                if (edge >= 0)
                    attachEnd(t, edge, vertex);
            } else {
                auto old = t.vertices.at(vertex).outgoing;
                if (old >= 0)
                    attachStart(t, old, -1);
                if (edge >= 0)
                    attachStart(t, edge, vertex);
            }
        }
    }
    return removed;
}
double booleanStraightPathLength(const ConvolutionTopology &t, int32_t first, int32_t last) {
    double result = 0;
    for (auto e : pathEdges(t, first, last))
        result = add(length(t.edges[e]), result);
    return result;
}
double booleanStraightPathArea(const ConvolutionTopology &t, int32_t first, int32_t last) {
    double segments = 0, joints = 0;
    for (auto index : pathEdges(t, first, last)) {
        const auto &e = t.edges[index];
        const auto &a = e.curve.endpoints[0], &b = e.curve.endpoints[1];
        if (e.curve.circle) {
            const auto &circle = *e.curve.circle;
            const double chord = sub(mul(a[0], b[1]), mul(a[1], b[0]));
            const double sector = mul(sub(e.curve.parameters[1], e.curve.parameters[0]), circle.radius);
            const double relativeChord = sub(mul(sub(b[1], circle.center[1]), sub(a[0], circle.center[0])),
                                             mul(sub(a[1], circle.center[1]), sub(b[0], circle.center[0])));
            segments = add(mul(sub(add(add(chord, 0.), sector), relativeChord), .5), segments);
        } else {
            line(e);
            segments = add(mul(sub(mul(b[1], a[0]), mul(a[1], b[0])), .5), segments);
        }
    }
    auto joint = [&](int32_t in, int32_t out, bool closure) {
        if (in < 0 || out < 0)
            return 0.;
        const auto &a = t.edges.at(in).curve.endpoints[1], &b = t.edges.at(out).curve.endpoints[0];
        return mul(sub(mul(b[1], a[0]), closure ? mul(a[1], b[0]) : mul(b[0], a[1])), .5);
    };
    auto e = first;
    size_t count = 0;
    do {
        if (++count > t.edges.size() + 1)
            throw std::invalid_argument("Invalid Boolean area cycle");
        const auto &v = t.vertices.at(t.edges.at(e).last);
        e = v.outgoing;
        joints = add(joints, joint(v.incoming, e, false));
    } while (e >= 0 && e != last);
    if (t.edges.at(first).first >= 0 && t.edges.at(first).first == t.edges.at(last).last) {
        const auto &v = t.vertices.at(t.edges.at(first).first);
        joints = add(joints, joint(v.incoming, v.outgoing, true));
    }
    return add(joints, segments);
}
std::optional<BooleanRebuiltPath> booleanExtractStraightPath(ConvolutionTopology &t, int32_t start,
                                                             uint64_t owner) {
    auto &initial = t.vertices.at(start);
    if (initial.incoming < 0 && initial.outgoing < 0) {
        visit(initial);
        return std::nullopt;
    }
    BooleanRebuiltPath p;
    p.bounds = {initial.point[0], initial.point[1], initial.point[0], initial.point[1]};
    auto consume = [&](int32_t index) {
        auto &e = t.edges.at(index);
        if (!e.curve.line && !e.curve.circle)
            throw std::invalid_argument("Boolean extraction requires a supported curve");
        e.metadata[7] = owner;
        const auto &b = e.curve.bounds;
        for (size_t a = 0; a < 2; ++a) {
            if (b[a] < p.bounds[a])
                p.bounds[a] = b[a];
            if (b[a + 2] > p.bounds[a + 2])
                p.bounds[a + 2] = b[a + 2];
        }
    };
    auto v = start;
    size_t count = 0;
    while (t.vertices.at(v).outgoing >= 0) {
        if (++count > t.edges.size())
            throw std::invalid_argument("Invalid Boolean extraction cycle");
        visit(t.vertices[v]);
        auto e = t.vertices[v].outgoing;
        consume(e);
        v = t.edges[e].last;
        if (v == start)
            break;
    }
    visit(t.vertices.at(v));
    if (t.vertices[v].outgoing < 0) {
        p.lastEdge = t.vertices[v].incoming;
        v = start;
        count = 0;
        while (t.vertices[v].incoming >= 0) {
            if (++count > t.edges.size())
                throw std::invalid_argument("Invalid Boolean extraction backward cycle");
            visit(t.vertices[v]);
            auto e = t.vertices[v].incoming;
            consume(e);
            v = t.edges[e].first;
        }
        visit(t.vertices[v]);
        p.firstEdge = t.vertices[v].outgoing;
    } else {
        p.firstEdge = t.vertices[start].outgoing;
        p.lastEdge = t.vertices[start].incoming;
    }
    p.positive = booleanStraightPathArea(t, p.firstEdge, p.lastEdge) > 0;
    p.signedArea = booleanStraightPathArea(t, p.firstEdge, p.lastEdge);
    p.length = booleanStraightPathLength(t, p.firstEdge, p.lastEdge);
    return p;
}
bool booleanStraightEdgesCoincide(const ConvolutionTopology &t, int32_t first, int32_t second,
                                  double tolerance) {
    const auto a = midpoint(t.edges.at(first)), b = midpoint(t.edges.at(second));
    return std::abs(sub(a[0], b[0])) < tolerance && std::abs(sub(a[1], b[1])) < tolerance;
}
void booleanJoinBoundaryPair(ConvolutionTopology &t, BooleanBoundaryEvent a, BooleanBoundaryEvent b,
                             double tolerance) {
    const auto old = t.edges.at(a.edge).first, target = t.edges.at(b.edge).last,
               displaced = t.vertices.at(target).outgoing;
    attachStart(t, a.edge, target);
    if (displaced >= 0)
        attachStart(t, displaced, old);
    const auto x = a.outgoing ? t.edges.at(a.edge).last : t.edges.at(a.edge).first,
               y = b.outgoing ? t.edges.at(b.edge).last : t.edges.at(b.edge).first;
    const bool same =
        x == y || (group(t.vertices.at(x)) != 0 && group(t.vertices.at(x)) == group(t.vertices.at(y)));
    byte(t.vertices.at(target).metadata[5], 32,
         same && booleanStraightEdgesCoincide(t, a.edge, b.edge, tolerance));
}
void booleanJoinBoundaryEvents(ConvolutionTopology &t, std::span<const BooleanBoundaryEvent> events,
                               double tolerance) {
    std::vector<size_t> remaining;
    for (size_t n = 0; n < events.size(); ++n)
        remaining.push_back(n);
    size_t cursor = 0;
    while (!remaining.empty()) {
        const size_t start = cursor;
        bool found = false;
        do {
            const size_t after = (cursor + 1) % remaining.size();
            if (events[remaining[cursor]].outgoing && !events[remaining[after]].outgoing) {
                found = true;
                break;
            }
            cursor = after;
        } while (cursor != start);
        if (!found)
            return;
        const size_t before = (cursor + remaining.size() - 1) % remaining.size(),
                     after = (cursor + 1) % remaining.size(), resume = remaining[before];
        booleanJoinBoundaryPair(t, events[remaining[cursor]], events[remaining[after]], tolerance);
        if (after > cursor) {
            remaining.erase(remaining.begin() + after);
            remaining.erase(remaining.begin() + cursor);
        } else {
            remaining.erase(remaining.begin() + cursor);
            remaining.erase(remaining.begin() + after);
        }
        if (!remaining.empty())
            cursor = size_t(std::find(remaining.begin(), remaining.end(), resume) - remaining.begin());
    }
}
BooleanRebuiltCollection booleanCollectStraightPaths(ConvolutionTopology &t,
                                                     std::span<BooleanArrangementGroup> groups) {
    BooleanRebuiltCollection result;
    for (auto &entry : groups) {
        for (auto index : entry.vertices)
            byte(t.vertices.at(index).metadata[5], 40, !entry.events.empty());
        for (auto event : entry.events) {
            const auto &edge = t.edges.at(event.edge);
            const auto vertex = event.outgoing ? edge.first : edge.last;
            byte(t.vertices.at(vertex).metadata[5], 40, false);
        }
    }
    for (auto &entry : groups) {
        size_t cursor = 0;
        while (cursor < entry.vertices.size()) {
            const auto vertex = entry.vertices[cursor];
            if ((t.vertices.at(vertex).metadata[5] >> 40) & 255) {
                ++cursor;
                continue;
            }
            auto path = booleanExtractStraightPath(t, vertex, result.paths.size() + 1);
            if (path)
                result.paths.push_back(*path);
        }
    }
    const auto invalid = [&](int32_t index) {
        const auto &edge = t.edges.at(index);
        return edge.first < 0 || edge.last < 0 || edge.first == edge.last ||
               group(t.vertices.at(edge.first)) == group(t.vertices.at(edge.last));
    };
    const auto eraseEdge = [&](int32_t index) {
        const auto &edge = t.edges.at(index);
        if (edge.last >= 0)
            t.vertices.at(edge.last).incoming = -1;
        if (edge.first >= 0)
            t.vertices.at(edge.first).outgoing = -1;
        result.removedEdges.push_back(index);
    };
    for (auto &entry : groups) {
        size_t cursor = 0;
        while (cursor < entry.vertices.size()) {
            const auto index = entry.vertices[cursor];
            auto &vertex = t.vertices.at(index);
            bool removable = true;
            if (vertex.outgoing >= 0) {
                if (invalid(vertex.outgoing))
                    eraseEdge(vertex.outgoing);
                else
                    removable = false;
            }
            if (vertex.incoming >= 0) {
                if (invalid(vertex.incoming))
                    eraseEdge(vertex.incoming);
                else {
                    ++cursor;
                    continue;
                }
            }
            if (!removable) {
                ++cursor;
                continue;
            }
            if (vertex.outgoing >= 0)
                t.edges.at(vertex.outgoing).first = -1;
            if (vertex.incoming >= 0)
                t.edges.at(vertex.incoming).last = -1;
            vertex.alive = false;
            result.removedVertices.push_back(index);
            entry.vertices.erase(entry.vertices.begin() + cursor);
        }
    }
    return result;
}
std::vector<BooleanEdgePair> booleanScanRetracedEdges(ConvolutionTopology &t,
                                                      const BooleanRebuiltPath &p, double tolerance) {
    std::vector<BooleanEdgePair> result;
    auto v = t.edges.at(p.firstEdge).first;
    const auto end = t.edges.at(p.lastEdge).last;
    size_t count = 0;
    do {
        if (++count > t.vertices.size())
            throw std::invalid_argument("Invalid Boolean scan cycle");
        const auto in = t.vertices.at(v).incoming, out = t.vertices.at(v).outgoing;
        if (in < 0 || out < 0)
            return result;
        const auto a = t.edges.at(in).first, b = t.edges.at(out).last;
        if (a < 0 || b < 0)
            return result;
        if ((group(t.vertices.at(a)) != 0 && group(t.vertices.at(a)) == group(t.vertices.at(b))) ||
            a == b) {
            if (booleanStraightEdgesCoincide(t, in, out, tolerance)) {
                mark(t.edges[in], true);
                mark(t.edges[out], true);
                result.push_back({in, out});
            }
        }
        v = t.edges[out].last;
    } while (v != end);
    return result;
}
void booleanMarkRetracedEdges(ConvolutionTopology &t, std::span<const BooleanEdgePair> pairs,
                              double tolerance) {
    for (auto pair : pairs) {
        auto in = previous(t, pair[0]), out = next(t, pair[1]);
        if (marked(t.edges.at(in)) && marked(t.edges.at(out)))
            continue;
        while (true) {
            while (marked(t.edges.at(in))) {
                if (in == pair[0])
                    return;
                in = previous(t, in);
            }
            while (marked(t.edges.at(out))) {
                if (out == pair[1])
                    return;
                out = next(t, out);
            }
            if (in == out) {
                double twice = add(length(t.edges[in]), 0.);
                twice = add(length(t.edges[in]), twice);
                if (tolerance >= twice)
                    mark(t.edges[in], true);
                return;
            }
            const auto &a = t.edges.at(in), &b = t.edges.at(out);
            const auto first = group(t.vertices.at(a.first)), last = group(t.vertices.at(a.last));
            if (!first || first != group(t.vertices.at(b.last)) || !last ||
                last != group(t.vertices.at(b.first)) ||
                !booleanStraightEdgesCoincide(t, in, out, tolerance))
                break;
            mark(t.edges[in], true);
            mark(t.edges[out], true);
            in = previous(t, in);
            out = next(t, out);
        }
    }
}
void booleanClassifyRetracedPath(ConvolutionTopology &t, BooleanRebuiltPath &p, double tolerance) {
    if (!closed(t, p))
        return;
    const auto edges = pathEdges(t, p.firstEdge, p.lastEdge);
    for (auto e : edges)
        mark(t.edges[e], false);
    const auto pairs = booleanScanRetracedEdges(t, p, tolerance);
    booleanMarkRetracedEdges(t, pairs, tolerance);
    for (auto e : edges)
        if (!marked(t.edges[e]))
            return;
    p.positive = false;
}
BooleanAssemblyResult booleanAssembleParts(std::span<BooleanAssemblyNode> nodes,
                                           int32_t initialWinding) {
    BooleanAssemblyResult result;
    int32_t exteriorPart = -1;
    if (initialWinding > 0) {
        exteriorPart = 0;
        result.parts.emplace_back();
    }
    std::vector<bool> active(nodes.size());
    std::vector<int32_t> associations;
    associations.reserve(nodes.size());
    for (const auto &node : nodes)
        associations.push_back(node.part);
    const auto appendPart = [&]() {
        const auto index = static_cast<int32_t>(result.parts.size());
        result.parts.emplace_back();
        return index;
    };
    const auto visitNode = [&](auto &&visitNode, int32_t index, int32_t winding, int32_t part) -> void {
        if (index < 0 || size_t(index) >= nodes.size() || active[index])
            throw std::invalid_argument("Invalid Boolean containment hierarchy");
        active[index] = true;
        auto &node = nodes[index];
        if (!node.positive && winding == 1) {
            // The normal root entry supplies the exterior part for a positive
            // initial winding. A positive ancestor creates all other parts.
            if (part < 0)
                throw std::invalid_argument("Boolean hole has no active part");
            associations[index] = part;
            result.parts.at(part).holes.push_back(node.path);
        } else if (node.positive && winding == 0) {
            part = appendPart();
            associations[index] = part;
            result.parts[part].outer = node.path;
        } else {
            result.discarded.push_back(index);
        }
        const uint32_t delta = node.positive ? 1U : UINT32_MAX;
        const auto childWinding = std::bit_cast<int32_t>(std::bit_cast<uint32_t>(winding) + delta);
        for (const auto child : node.children)
            visitNode(visitNode, child, childWinding, part);
        active[index] = false;
    };
    for (size_t index = 0; index < nodes.size(); ++index)
        if (nodes[index].parent < 0)
            visitNode(visitNode, static_cast<int32_t>(index), initialWinding, exteriorPart);
    for (size_t index = 0; index < nodes.size(); ++index)
        nodes[index].part = associations[index];
    return result;
}
std::optional<std::vector<BooleanStraightPart>>
booleanArrangeStraightPaths(ConvolutionTopology graph, std::span<const BooleanEdgePair> paths,
                            double tolerance, ConvolutionCurves &curves, int32_t initialWinding,
                            bool reverse, bool closedOnly) {
    std::vector<BooleanEdgePair> anchors(paths.begin(), paths.end());
    const auto reversePath = [&](int32_t &first, int32_t &last) {
        graph.firstEdge = first;
        graph.lastEdge = last;
        convolutionReverse(graph);
        first = graph.firstEdge;
        last = graph.lastEdge;
    };
    if (reverse)
        for (auto &path : anchors)
            reversePath(path[0], path[1]);
    // Defined fields initialized by 7e6d50. Its two trailing padding bytes in
    // each metadata record are indeterminate stack data; preserve caller bytes.
    for (const auto &path : anchors) {
        if (path[0] < 0)
            continue;
        for (auto index : pathEdges(graph, path[0], path[1])) {
            auto &edge = graph.edges[index];
            edge.metadata[7] = edge.metadata[8] = 0;
            edge.metadata[9] = (edge.metadata[9] & 0xffff000000000000ULL) | 0xffffffffffULL;
            for (auto vertex : {edge.first, edge.last}) {
                auto &metadata = graph.vertices.at(vertex).metadata;
                metadata[4] = 0;
                metadata[5] = (metadata[5] & 0xffff000000000000ULL) | UINT32_MAX;
            }
        }
    }
    auto input = booleanProduceStraightGroups(graph, anchors, tolerance, curves);
    booleanPrepareBoundaryEvents(graph, input.groups);
    for (const auto &group : input.groups)
        booleanJoinBoundaryEvents(graph, group.events, tolerance);
    auto collection = booleanCollectStraightPaths(graph, input.groups);
    std::vector<BooleanContainmentNode> nodes;
    for (auto &path : collection.paths) {
        booleanClassifyRetracedPath(graph, path, tolerance);
        nodes.push_back({path, nodes.size() + 1});
    }
    if (!booleanResolvePathHierarchy(graph, input.groups, nodes))
        return std::nullopt;
    booleanResolveDetachedHierarchy(graph, input.groups, nodes, tolerance, {input.state.sequence});
    if (reverse)
        for (auto &node : nodes) {
            reversePath(node.path.firstEdge, node.path.lastEdge);
            node.path.positive = !node.path.positive;
        }
    std::vector<int32_t> indices(nodes.size(), -1);
    int32_t count = 0;
    for (size_t index = 0; index < nodes.size(); ++index)
        if (!closedOnly || closed(graph, nodes[index].path))
            indices[index] = count++;
    std::vector<BooleanAssemblyNode> assembly;
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (indices[index] < 0)
            continue;
        const auto &node = nodes[index];
        auto path = std::make_shared<ConvolutionTopology>();
        std::map<int32_t, int32_t> vertices;
        for (auto edgeIndex : pathEdges(graph, node.path.firstEdge, node.path.lastEdge)) {
            auto edge = graph.edges[edgeIndex];
            for (auto endpoint : {&edge.first, &edge.last}) {
                auto [found, inserted] = vertices.try_emplace(*endpoint, int32_t(vertices.size()));
                if (inserted) {
                    auto vertex = graph.vertices.at(*endpoint);
                    vertex.incoming = vertex.outgoing = -1;
                    path->vertices.push_back(vertex);
                }
                *endpoint = found->second;
            }
            const auto newIndex = int32_t(path->edges.size());
            path->vertices[edge.first].outgoing = newIndex;
            path->vertices[edge.last].incoming = newIndex;
            path->edges.push_back(std::move(edge));
        }
        path->firstEdge = 0;
        path->lastEdge = int32_t(path->edges.size()) - 1;
        std::vector<int32_t> children;
        for (auto child : node.children)
            children.push_back(indices.at(child));
        assembly.push_back({path, node.path.positive, node.parent < 0 ? -1 : indices.at(node.parent),
                            std::move(children)});
    }
    return booleanAssembleParts(assembly, initialWinding).parts;
}
std::array<double, 4> booleanPathBounds(const ConvolutionTopology &graph, BooleanEdgePair anchors) {
    auto bounds = graph.edges.at(anchors[0]).curve.bounds;
    const auto extend = [&](const std::array<double, 4> &other) {
        for (size_t axis = 0; axis < 2; ++axis) {
            if (other[axis] < bounds[axis])
                bounds[axis] = other[axis];
            if (other[axis + 2] > bounds[axis + 2])
                bounds[axis + 2] = other[axis + 2];
        }
    };
    for (const auto index : pathEdges(graph, anchors[0], anchors[1])) {
        const auto &edge = graph.edges[index];
        extend(edge.curve.bounds);
        if (edge.first >= 0) {
            const auto point = graph.vertices.at(edge.first).point;
            extend({point[0], point[1], point[0], point[1]});
        }
    }
    const auto last = graph.edges.at(anchors[1]).last;
    if (last >= 0) {
        const auto point = graph.vertices.at(last).point;
        extend({point[0], point[1], point[0], point[1]});
    }
    return bounds;
}
double booleanInputPathsScale(const ConvolutionTopology &graph,
                              std::span<const BooleanEdgePair> paths) {
    if (paths.empty())
        return 1.;
    auto bounds = booleanPathBounds(graph, paths.front());
    for (const auto &path : paths.subspan(1)) {
        const auto other = booleanPathBounds(graph, path);
        for (size_t axis = 0; axis < 2; ++axis) {
            if (other[axis] < bounds[axis])
                bounds[axis] = other[axis];
            if (other[axis + 2] > bounds[axis + 2])
                bounds[axis + 2] = other[axis + 2];
        }
    }
    const auto width = sub(bounds[2], bounds[0]), height = sub(bounds[3], bounds[1]);
    // MAXSD selects the second operand for equality and unordered operands.
    return height > width ? height : width;
}
BooleanEdgePair booleanPrepareCircularPath(ConvolutionTopology &graph, BooleanEdgePair anchors,
                                           ConvolutionCurves &curves) {
    InputSweep preparation{graph, curves, 0.};
    const auto edges = pathEdges(graph, anchors[0], anchors[1]);
    const auto firstVertex = graph.edges.at(anchors[0]).first;
    const auto lastVertex = graph.edges.at(anchors[1]).last;
    for (const auto index : edges) {
        const auto source = graph.edges.at(index);
        if (!source.curve.line && !source.curve.circle)
            throw std::invalid_argument("Boolean input requires a supported curve");
        if (source.curve.circle && 2.0943951023931953 <
            div(std::abs(sub(source.curve.parameters[0], source.curve.parameters[1])), source.curve.circle->radius)) {
            const double firstMiddle = mul(add(source.curve.parameters[0], source.curve.parameters[1]), .5);
            const double secondMiddle = mul(add(source.curve.parameters[1], source.curve.parameters[0]), .5);
            const auto vertex = preparation.splitVertex(source.curve, firstMiddle);
            preparation.splitSegment(source, source.first, vertex, source.curve.parameters[0], firstMiddle);
            preparation.splitSegment(source, vertex, source.last, secondMiddle, source.curve.parameters[1]);
        }
    }
    return {graph.vertices.at(firstVertex).outgoing, graph.vertices.at(lastVertex).incoming};
}
void booleanMergeStraightPathSegments(ConvolutionTopology &graph, ConvolutionCurves &curves) {
    const auto mergeRange = [&](int32_t seed) {
        const auto &source = graph.edges.at(seed);
        if (!source.curve.line && !source.curve.circle)
            throw std::invalid_argument("Boolean path merging requires a supported curve");
        const auto lineCurve = source.curve.line;
        const auto circleCurve = source.curve.circle;
        const auto sameCurve = [&](int32_t index) {
            const auto &curve = graph.edges.at(index).curve;
            return curve.line == lineCurve && curve.circle == circleCurve;
        };
        const auto classification = source.metadata[3];
        auto first = seed, last = seed;
        for (auto candidate = previous(graph, first);
             candidate >= 0 && candidate != seed && sameCurve(candidate) &&
             graph.edges.at(candidate).metadata[3] == classification;
             candidate = previous(graph, first))
            first = candidate;
        for (auto candidate = next(graph, last);
             candidate >= 0 && candidate != first && sameCurve(candidate) &&
             graph.edges.at(candidate).metadata[3] == classification;
             candidate = next(graph, last))
            last = candidate;
        if (first == last)
            return seed;
        const auto retired = pathEdges(graph, first, last);
        const auto start = graph.edges[first].first, end = graph.edges[last].last;
        auto replacement = source;
        replacement.curve = curves.range(source.curve, graph.edges[first].curve.parameters[0],
                                          graph.edges[last].curve.parameters[1]);
        replacement.first = replacement.last = -1;
        const auto index = int32_t(graph.edges.size());
        graph.edges.push_back(std::move(replacement));
        attachStart(graph, index, start);
        attachEnd(graph, index, end);
        // The original detaches the surviving endpoints before destroying the
        // old range. Retired nodes keep payload snapshots, as in arrangement.
        for (const auto old : retired) {
            const auto vertex = graph.edges[old].first;
            if (vertex >= 0) {
                auto &point = graph.vertices.at(vertex);
                if (point.outgoing >= 0)
                    graph.edges.at(point.outgoing).first = -1;
                if (point.incoming >= 0)
                    graph.edges.at(point.incoming).last = -1;
                point.alive = false;
            }
            const auto &edge = graph.edges[old];
            if (edge.last >= 0)
                graph.vertices.at(edge.last).incoming = -1;
            if (edge.first >= 0)
                graph.vertices.at(edge.first).outgoing = -1;
        }
        return index;
    };
    graph.firstEdge = mergeRange(graph.firstEdge);
    auto last = graph.firstEdge;
    for (auto edge = next(graph, last); edge >= 0 && edge != graph.firstEdge;
         edge = next(graph, last))
        last = mergeRange(edge);
    graph.lastEdge = last;
}
std::optional<std::vector<BooleanStraightPart>>
booleanArrangeStraightParts(ConvolutionTopology graph, std::span<const BooleanEdgePair> paths,
                            double tolerance, ConvolutionCurves &curves, int32_t initialWinding,
                            bool reverse, bool closedOnly) {
    if (tolerance == 0.)
        tolerance = mul(booleanInputPathsScale(graph, paths), 1e-7);
    std::vector<BooleanEdgePair> anchors(paths.begin(), paths.end());
    for (auto &path : anchors) {
        path = booleanPrepareCircularPath(graph, path, curves);
    }
    auto result = booleanArrangeStraightPaths(std::move(graph), anchors, tolerance, curves,
                                               initialWinding, reverse, closedOnly);
    if (result)
        for (auto &part : *result) {
            if (part.outer)
                booleanMergeStraightPathSegments(*part.outer, curves);
            for (auto &hole : part.holes)
                booleanMergeStraightPathSegments(*hole, curves);
        }
    return result;
}
std::optional<std::vector<BooleanStraightPart>>
booleanDifferenceStraightParts(std::optional<std::vector<BooleanStraightPart>> first,
                               std::optional<std::vector<BooleanStraightPart>> second,
                               double tolerance, ConvolutionCurves &curves, bool reverse) {
    if (!first || !second)
        return first;
    ConvolutionTopology graph;
    std::vector<BooleanEdgePair> paths;
    uint32_t winding = 0;
    const auto append = [&](const ConvolutionTopology &input, bool reversed) {
        auto path = input;
        if (reversed)
            convolutionReverse(path);
        const auto edgeOffset = int32_t(graph.edges.size()),
                   vertexOffset = int32_t(graph.vertices.size());
        paths.push_back({path.firstEdge + edgeOffset, path.lastEdge + edgeOffset});
        for (auto vertex : path.vertices) {
            if (vertex.incoming >= 0)
                vertex.incoming += edgeOffset;
            if (vertex.outgoing >= 0)
                vertex.outgoing += edgeOffset;
            graph.vertices.push_back(vertex);
        }
        for (auto edge : path.edges) {
            if (edge.first >= 0)
                edge.first += vertexOffset;
            if (edge.last >= 0)
                edge.last += vertexOffset;
            graph.edges.push_back(std::move(edge));
        }
    };
    for (const auto subtract : {false, true})
        for (const auto &part : subtract ? *second : *first) {
            if (part.outer)
                append(*part.outer, subtract);
            else
                winding += subtract ? UINT32_MAX : 1U;
            for (const auto &hole : part.holes)
                append(*hole, subtract);
        }
    return booleanArrangeStraightParts(std::move(graph), paths, tolerance, curves,
                                        std::bit_cast<int32_t>(winding), reverse, false);
}
} // namespace study::recovered
