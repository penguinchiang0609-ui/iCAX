#include "recovered_rich_conversion.hpp"
#include "recovered_boolean_arrangement.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace study::recovered {
namespace {
RichNode *mutableNode(const RichNode *p){return const_cast<RichNode*>(p);}
bool within(ShakerPoint evaluated,ShakerPoint point,double tolerance){
    return tolerance>=std::abs(evaluated[0]-point[0])&&tolerance>=std::abs(evaluated[1]-point[1]);
}
}
RichNode &RichGraph::add(RichKind kind){auto node=std::make_unique<RichNode>();node->kind=kind;node->identity=nodes.size();auto &result=*node;nodes.push_back(std::move(node));return result;}
RichNode &richStartCurve(RichGraph &graph,ShakerPoint point,double tolerance){auto &curve=graph.add(RichKind::Curve);curve.tolerance=tolerance;auto &vertex=graph.add(RichKind::Vertex);vertex.point=point;curve.first=curve.last=&vertex;return curve;}
void richAttach(RichNode &node,RichNode *next){
    if(node.next)mutableNode(node.next)->previous=nullptr;
    node.next=next;
    if(next){if(next->previous)mutableNode(next->previous)->next=nullptr;next->previous=&node;}
}
RichNode *richAppendLine(RichGraph &graph,RichNode &curve,ShakerPoint point){
    if(curve.kind!=RichKind::Curve)throw std::invalid_argument("Rich append requires a curve");
    if(!curve.last||curve.last->next)return nullptr;
    auto &edge=graph.add(RichKind::Edge);
    const auto segment = richStraightSegment(curve.last->point,point);
    edge.endpoints=segment.endpoints;
    edge.parameters=std::array<double, 2>{0., segment.length};
    auto &vertex=graph.add(RichKind::Vertex);vertex.point=point;
    if(!within(edge.endpoints[1],point,curve.tolerance))return nullptr;
    richAttach(edge,&vertex);
    if(!within(edge.endpoints[0],curve.last->point,curve.tolerance))return nullptr;
    richAttach(*mutableNode(curve.last),&edge);curve.last=&vertex;return &edge;
}
RichNode *richAppendArc(RichGraph &graph, RichNode &curve, ShakerPoint point, ShakerPoint center,
                        bool positive) {
    if (curve.kind != RichKind::Curve) throw std::invalid_argument("Rich append requires a curve");
    if (!curve.last || curve.last->next) return nullptr;
    const auto segment = richArcSegment(curve.last->point, point, center, positive);
    auto &edge = graph.add(RichKind::Edge);
    edge.line = false;
    edge.circle = segment.circle;
    edge.endpoints = segment.endpoints;
    edge.parameters = segment.parameters;
    auto &vertex = graph.add(RichKind::Vertex);
    vertex.point = point;
    if (!within(edge.endpoints[1], point, curve.tolerance)) return nullptr;
    richAttach(edge, &vertex);
    if (!within(edge.endpoints[0], curve.last->point, curve.tolerance)) return nullptr;
    richAttach(*mutableNode(curve.last), &edge);
    curve.last = &vertex;
    return &edge;
}
bool richClose(RichNode &curve){
    if(curve.kind!=RichKind::Curve)throw std::invalid_argument("Rich close requires a curve");
    if(!curve.first||!curve.last||curve.first==curve.last)return false;
    auto edge=mutableNode(curve.last->previous);
    if(!edge||edge->kind!=RichKind::Edge)throw std::invalid_argument("Rich close requires a live incoming edge");
    if(!within(edge->endpoints[1],curve.first->point,curve.tolerance))return false;
    richAttach(*edge,mutableNode(curve.first));curve.last=curve.first;return true;
}
void richReverse(RichNode &curve){
    if(curve.kind!=RichKind::Curve)throw std::invalid_argument("Rich reverse requires a curve");
    auto vertex=mutableNode(curve.first);
    while(vertex){auto edge=mutableNode(vertex->next);std::swap(vertex->next,vertex->previous);if(!edge)break;
        auto next=mutableNode(edge->next);std::swap(edge->next,edge->previous);std::swap(edge->endpoints[0],edge->endpoints[1]);
        if (edge->parameters) std::swap((*edge->parameters)[0], (*edge->parameters)[1]);
        if(next==curve.first)break;vertex=next;
    }
    std::swap(curve.first,curve.last);
}
bool richSetOuter(RichNode &polygon,RichNode *curve){
    if(polygon.kind!=RichKind::Polygon)throw std::invalid_argument("Rich outer requires a polygon");
    if(curve){if((!curve->last||!curve->last->next)&&!richClose(*curve))return false;if(!(richStraightCurveArea(*curve)>0))richReverse(*curve);}
    polygon.outer=curve;return true;
}
void richAppendHole(RichNode &polygon,RichNode *curve){
    if(polygon.kind!=RichKind::Polygon)throw std::invalid_argument("Rich hole requires a polygon");
    if(curve){if(curve->last&&curve->last->next&&richStraightCurveArea(*curve)>0)richReverse(*curve);polygon.holes.push_back(curve);}
}
RichNode *booleanStraightPathToRich(RichGraph &graph,const ConvolutionTopology *source,double tolerance,
                                  const RichSourceAttributes *attributes){
    if(!source)return nullptr;
    if(source->firstEdge<0||source->lastEdge<0)throw std::invalid_argument("Boolean conversion requires a nonempty path");
    RichNode *curve=nullptr;auto current=source->firstEdge;size_t visited=0;
    while(current>=0){
        if(++visited>source->edges.size())throw std::invalid_argument("Boolean conversion path cycle");
        const auto &edge=source->edges.at(current);
        if(!curve)curve=&richStartCurve(graph,edge.curve.endpoints[0],tolerance);
        RichNode *converted = nullptr;
        if (edge.curve.circle) {
            const auto &a = edge.curve.endpoints[0], &b = edge.curve.endpoints[1];
            const double x = a[0] - b[0], y = a[1] - b[1];
            const double chord = std::sqrt(y * y + x * x);
            const double radius = std::abs(edge.curve.circle->radius);
            double ratio = (chord * .5) / radius;
            if (ratio < -1.) ratio = -1.;
            if (ratio > 1.) ratio = 1.;
            const double angle = std::asin(ratio);
            if (radius != 0 && angle + angle > 1e-10)
                converted = richAppendArc(graph, *curve, b, edge.curve.circle->center,
                                           edge.curve.parameters[1] > edge.curve.parameters[0]);
            else
                converted = richAppendLine(graph, *curve, b);
        } else {
            if (!edge.curve.line) throw std::invalid_argument("Boolean conversion requires a supported curve");
            converted = richAppendLine(graph, *curve, edge.curve.endpoints[1]);
        }
        const auto label = std::bit_cast<int32_t>(uint32_t(edge.metadata[3]));
        if (converted && attributes && label != 0) {
            const auto found = attributes->entries.find(label);
            if (found != attributes->entries.end()) {
                RichAttributeCopies copies;
                richCopyAttributes(converted->attributes, found->second, copies);
            }
        }
        current=current==source->lastEdge?-1:source->vertices.at(edge.last).outgoing;
    }
    if(source->vertices.at(source->edges.at(source->lastEdge).last).outgoing==source->firstEdge){
        richClose(*curve);
        if(!curve->last||!curve->last->next){richAppendLine(graph,*curve,curve->first->point);richClose(*curve);}
    }
    return curve;
}
RichNode &booleanStraightPartsToRich(RichGraph &graph,std::span<const BooleanStraightPart> parts,double tolerance,
                                   const RichSourceAttributes *attributes){
    auto &root=graph.add(RichKind::Root);
    for(const auto &part:parts){auto &polygon=graph.add(RichKind::Polygon);richSetOuter(polygon,booleanStraightPathToRich(graph,part.outer.get(),tolerance,attributes));
        for(const auto &hole:part.holes)richAppendHole(polygon,booleanStraightPathToRich(graph,hole.get(),tolerance,attributes));root.children.push_back(&polygon);
    }
    return root;
}
std::shared_ptr<ConvolutionTopology> richStraightPathToBoolean(const RichNode &curve,
                                                             ConvolutionCurves &curves,
                                                             RichSourceAttributes *attributes) {
    if (curve.kind != RichKind::Curve)
        throw std::invalid_argument("Boolean path conversion requires a rich curve");
    std::shared_ptr<ConvolutionTopology> path;
    const bool closed = curve.last && curve.last->next;
    richVisit(curve, {[&](RichEvent event, const RichNode &edge) {
        if (event != RichEvent::Edge)
            return;
        const auto source = attributes ? attributes->insert(edge.attributes) : 0;
        if (!edge.line && !edge.circle)
            throw std::invalid_argument("Boolean path conversion requires a supported segment");
        const auto length = edge.parameters
            ? std::abs((*edge.parameters)[1] - (*edge.parameters)[0])
            : richStraightSegment(edge.endpoints[0], edge.endpoints[1]).length;
        if (length <= 1e-6)
            return;
        if (!path) {
            path = std::make_shared<ConvolutionTopology>();
            path->cursor = edge.endpoints[0];
        }
        auto endpoint = edge.endpoints[1];
        if (closed && edge.next == curve.first) {
            if (!curve.first || !curve.first->next)
                throw std::invalid_argument("Closed rich curve has no first segment");
            endpoint = curve.first->next->endpoints[0];
        }
        if (edge.circle) {
            if (!edge.parameters) throw std::invalid_argument("Boolean arc conversion requires parameters");
            const auto parameters = *edge.parameters;
            const double middleParameter = (parameters[0] + parameters[1]) * .5;
            const auto middleTangent = richArcDirectedTangent(*edge.circle, parameters, middleParameter);
            const auto firstTangent = richArcDirectedTangent(*edge.circle, parameters, parameters[0]);
            const double angle = std::atan2(middleTangent[1] * firstTangent[0] - middleTangent[0] * firstTangent[1],
                                           firstTangent[0] * middleTangent[0] + middleTangent[1] * firstTangent[1]);
            const double radius = (parameters[1] > parameters[0] ? 1. : -1.) * edge.circle->radius;
            const ConvolutionCurveFactory factory = [&](ShakerPoint first, ShakerPoint last) {
                return curves.copy(curves.createRadiusArc(first, last, radius));
            };
            if (std::abs(angle + angle) > 3.141592653589793) {
                const auto middle = richCircleSample(*edge.circle, middleParameter).point;
                const auto index = convolutionAppend(*path, middle, factory);
                path->edges[index].metadata[3] = uint32_t(source);
            }
            const auto index = convolutionAppend(*path, endpoint, factory);
            path->edges[index].metadata[3] = uint32_t(source);
        } else {
            const auto index = convolutionAppend(*path, endpoint, curves.edgeFactory());
            path->edges[index].metadata[3] = uint32_t(source);
        }
    }, {}});
    if (path && closed && !convolutionMergeClosure(*path)) {
        if (convolutionClose(*path, curves.edgeFactory()))
            path->edges[path->lastEdge].metadata[3] &= 0xffffffff00000000ULL;
    }
    return path;
}
std::vector<BooleanStraightPart> richStraightTreeToBoolean(const RichNode &tree,
                                                          ConvolutionCurves &curves,
                                                          RichSourceAttributes *attributes) {
    std::vector<BooleanStraightPart> parts;
    richVisit(tree, {[&](RichEvent event, const RichNode &polygon) {
        if (event != RichEvent::PolygonBegin)
            return;
        BooleanStraightPart part;
        richVisit(polygon, {[&](RichEvent childEvent, const RichNode &curve) {
            if (childEvent != RichEvent::CurveBegin)
                return;
            auto path = richStraightPathToBoolean(curve, curves, attributes);
            if (!path)
                return;
            const auto area = booleanStraightPathArea(*path, path->firstEdge, path->lastEdge);
            if (!part.outer) {
                if (!(area > 0.))
                    convolutionReverse(*path);
                part.outer = std::move(path);
            } else {
                if (area > 0.)
                    convolutionReverse(*path);
                part.holes.push_back(std::move(path));
            }
        }, {}});
        if (part.outer)
            parts.push_back(std::move(part));
    }, {}});
    return parts;
}
RichNode &richDifferenceStraightTrees(RichGraph &output, const RichNode &first,
                                     const RichNode &second, double tolerance,
                                     ConvolutionCurves &curves) {
    RichSourceAttributes attributes;
    auto left = richStraightTreeToBoolean(first, curves, &attributes);
    auto right = richStraightTreeToBoolean(second, curves, &attributes);
    auto result = booleanDifferenceStraightParts(std::move(left), std::move(right), tolerance, curves);
    if (!result)
        return booleanStraightPartsToRich(output, {}, 1e-6);
    booleanFinishStraightParts(*result, tolerance, curves.edgeFactory(), curves.arcFactory());
    return booleanStraightPartsToRich(output, *result, 1e-6, &attributes);
}
RichBounds richStraightBounds(const RichNode &node) {
    RichBounds bounds;
    const auto extend = [&](ShakerPoint point) {
        auto &values = bounds.coordinates;
        if (bounds.empty) {
            values = {point[0], point[1], point[0], point[1]};
            bounds.empty = false;
            return;
        }
        for (size_t axis = 0; axis < 2; ++axis) {
            if (point[axis] < values[axis])
                values[axis] = point[axis];
            if (point[axis] > values[axis + 2])
                values[axis + 2] = point[axis];
        }
    };
    const auto include = [&](const RichNode *child) {
        if (!child)
            return;
        const auto other = richStraightBounds(*child);
        if (!other.empty) {
            extend({other.coordinates[0], other.coordinates[1]});
            extend({other.coordinates[2], other.coordinates[3]});
        }
    };
    switch (node.kind) {
    case RichKind::Root:
        for (auto child : node.children)
            include(child);
        break;
    case RichKind::Polygon:
        include(node.outer);
        for (auto hole : node.holes)
            include(hole);
        for (auto auxiliary : node.auxiliary)
            include(auxiliary);
        break;
    case RichKind::Curve: {
        if (!node.first)
            throw std::invalid_argument("Rich curve bounds require a first vertex");
        extend(node.first->point);
        for (auto edge = node.first->next; edge; edge = edge->next->next) {
            include(edge);
            extend(edge->next->point);
            if (edge->next == node.first)
                break;
        }
        break;
    }
    case RichKind::Vertex:
        extend(node.point);
        break;
    case RichKind::Edge:
        extend(node.endpoints[0]);
        extend(node.endpoints[1]);
        if (node.circle) {
            if (!node.parameters) throw std::invalid_argument("Rich arc bounds require parameters");
            for (const auto parameter : richCircleExtremaParameters(*node.circle, (*node.parameters)[0], (*node.parameters)[1]))
                extend(richCircleSample(*node.circle, parameter).point);
        } else if (!node.line) {
            throw std::invalid_argument("Rich bounds require a supported segment");
        }
        break;
    }
    return bounds;
}
RichNode &shakerBuildRichPolygonInput(RichGraph &graph, const ShakerPolygonInput &input) {
    const auto buildCurve = [&](std::span<const ShakerPoint> points) -> RichNode & {
        if (points.empty())
            throw std::invalid_argument("SDK rich polygon requires nonempty contours");
        auto &curve = richStartCurve(graph, points.front(), 1e-6);
        for (auto point : points.subspan(1))
            richAppendLine(graph, curve, point);
        richAppendLine(graph, curve, points.front());
        richClose(curve);
        return curve;
    };
    auto &polygon = graph.add(RichKind::Polygon);
    richSetOuter(polygon, &buildCurve(input.outer));
    for (const auto &hole : input.holes)
        richAppendHole(polygon, &buildCurve(hole));
    auto &root = graph.add(RichKind::Root);
    root.children.push_back(&polygon);
    return root;
}
RichNode &shakerBuildHoleCuts(RichGraph &graph, const RichNode &input) {
    if (input.kind != RichKind::Root || input.children.empty() ||
        input.children.front()->kind != RichKind::Polygon)
        throw std::invalid_argument("Hole cuts require the SDK polygon input tree");
    std::vector<std::array<double, 4>> holes;
    for (auto hole : input.children.front()->holes)
        holes.push_back(richStraightBounds(*hole).coordinates);
    const auto levels = shakerHoleCutLevels(holes);
    const auto bounds = richStraightBounds(input).coordinates;
    auto &root = graph.add(RichKind::Root);
    const double width = bounds[2] - bounds[0], left = bounds[0] - width, right = bounds[2] + width;
    for (auto level : levels) {
        auto &curve = richStartCurve(graph, {left, level}, 1e-6);
        richAppendLine(graph, curve, {right, level});
        richAppendLine(graph, curve, {left, level});
        richClose(curve);
        auto &polygon = graph.add(RichKind::Polygon);
        richSetOuter(polygon, &curve);
        root.children.push_back(&polygon);
    }
    return root;
}
size_t shakerAppendPolygonComponents(ShakerInputScene &scene, size_t shape,
                                     const ShakerPolygonInput &input, ConvolutionCurves &curves) {
    RichGraph graph;
    const auto &source = shakerBuildRichPolygonInput(graph, input);
    const auto &cuts = shakerBuildHoleCuts(graph, source);
    const auto &normalized = richDifferenceStraightTrees(graph, source, cuts, 1e-6, curves);
    return shakerAppendVisitedStraightTree(scene, shape, input.type, normalized, input.offset);
}
}
