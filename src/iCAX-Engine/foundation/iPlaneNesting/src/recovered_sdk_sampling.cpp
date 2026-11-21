#include "recovered_sdk_sampling.hpp"
#include "recovered_rich_sampling.hpp"
#include "recovered_sdk_points.hpp"
#include <cmath>
#include <stdexcept>

namespace study::recovered {
bool sdkMergeableLines(const std::array<ShakerPoint, 2> &previous,
                       const std::array<ShakerPoint, 2> &current,
                       ShakerPoint point, double tolerance) {
    const double dx = current[1][0] - previous[0][0], dy = current[1][1] - previous[0][1];
    const double px = point[0] - previous[0][0], py = point[1] - previous[0][1];
    const double dot = px * dx + py * dy, squared = dx * dx + dy * dy;
    if (!(dot >= 0.) || squared < dot || squared < 1e-12) return false;
    const double ex = (dot / squared) * dx + previous[0][0] - point[0];
    const double ey = (dot / squared) * dy + previous[0][1] - point[1];
    return std::sqrt(ey * ey + ex * ex) < tolerance;
}
void sdkCleanSampledCurve(RichGraph &graph, RichNode &curve, double tolerance) {
    if (curve.kind != RichKind::Curve) throw std::invalid_argument("SDK cleanup requires a curve");
    const auto mutableNode = [](const RichNode *node) { return const_cast<RichNode *>(node); };
    auto vertex = curve.first;
    size_t visited = 0;
    const size_t limit = graph.nodes.size() + 1;
    while (vertex && vertex->next) {
        if (++visited > limit) throw std::invalid_argument("Invalid sampled curve cycle");
        const auto current = vertex->next;
        const auto nextVertex = current->next;
        const auto previous = vertex->previous;
        if (previous && previous != current && previous->line && current->line &&
            sdkMergeableLines(previous->endpoints, current->endpoints, vertex->point, tolerance)) {
            const auto startVertex = previous->previous;
            if (!startVertex || !nextVertex) throw std::invalid_argument("Invalid sampled edge range");
            auto &replacement = richStartCurve(graph, previous->endpoints[0], 1e-6);
            auto edge = richAppendLine(graph, replacement, current->endpoints[1]);
            if (edge) {
                const double startX = replacement.first->point[0] - previous->endpoints[0][0];
                const double startY = replacement.first->point[1] - previous->endpoints[0][1];
                const double endX = replacement.last->point[0] - current->endpoints[1][0];
                const double endY = replacement.last->point[1] - current->endpoints[1][1];
                if (!(curve.tolerance < std::sqrt(startY * startY + startX * startX)) &&
                    !(curve.tolerance < std::sqrt(endY * endY + endX * endX))) {
                    if (curve.last && curve.last->next) curve.first = curve.last = startVertex;
                    richAttach(*mutableNode(startVertex), edge);
                    richAttach(*edge, mutableNode(nextVertex));
                    replacement.first = replacement.last;
                }
            }
        }
        vertex = nextVertex;
        if (vertex == curve.last) break;
    }
}
void sdkRemoveDuplicatePoints(std::vector<ShakerPoint> &points) {
    if (points.size() < 2) return;
    const auto near = [](ShakerPoint a, ShakerPoint b) {
        return std::abs(a[0] - b[0]) < .001 && std::abs(a[1] - b[1]) < .001;
    };
    size_t written = 1;
    for (size_t index = 1; index < points.size(); ++index)
        if (!near(points[written - 1], points[index])) points[written++] = points[index];
    points.resize(written);
    if (near(points.back(), points.front())) points.pop_back();
}
std::vector<ShakerPoint> sdkSampleContour(const RichNode &source, bool exterior, double precision) {
    if (source.kind != RichKind::Curve || !source.first || !source.last || !source.last->next)
        throw std::invalid_argument("SDK sampling requires a closed contour");
    const auto box = richStraightBounds(source);
    if (box.empty || !(precision > 0.) || !std::isfinite(precision))
        throw std::invalid_argument("Invalid SDK sampling precision");
    const double dimension = std::min(std::abs(box.coordinates[2] - box.coordinates[0]),
                                      std::abs(box.coordinates[3] - box.coordinates[1]));
    const double error = dimension * (exterior ? precision : -precision);
    if (!(dimension > 0.) || !std::isfinite(error))
        throw std::invalid_argument("Degenerate SDK contour bounds");
    RichGraph graph;
    auto &curve = richStartCurve(graph, source.first->point, source.tolerance);
    richVisit(source, {[&](RichEvent event, const RichNode &edge) {
        if (event != RichEvent::Edge) return;
        if (edge.circle) {
            if (!edge.parameters) throw std::invalid_argument("SDK arc requires parameters");
            for (const auto point : richDiscretizeArc(*edge.circle, *edge.parameters, error))
                richAppendLine(graph, curve, point);
        } else if (!edge.line) throw std::invalid_argument("Unsupported SDK curve");
        if (!edge.next) throw std::invalid_argument("SDK contour has an open edge");
        richAppendLine(graph, curve, edge.next->point);
    }, {}});
    richClose(curve);
    sdkCleanSampledCurve(graph, curve, 1e-5);
    std::vector<ShakerPoint> points;
    richVisit(curve, {[&](RichEvent event, const RichNode &node) {
        if (event == RichEvent::Vertex) points.push_back(node.point);
    }, {}});
    sdkNormalizePoints(points, exterior);
    // The SDK polygon consumer applies the outer/hole role separately. Keep
    // this shared Polygon adapter's established positive outer-ring contract.
    sdkOrientPoints(points, true);
    return points;
}
}
