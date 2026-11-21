#include "recovered_convolution_line.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
double quiet(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)|0x8000000000000ULL);}
double add(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a+b;}
double sub(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a-b;}
double mul(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a*b;}
double div(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a/b;}
double neg(double a){return std::bit_cast<double>(std::bit_cast<uint64_t>(a)^0x8000000000000000ULL);}
ShakerPoint normalize(ShakerPoint v){const double length=std::sqrt(add(mul(v[1],v[1]),mul(v[0],v[0])));if(1e-9>length)return {1,0};const double r=div(1,length);return {mul(v[0],r),mul(v[1],r)};}
}
double convolutionLineParameter(const ConvolutionLine &l,ShakerPoint p){return add(mul(sub(p[1],l.origin[1]),l.direction[1]),mul(sub(p[0],l.origin[0]),l.direction[0]));}
ShakerPoint convolutionLineEvaluate(const ConvolutionLine &l,double p){return {add(mul(l.direction[0],p),l.origin[0]),add(mul(l.direction[1],p),l.origin[1])};}
std::array<double,4> convolutionLineBounds(const ConvolutionLine &l,double a,double b){
    const auto last=convolutionLineEvaluate(l,b),first=convolutionLineEvaluate(l,a);
    return {first[0]>last[0]?last[0]:first[0],first[1]>last[1]?last[1]:first[1],last[0]>first[0]?last[0]:first[0],last[1]>first[1]?last[1]:first[1]};
}
ShakerPoint convolutionCircleEvaluate(const ConvolutionCircle &circle, double parameter) {
    const double angle = div(parameter, circle.radius);
    return {add(mul(std::cos(angle), circle.radius), circle.center[0]),
            add(mul(std::sin(angle), circle.radius), circle.center[1])};
}
ShakerPoint convolutionCircleTangent(const ConvolutionCircle &circle, double parameter) {
    const double angle = div(parameter, circle.radius);
    return {neg(std::sin(angle)), std::cos(angle)};
}
ShakerPoint convolutionCircleNormal(const ConvolutionCircle &circle, double parameter) {
    const double angle = div(parameter, circle.radius);
    return {neg(std::cos(angle)), neg(std::sin(angle))};
}
double convolutionCircleParameter(const ConvolutionCircle &circle, ShakerPoint point) {
    return mul(std::atan2(sub(point[1], circle.center[1]), sub(point[0], circle.center[0])),
               circle.radius);
}
bool convolutionCirclePointOn(const ConvolutionCircle &circle, ShakerPoint point,
                             double tolerance, double &parameter) {
    const double x = sub(point[0], circle.center[0]), y = sub(point[1], circle.center[1]);
    const double distance = sub(std::sqrt(add(mul(y, y), mul(x, x))), circle.radius);
    // The original COMISD/JBE accepts the unordered comparison as well.
    if (std::abs(distance) > tolerance)
        return false;
    parameter = mul(std::atan2(y, x), circle.radius);
    return true;
}
std::array<double, 4> convolutionCircleBounds(const ConvolutionCircle &circle,
                                             double first, double last) {
    constexpr double period = 6.283185307179586;
    const double span = std::abs(div(sub(first, last), circle.radius));
    if (span >= period) {
        const ShakerPoint upper{add(circle.center[0], circle.radius), add(circle.center[1], circle.radius)};
        const ShakerPoint lower{sub(circle.center[0], circle.radius), sub(circle.center[1], circle.radius)};
        return {lower[0] <= upper[0] ? lower[0] : upper[0],
                lower[1] <= upper[1] ? lower[1] : upper[1],
                upper[0] <= lower[0] ? lower[0] : upper[0],
                upper[1] <= lower[1] ? lower[1] : upper[1]};
    }
    double start = std::fmod(div(last <= first ? last : first, circle.radius), period);
    double end = std::fmod(div(last <= first ? first : last, circle.radius), period);
    if (start < 0) start = add(start, period);
    if (end < 0) end = add(end, period);
    while (end < start) end = add(end, period);
    const auto b = convolutionCircleEvaluate(circle, last);
    const auto a = convolutionCircleEvaluate(circle, first);
    std::array<double, 4> bounds{b[0] < a[0] ? b[0] : a[0], b[1] < a[1] ? b[1] : a[1],
                                 a[0] < b[0] ? b[0] : a[0], a[1] < b[1] ? b[1] : a[1]};
    const auto include = [&](ShakerPoint point) {
        for (size_t axis = 0; axis < 2; ++axis) {
            if (point[axis] < bounds[axis]) bounds[axis] = point[axis];
            if (bounds[axis + 2] < point[axis]) bounds[axis + 2] = point[axis];
        }
    };
    if ((start < 1.5707963267948966 && 1.5707963267948966 < end) ||
        (start < 7.853981633974483 && 7.853981633974483 < end))
        include({add(circle.center[0], 0), add(circle.center[1], circle.radius)});
    if ((start < 3.141592653589793 && 3.141592653589793 < end) ||
        (start < 9.42477796076938 && 9.42477796076938 < end))
        include({sub(circle.center[0], circle.radius), add(circle.center[1], 0)});
    if ((start < 4.71238898038469 && 4.71238898038469 < end) ||
        (start < 10.995574287564276 && 10.995574287564276 < end))
        include({add(circle.center[0], 0), sub(circle.center[1], circle.radius)});
    if (start < period && period < end)
        include({add(circle.center[0], circle.radius), add(circle.center[1], 0)});
    return bounds;
}
ShakerPoint convolutionCurveEvaluate(const ConvolutionCurveSegment &curve, double parameter) {
    if (curve.line) return convolutionLineEvaluate(*curve.line, parameter);
    if (curve.circle) return convolutionCircleEvaluate(*curve.circle, parameter);
    throw std::invalid_argument("Curve evaluation requires a line or circle");
}
std::array<double, 4> convolutionCurveBounds(const ConvolutionCurveSegment &curve,
                                             double first, double last) {
    if (curve.line) return convolutionLineBounds(*curve.line, first, last);
    if (curve.circle) return convolutionCircleBounds(*curve.circle, first, last);
    throw std::invalid_argument("Curve bounds require a line or circle");
}
double convolutionCurveParameter(const ConvolutionCurveSegment &curve, ShakerPoint point) {
    if (curve.line) return convolutionLineParameter(*curve.line, point);
    if (curve.circle) return convolutionCircleParameter(*curve.circle, point);
    throw std::invalid_argument("Curve parameter requires a line or circle");
}
double convolutionNormalizeParameter(const ConvolutionCurveSegment &curve, double parameter) {
    if (!curve.circle) return parameter;
    const double lower = std::min(curve.parameters[0], curve.parameters[1]);
    const double upper = std::max(curve.parameters[0], curve.parameters[1]);
    const double period = mul(curve.circle->radius, 6.283185307179586);
    if (!(period > 0) && (parameter > upper || parameter < lower))
        throw std::invalid_argument("Periodic curve requires a positive period");
    while (upper < parameter) parameter = sub(parameter, period);
    while (parameter < lower) parameter = add(parameter, period);
    return parameter;
}
ConvolutionCurveSegment ConvolutionCurves::create(ShakerPoint a,ShakerPoint b){
    auto line=std::make_shared<ConvolutionLine>();line->origin=a;
    const ShakerPoint delta{sub(b[0],a[0]),sub(b[1],a[1])};line->direction=normalize(delta);
    // Normal normalization is separate, retaining its own operand order and
    // fallback {1,0} even when both endpoints coincide.
    const double nx=neg(delta[1]),ny=delta[0];const double length=std::sqrt(add(mul(nx,nx),mul(ny,ny)));
    if(1e-9>length)line->normal={1,0};else {const double r=div(1,length);line->normal={mul(nx,r),mul(r,ny)};}
    line->offset=neg(add(mul(line->normal[1],a[1]),mul(line->normal[0],a[0])));
    ConvolutionCurveSegment result;result.line=std::move(line);
    result.parameters={convolutionLineParameter(*result.line,a),convolutionLineParameter(*result.line,b)};
    result.endpoints={convolutionLineEvaluate(*result.line,result.parameters[0]),convolutionLineEvaluate(*result.line,result.parameters[1])};
    result.bounds=convolutionLineBounds(*result.line,result.parameters[0],result.parameters[1]);
    result.identity=sequence_.fetch_add(1)+1;return result;
}
ConvolutionCurveSegment ConvolutionCurves::copy(const ConvolutionCurveSegment &s){auto result=s;result.identity=sequence_.fetch_add(1)+1;return result;}
ConvolutionCurveSegment ConvolutionCurves::createArc(ShakerPoint first, ShakerPoint middle,
                                                     ShakerPoint last) {
    const ShakerPoint chord{sub(last[0], first[0]), sub(last[1], first[1])};
    if (add(mul(chord[0], chord[0]), mul(chord[1], chord[1])) < 1e-18) {
        ConvolutionCurveSegment result;
        result.boundsEmpty = true;
        result.identity = sequence_.fetch_add(1) + 1;
        return result;
    }
    const ShakerPoint a{sub(middle[0], first[0]), sub(middle[1], first[1])};
    const ShakerPoint b{sub(last[0], middle[0]), sub(last[1], middle[1])};
    const double aSquared = add(mul(a[0], a[0]), mul(a[1], a[1]));
    const double bSquared = add(mul(b[0], b[0]), mul(b[1], b[1]));
    if (aSquared < 1e-18 || bSquared < 1e-18) return create(first, last);
    const double aInverse = div(1, std::sqrt(aSquared));
    const double bInverse = div(1, std::sqrt(bSquared));
    const ShakerPoint an{mul(a[0], aInverse), mul(aInverse, a[1])};
    const ShakerPoint bn{mul(b[0], bInverse), mul(bInverse, b[1])};
    const ShakerPoint am{mul(add(first[0], middle[0]), .5), mul(add(first[1], middle[1]), .5)};
    const ShakerPoint bm{mul(add(middle[0], last[0]), .5), mul(add(middle[1], last[1]), .5)};
    const double ac = neg(add(mul(an[1], am[1]), mul(am[0], an[0])));
    const double bc = neg(add(mul(bn[1], bm[1]), mul(bm[0], bn[0])));
    const double determinant = sub(mul(an[0], bn[1]), mul(an[1], bn[0]));
    if (determinant == 0) return create(first, last);
    auto circle = std::make_shared<ConvolutionCircle>();
    circle->center = {div(sub(mul(an[1], bc), mul(ac, bn[1])), determinant),
                      div(sub(mul(ac, bn[0]), mul(an[0], bc)), determinant)};
    const double dx = sub(circle->center[0], first[0]), dy = sub(circle->center[1], first[1]);
    circle->radius = std::sqrt(add(mul(dx, dx), mul(dy, dy)));
    double start = convolutionCircleParameter(*circle, first);
    double end = convolutionCircleParameter(*circle, last);
    const double period = mul(circle->radius, 6.283185307179586);
    if (sub(mul(a[0], chord[1]), mul(chord[0], a[1])) < 0) {
        while (start < end) start = add(start, period);
    } else {
        while (end < start) end = add(end, period);
    }
    ConvolutionCurveSegment source;
    source.circle = std::move(circle);
    return range(source, start, end);
}
ConvolutionCurveSegment ConvolutionCurves::range(const ConvolutionCurveSegment &source, double first,
                                                double last) {
    ConvolutionCurveSegment result;
    result.line = source.line;
    result.circle = source.circle;
    result.source = source.source;
    result.parameters = {first, last};
    result.endpoints = {convolutionCurveEvaluate(result, first), convolutionCurveEvaluate(result, last)};
    result.bounds = convolutionCurveBounds(result, first, last);
    result.identity = sequence_.fetch_add(1) + 1;
    return result;
}
ConvolutionCurveFactory ConvolutionCurves::edgeFactory(){return [this](ShakerPoint a,ShakerPoint b){return copy(create(a,b));};}
ConvolutionArcFactory ConvolutionCurves::arcFactory() {
    return [this](ShakerPoint first, ShakerPoint middle, ShakerPoint last) {
        return copy(createArc(first, middle, last));
    };
}
std::vector<ConvolutionEntity> convolutionPolygon(const ConvolutionTopology &t){
    constexpr double tau=6.283185307179586,pi=3.141592653589793;
    auto wrap=[](double a){while(a<0)a+=tau;while(a>=tau)a-=tau;return a;};
    std::vector<int32_t> edges;std::vector<double> angles;
    auto current=t.firstEdge;
    do {
        if(current<0||edges.size()>=t.edges.size())throw std::invalid_argument("Convolution requires a closed traversable contour");
        const auto &e=t.edges.at(current);if(!e.curve.line)throw std::invalid_argument("Convolution contour requires a supported curve");
        auto normal=e.curve.line->normal;
        // The virtual +18 method is the line NORMAL, not its derivative.
        // Parameter order reverses that normal before the final negation.
        if(!(e.curve.parameters[1]>e.curve.parameters[0])){normal[0]=neg(normal[0]);normal[1]=neg(normal[1]);}
        edges.push_back(current);angles.push_back(wrap(std::atan2(neg(normal[1]),neg(normal[0]))));
        current=t.vertices.at(e.last).outgoing;
    }while(current!=t.firstEdge);
    std::vector<ConvolutionEntity> result;result.reserve(edges.size()*2);
    for(size_t n=0;n<edges.size();++n){const auto &e=t.edges[edges[n]];const auto &v=t.vertices.at(e.first);const double first=angles[(n+edges.size()-1)%edges.size()],last=angles[n];double span=last-first;while(span<0)span+=tau;
        result.push_back({ConvolutionEntity::Kind::vertex,first,last,int32_t(2*n),span<pi,v.point,v.point,reinterpret_cast<uintptr_t>(&v)});
        result.push_back({ConvolutionEntity::Kind::straight,last,last,int32_t(2*n+1),(last-last)<pi,e.curve.endpoints[0],e.curve.endpoints[1],reinterpret_cast<uintptr_t>(&e)});
    }return result;
}
}
