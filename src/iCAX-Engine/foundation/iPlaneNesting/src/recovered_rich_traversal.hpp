#pragma once
#include "recovered_shaker_geometry_input.hpp"
#include "recovered_rich_attributes.hpp"

namespace study::recovered {
enum class RichKind { Root, Polygon, Curve, Vertex, Edge };
enum class RichEvent { RootBegin, RootEnd, PolygonBegin, PolygonEnd, CurveBegin, CurveEnd, Vertex, Edge, Line, OtherCurve };
struct RichCircle {
    ShakerPoint center{}, radial{};
    double radius = 0;
};
struct RichCircleSample {
    ShakerPoint point{}, tangent{}, normal{};
};
struct RichArcSegment {
    RichCircle circle;
    std::array<double, 2> parameters{};
    std::array<ShakerPoint, 2> endpoints{};
};
// Non-owning view of an already constructed rich-geometry graph. The caller
// retains all nodes throughout traversal. This is not the Boolean-to-rich
// converter: raw SDK rings must not be passed off as Boolean output here.
struct RichNode {
    RichKind kind=RichKind::Root;
    size_t identity=0;
    ShakerPoint point{};
    std::array<ShakerPoint,2> endpoints{}; // Evaluated segment endpoints; distinct from topology vertices.
    // Original segment parameters, when available. Legacy views without these
    // values derive straight length from their evaluated endpoint pair.
    std::optional<std::array<double, 2>> parameters;
    double tolerance=0;
    bool line=true;
    std::optional<RichCircle> circle;
    const RichNode *outer=nullptr,*first=nullptr,*last=nullptr,*next=nullptr,*previous=nullptr;
    std::vector<const RichNode*> children,holes,auxiliary;
    RichAttributes attributes;
};
struct RichVisitor {
    std::function<void(RichEvent,const RichNode&)> event;
    std::function<bool()> proceed;
};
// b80020 / b8bd80 / b83360 / b85860 / b7eb60. The continuation predicate is
// queried at the original call sites, including after a cancelled child.
void richVisit(const RichNode &,const RichVisitor &);
// 7139f0 / 713a20 / 713a10 / 713a50, connected to the recovered traversal.
// The generic visitor accepts an area provider for additional curve families.
// The existing straight-tree entry also accepts restored circular segments.
using RichPolygonArea=std::function<double(const RichNode&)>;
struct RichStraightSegment {
    ShakerPoint origin{},direction{};
    double length=0;
    std::array<ShakerPoint,2> endpoints{};
};
RichStraightSegment richStraightSegment(ShakerPoint,ShakerPoint); // b799c0 / b77b50 / b77270
RichCircleSample richCircleSample(const RichCircle &, double); // b7ca10 / b7dc20
ShakerPoint richArcDirectedTangent(const RichCircle &, std::array<double, 2>, double); // b77c40 / b77e00
double richCircleParameter(const RichCircle &, ShakerPoint, double hint); // b7c920
double richCircleRepresentativeParameter(const RichCircle &, ShakerPoint); // b7c9d0
double richCirclePeriod(const RichCircle &); // b7c8b0
double richCircleDistance(const RichCircle &, ShakerPoint); // b7c8c0
bool richCirclePointOn(const RichCircle &, ShakerPoint, double tolerance); // b7cb20
ShakerPoint richArcCenter(ShakerPoint, ShakerPoint, ShakerPoint); // b7cd80
RichArcSegment richArcSegment(ShakerPoint, ShakerPoint, ShakerPoint, bool); // b79b80
std::vector<double> richCircleExtremaParameters(const RichCircle &, double, double); // b78500
double richArcSegmentArea(const RichCircle &, std::array<double, 2>,
                         const std::array<ShakerPoint, 2> &, ShakerPoint); // b79000 / b790c0
double richStraightSegmentArea(const std::array<ShakerPoint,2>&,ShakerPoint reference); // b790c0, Line2 branch
double richStraightCurveArea(const RichNode &); // b80bb0
double richStraightPolygonArea(const RichNode &); // b8c0f0, signed outer + signed holes
size_t shakerAppendVisitedTree(ShakerInputScene &,size_t shape,int32_t type,
    const RichNode &,const RichPolygonArea &,std::optional<double> offset={});
size_t shakerAppendVisitedStraightTree(ShakerInputScene &,size_t shape,int32_t type,
    const RichNode &,std::optional<double> offset={});
}
