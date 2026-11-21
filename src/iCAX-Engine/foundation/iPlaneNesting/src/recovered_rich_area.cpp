#include "recovered_rich_traversal.hpp"
#include <immintrin.h>
#include <bit>
#include <cmath>
#include <set>
#include <stdexcept>

namespace study::recovered {
namespace {
// Preserve the destination operand's NaN payload. MSVC may exchange the
// operands of an inlined commutative SSE intrinsic even under /fp:precise.
double quiet(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)|0x8000000000000ULL);}
double add(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a+b;}
double sub(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a-b;}
double mul(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a*b;}
double div(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a/b;}
double absBits(double a){return std::bit_cast<double>(std::bit_cast<uint64_t>(a)&0x7fffffffffffffffULL);}
double neg(double a){return std::bit_cast<double>(std::bit_cast<uint64_t>(a)^0x8000000000000000ULL);}
}
double richCirclePeriod(const RichCircle &circle) {
    return mul(circle.radius, 6.283185307179586);
}
RichCircleSample richCircleSample(const RichCircle &circle, double parameter) {
    const double angle = div(parameter, circle.radius);
    const double cosine = std::cos(angle), sine = std::sin(angle);
    const double negativeSine = neg(sine);
    const double magnitude = std::sqrt(add(mul(negativeSine, negativeSine), mul(cosine, cosine)));
    double normalizedCosine = 0, normalizedNegativeSine = 1;
    if (magnitude != 0) {
        normalizedCosine = div(cosine, magnitude);
        normalizedNegativeSine = div(negativeSine, magnitude);
    }
    const double x = add(mul(circle.radial[0], cosine), mul(circle.radial[1], normalizedNegativeSine));
    const double y = add(mul(circle.radial[1], normalizedCosine), mul(circle.radial[0], sine));
    return {{add(x, circle.center[0]), add(y, circle.center[1])}, {neg(y), x}, {neg(x), neg(y)}};
}
ShakerPoint richArcDirectedTangent(const RichCircle &circle, std::array<double, 2> parameters,
                                 double parameter) {
    const auto tangent = richCircleSample(circle, parameter).tangent;
    const double length = std::sqrt(add(mul(tangent[1], tangent[1]), mul(tangent[0], tangent[0])));
    ShakerPoint unit{1, 0};
    if (length != 0) unit = {div(tangent[0], length), div(tangent[1], length)};
    const double sign = parameters[1] > parameters[0] ? 1. : -1.;
    return {mul(unit[0], sign), mul(unit[1], sign)};
}
double richCircleParameter(const RichCircle &circle, ShakerPoint point, double hint) {
    const double x = sub(point[0], circle.center[0]), y = sub(point[1], circle.center[1]);
    const double angle = std::atan2(sub(mul(circle.radial[0], y), mul(circle.radial[1], x)),
                                    add(mul(circle.radial[1], y), mul(circle.radial[0], x)));
    const double base = mul(angle, circle.radius), period = richCirclePeriod(circle);
    return add(mul(std::round(div(sub(hint, base), period)), period), base);
}
double richCircleRepresentativeParameter(const RichCircle &circle, ShakerPoint point) {
    return richCircleParameter(circle, point, mul(richCirclePeriod(circle), .5));
}
double richCircleDistance(const RichCircle &circle, ShakerPoint point) {
    const double x = sub(circle.center[0], point[0]), y = sub(circle.center[1], point[1]);
    return sub(circle.radius, std::sqrt(add(mul(y, y), mul(x, x))));
}
bool richCirclePointOn(const RichCircle &circle, ShakerPoint point, double tolerance) {
    const double x = sub(point[0], circle.center[0]), y = sub(point[1], circle.center[1]);
    const double distance = sub(std::sqrt(add(mul(y, y), mul(x, x))), circle.radius);
    return tolerance >= absBits(distance);
}
ShakerPoint richArcCenter(ShakerPoint first, ShakerPoint last, ShakerPoint approximate) {
    if (last[0] == first[0] && last[1] == first[1]) return approximate;
    const double x = sub(first[0], approximate[0]), y = sub(first[1], approximate[1]);
    const double dx = sub(last[0], first[0]), dy = sub(last[1], first[1]);
    const double firstRadius = std::sqrt(add(mul(y, y), mul(x, x)));
    const double lastX = sub(last[0], approximate[0]), lastY = sub(last[1], approximate[1]);
    const double lastRadius = std::sqrt(add(mul(lastY, lastY), mul(lastX, lastX)));
    const ShakerPoint midpoint{mul(add(last[0], first[0]), .5), mul(add(last[1], first[1]), .5)};
    const double radius = mul(add(lastRadius, firstRadius), .5);
    const double chordSquared = add(mul(dy, dy), mul(dx, dx));
    const double heightSquared = sub(mul(radius, radius), mul(chordSquared, .25));
    if (!(1e-6 < absBits(sub(heightSquared, 0.)))) return midpoint;
    const double height = std::sqrt(heightSquared);
    ShakerPoint radial{1, 0}, direction{1, 0};
    if (firstRadius != 0) radial = {div(x, firstRadius), div(y, firstRadius)};
    const double chord = std::sqrt(chordSquared);
    if (chord != 0) direction = {div(dx, chord), div(dy, chord)};
    const double cross = sub(mul(radial[0], direction[1]), mul(radial[1], direction[0]));
    const int sign = sub(0., cross) > 1e-6 ? -1 : int(sub(cross, 0.) > 1e-6);
    const double signedHeight = mul(double(sign), height);
    return {sub(midpoint[0], mul(signedHeight, direction[1])),
            add(mul(signedHeight, direction[0]), midpoint[1])};
}
RichArcSegment richArcSegment(ShakerPoint first, ShakerPoint last, ShakerPoint approximate, bool positive) {
    RichArcSegment segment;
    segment.circle.center = richArcCenter(first, last, approximate);
    double x = sub(first[0], segment.circle.center[0]), y = sub(first[1], segment.circle.center[1]);
    segment.circle.radius = std::sqrt(add(mul(y, y), mul(x, x)));
    if (segment.circle.radius == 0) {
        segment.circle.center = approximate;
        x = sub(first[0], approximate[0]);
        y = sub(first[1], approximate[1]);
        segment.circle.radius = std::sqrt(add(mul(y, y), mul(x, x)));
    }
    segment.circle.radial = {segment.circle.radius, 0};
    const double begin = richCircleRepresentativeParameter(segment.circle, first);
    double end = richCircleRepresentativeParameter(segment.circle, last);
    const double direction = double(positive) * 2 - 1;
    if (mul(direction, end) < mul(direction, begin))
        end = add(end, mul(richCirclePeriod(segment.circle), direction));
    segment.parameters = {begin, end};
    segment.endpoints = {richCircleSample(segment.circle, begin).point,
                         richCircleSample(segment.circle, end).point};
    return segment;
}
std::vector<double> richCircleExtremaParameters(const RichCircle &circle, double first, double last) {
    std::vector<double> result;
    const ShakerPoint top{add(circle.center[0], 0.), add(circle.center[1], circle.radius)};
    const double period = richCirclePeriod(circle);
    double parameter = richCircleRepresentativeParameter(circle, top);
    const double quarter = mul(period, .25);
    const auto include = [&]() {
        const double lower = last < first ? last : first, upper = first < last ? last : first;
        if (lower <= parameter && parameter <= upper) result.push_back(parameter);
    };
    if (last <= first) {
        while (parameter < first) parameter = add(parameter, period);
        if (last < parameter) {
            do {
                include();
                parameter = sub(parameter, quarter);
            } while (last < parameter);
        }
    } else {
        while (first < parameter) parameter = sub(parameter, period);
        if (parameter < last) {
            do {
                include();
                parameter = add(parameter, quarter);
            } while (parameter < last);
        }
    }
    return result;
}
double richArcSegmentArea(const RichCircle &circle, std::array<double, 2> parameters,
                         const std::array<ShakerPoint, 2> &endpoints, ShakerPoint reference) {
    const double sector = mul(mul(mul(div(mul(sub(parameters[1], parameters[0]), 6.283185307179586),
                                        richCirclePeriod(circle)), .5), circle.radius), circle.radius);
    const double correction = sub(sector, richStraightSegmentArea(endpoints, circle.center));
    return add(correction, richStraightSegmentArea(endpoints, reference));
}
RichStraightSegment richStraightSegment(ShakerPoint first,ShakerPoint last){
    RichStraightSegment line;line.origin=first;line.direction={1,0};
    const double dx=sub(last[0],first[0]),dy=sub(last[1],first[1]);
    line.length=_mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(),_mm_set_sd(add(mul(dy,dy),mul(dx,dx)))));
    if(line.length!=0)line.direction={div(dx,line.length),div(dy,line.length)};
    for(size_t n=0;n<2;++n)for(size_t axis=0;axis<2;++axis)line.endpoints[n][axis]=add(mul(line.direction[axis],n?line.length:0),first[axis]);
    return line;
}
double richStraightSegmentArea(const std::array<ShakerPoint,2> &p,ShakerPoint reference){
    return mul(sub(mul(sub(p[1][1],reference[1]),sub(p[0][0],reference[0])),mul(sub(p[0][1],reference[1]),sub(p[1][0],reference[0]))),.5);
}
double richStraightCurveArea(const RichNode &curve){
    if(curve.kind!=RichKind::Curve||!curve.first||!curve.last)throw std::invalid_argument("Rich curve area requires nonempty curve topology");
    const auto first=curve.first;double sum=0;std::set<const RichNode*> visited;
    for(auto edge=first->next;edge;edge=edge->next->next){
        if(edge->kind!=RichKind::Edge||!edge->next||edge->next->kind!=RichKind::Vertex||!visited.insert(edge).second)throw std::invalid_argument("Rich curve area requires valid edges");
        if (edge->circle) {
            if (!edge->parameters) throw std::invalid_argument("Rich arc area requires curve parameters");
            sum=add(sum,richArcSegmentArea(*edge->circle,*edge->parameters,edge->endpoints,first->point));
        } else {
            if (!edge->line) throw std::invalid_argument("Rich curve area requires a supported curve");
            sum=add(sum,richStraightSegmentArea(edge->endpoints,first->point));
        }
        if(edge->next==first)break;
    }
    double closure=0;
    if(!curve.last->next){
        const double x=absBits(sub(first->point[0],curve.last->point[0])),y=absBits(sub(first->point[1],curve.last->point[1]));
        // COMISD/JB includes unordered comparisons in the closing-line branch.
        if(!(curve.tolerance>=x)||!(curve.tolerance>=y))closure=richStraightSegmentArea(richStraightSegment(curve.last->point,first->point).endpoints,first->point);
    }
    return add(closure,sum);
}
double richStraightPolygonArea(const RichNode &polygon){
    if(polygon.kind!=RichKind::Polygon)throw std::invalid_argument("Rich area requires a polygon");
    double area=polygon.outer?richStraightCurveArea(*polygon.outer):0;
    for(const auto hole:polygon.holes){if(!hole)throw std::invalid_argument("Rich area requires a hole curve");area=add(area,richStraightCurveArea(*hole));}
    return area;
}
size_t shakerAppendVisitedStraightTree(ShakerInputScene &scene,size_t shape,int32_t type,const RichNode &root,std::optional<double> offset){
    return shakerAppendVisitedTree(scene,shape,type,root,richStraightPolygonArea,offset);
}
}
