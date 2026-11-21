#include "PlaneMixedCurves.h"
#include <BndLib_Add2dCurve.hxx>
#include <Bnd_Box2d.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace iCAX::GeometryServices::PlaneGeometryDetail {
namespace {
using Point = PlaneNesting::Point;
using Ring = PlaneNesting::Ring;
constexpr double pi = std::numbers::pi, tau = 2 * pi;
constexpr double chord = .05, connectionTolerance = 1e-7;
constexpr std::size_t pointLimit = 100000, segmentLimit = 512, poleLimit = 256;
void Cancel(const std::function<bool()>& cancelled) {
  if (cancelled && cancelled()) throw std::runtime_error("PLANE_GEOMETRY_CANCELLED");
}
Point ReadPoint(const iCAX::Data::Variant& value) {
  const auto points = ToRing(Array{value});
  return points.front();
}
Array WritePoint(Point p) { return {p.X, p.Y}; }
Array WritePoints(const Ring& points) { Array result;for (auto p : points) result.emplace_back(WritePoint(p));return result; }
double Distance(Point a, Point b) { return std::hypot(a.X - b.X, a.Y - b.Y); }
double Mod(double value) { value = std::fmod(value, tau);return value < 0 ? value + tau : value; }
bool Flag(const Map& value, const Text& key) { return value.contains(key) && value.at(key).To<bool>(); }
double Angle(const Map& value, const Text& key) { return std::remainder(Number(value, key), 360.) * pi / 180; }
Point ConicPoint(const Map& curve, double parameter) {
  const auto center = ReadPoint(curve.at("center"));
  const bool circle = String(curve, "type") == "arc";
  const double rx = Number(curve, circle ? "radius" : "radiusX"), ry = circle ? rx : Number(curve, "radiusY");
  const double rotation = circle ? 0 : Angle(curve, "rotationDeg");
  const double x = rx * std::cos(parameter), y = ry * std::sin(parameter);
  return {center.X + x * std::cos(rotation) - y * std::sin(rotation),
          center.Y + x * std::sin(rotation) + y * std::cos(rotation)};
}
void ValidatePoles(const Ring& points, std::size_t minimum, std::size_t maximum) {
  if (points.size() < minimum || points.size() > maximum)
    throw std::invalid_argument("PLANE_INVALID_CURVE_CONTROL_POINTS");
  if (std::none_of(points.begin() + 1, points.end(), [&](Point p) { return Distance(points.front(), p) > 1e-9; }))
    throw std::invalid_argument("PLANE_DEGENERATE_CURVE_SEGMENT");
}
Map NormalizeSegment(const Map& source) {
  const auto type = String(source, "type");
  if (type == "line") {
    const auto points = ToRing(source.at("points").To<Array>());
    ValidatePoles(points, 2, 2);
    return {{"type", type}, {"points", WritePoints(points)}};
  }
  if (type == "arc" || type == "ellipseArc") {
    const auto center = ReadPoint(source.at("center"));
    const double sweep = Number(source, "sweepAngleDeg");
    if (std::abs(sweep) < 1e-10 || std::abs(sweep) > 360.)
      throw std::invalid_argument("PLANE_INVALID_CURVE_SWEEP");
    Map curve{{"type", type}, {"center", WritePoint(center)},
      {"startAngleDeg", Number(source, "startAngleDeg")}, {"sweepAngleDeg", sweep}};
    if (type == "arc") {
      const auto radius = Number(source, "radius");
      if (!(radius > 0)) throw std::invalid_argument("PLANE_INVALID_RADIUS");
      curve["radius"] = radius;
    } else {
      const auto rx = Number(source, "radiusX"), ry = Number(source, "radiusY");
      if (!(rx > 0) || !(ry > 0)) throw std::invalid_argument("PLANE_INVALID_ELLIPSE_RADII");
      curve["radiusX"] = rx;curve["radiusY"] = ry;curve["rotationDeg"] = Number(source, "rotationDeg");
    }
    return curve;
  }
  if (type != "bezier" && type != "bspline") throw std::invalid_argument("PLANE_INVALID_CURVE_SEGMENT");
  const auto poles = ToRing(source.at("controlPoints").To<Array>());
  ValidatePoles(poles, 2, type == "bezier" ? 26 : poleLimit);
  Map curve{{"type", type}, {"controlPoints", WritePoints(poles)}};
  if (source.contains("weights")) {
    const auto values = source.at("weights").To<Array>();
    if (values.size() != poles.size()) throw std::invalid_argument("PLANE_INVALID_BSPLINE_WEIGHTS");
    Array weights;
    for (const auto& value : values) {
      const auto weight = Number(value);
      if (!(weight >= 1e-12)) throw std::invalid_argument("PLANE_INVALID_BSPLINE_WEIGHTS");
      weights.emplace_back(weight);
    }
    curve["weights"] = weights;
  }
  if (type == "bezier") return curve;
  const auto rawDegree = Number(source, "degree", static_cast<double>(std::min<std::size_t>(3, poles.size() - 1)));
  if (rawDegree != std::floor(rawDegree) || rawDegree < 1 || rawDegree > 25 || rawDegree >= poles.size())
    throw std::invalid_argument("PLANE_INVALID_BSPLINE_DEGREE");
  const auto degree = static_cast<std::size_t>(rawDegree);
  Array knots;
  if (source.contains("knots")) knots = source.at("knots").To<Array>();
  else for (std::size_t index = 0; index < poles.size() + degree + 1; ++index)
    knots.emplace_back(index <= degree ? 0. : index >= poles.size() ? 1. :
      static_cast<double>(index - degree) / (poles.size() - degree));
  if (knots.size() != poles.size() + degree + 1) throw std::invalid_argument("PLANE_INVALID_BSPLINE_KNOTS");
  std::vector<double> values;
  for (const auto& knot : knots) values.push_back(Number(knot));
  if (!std::is_sorted(values.begin(), values.end()) || !(values.front() < values.back()))
    throw std::invalid_argument("PLANE_INVALID_BSPLINE_KNOTS");
  for (std::size_t index = 0; index <= degree; ++index)
    if (values[index] != values.front() || values[values.size() - 1 - index] != values.back())
      throw std::invalid_argument("PLANE_INVALID_BSPLINE_KNOTS");
  std::size_t multiplicity = 1;
  for (std::size_t index = 1; index < values.size(); ++index) {
    if (values[index] == values[index - 1]) ++multiplicity;
    else {
      if (values[index - 1] != values.front() && multiplicity > degree)
        throw std::invalid_argument("PLANE_INVALID_BSPLINE_KNOTS");
      if (values[index] - values[index - 1] <= 1e-12)
        throw std::invalid_argument("PLANE_INVALID_BSPLINE_KNOTS");
      multiplicity = 1;
    }
  }
  if (std::count(values.begin(), values.end(), values.front()) != degree + 1 ||
      std::count(values.begin(), values.end(), values.back()) != degree + 1)
    throw std::invalid_argument("PLANE_INVALID_BSPLINE_KNOTS");
  curve["degree"] = static_cast<int>(degree);curve["knots"] = knots;return curve;
}
Handle(Geom2d_Curve) ExactSegment(const Map& curve) {
  const auto type = String(curve, "type");
  if (type == "line") {
    const auto points = ToRing(curve.at("points").To<Array>());
    return GCE2d_MakeSegment(gp_Pnt2d(points[0].X, points[0].Y), gp_Pnt2d(points[1].X, points[1].Y)).Value();
  }
  if (type == "arc" || type == "ellipseArc") {
    const bool circular = type == "arc";
    const auto center = ReadPoint(curve.at("center"));
    const double rx = Number(curve, circular ? "radius" : "radiusX"), ry = circular ? rx : Number(curve, "radiusY");
    const bool swap = ry > rx;
    const double rotation = circular ? 0 : Angle(curve, "rotationDeg");
    const double orientation = rotation + (swap ? pi / 2 : 0);
    const gp_Ax2d axis(gp_Pnt2d(center.X, center.Y), gp_Dir2d(std::cos(orientation), std::sin(orientation)));
    Handle(Geom2d_Curve) basis;
    if (circular || rx == ry) basis = new Geom2d_Circle(axis, rx);
    else basis = new Geom2d_Ellipse(axis, std::max(rx, ry), std::min(rx, ry));
    const double start = Angle(curve, "startAngleDeg") - (swap ? pi / 2 : 0), sweep = Number(curve, "sweepAngleDeg") * pi / 180;
    auto trimmed = new Geom2d_TrimmedCurve(basis, std::min(start, start + sweep), std::max(start, start + sweep));
    if (sweep < 0) trimmed->Reverse();
    return trimmed;
  }
  const auto points = ToRing(curve.at("controlPoints").To<Array>());
  TColgp_Array1OfPnt2d poles(1, static_cast<int>(points.size()));
  TColStd_Array1OfReal weights(1, static_cast<int>(points.size()));
  const auto supplied = curve.contains("weights") ? curve.at("weights").To<Array>() : Array{};
  for (int index = 1; index <= poles.Upper(); ++index) {
    poles.SetValue(index, gp_Pnt2d(points[index - 1].X, points[index - 1].Y));
    weights.SetValue(index, supplied.empty() ? 1. : Number(supplied[index - 1]));
  }
  if (type == "bezier") return supplied.empty() ? Handle(Geom2d_Curve)(new Geom2d_BezierCurve(poles)) :
    Handle(Geom2d_Curve)(new Geom2d_BezierCurve(poles, weights));
  const auto full = curve.at("knots").To<Array>();
  std::vector<double> knots;std::vector<int> multiplicities;
  for (const auto& value : full) {
    const auto knot = Number(value);
    if (!knots.empty() && knots.back() == knot) ++multiplicities.back();
    else { knots.push_back(knot);multiplicities.push_back(1); }
  }
  TColStd_Array1OfReal distinct(1, static_cast<int>(knots.size()));
  TColStd_Array1OfInteger mult(1, static_cast<int>(knots.size()));
  for (int index = 1; index <= distinct.Upper(); ++index) { distinct.SetValue(index, knots[index - 1]);mult.SetValue(index, multiplicities[index - 1]); }
  const int degree = static_cast<int>(Number(curve, "degree"));
  return supplied.empty() ? Handle(Geom2d_Curve)(new Geom2d_BSplineCurve(poles, distinct, mult, degree)) :
    Handle(Geom2d_Curve)(new Geom2d_BSplineCurve(poles, weights, distinct, mult, degree));
}
std::pair<Point, Point> Endpoints(const Map& curve) {
  const auto type = String(curve, "type");
  if (type == "arc" || type == "ellipseArc") {
    const double start = Angle(curve, "startAngleDeg"), sweep = Number(curve, "sweepAngleDeg") * pi / 180;
    return {ConicPoint(curve, start), ConicPoint(curve, start + sweep)};
  }
  const auto points = ToRing(curve.at(type == "line" ? "points" : "controlPoints").To<Array>());
  return {points.front(), points.back()};
}
void AddSnap(Array* snaps, const Text& kind, Point p) {
  if (snaps) snaps->emplace_back(Map{{"type", kind}, {"point", WritePoint(p)}});
}
struct WeightedPoint { double X, Y, W; };
using Net = std::vector<WeightedPoint>;
Point Cartesian(const WeightedPoint& p) { return {p.X / p.W, p.Y / p.W}; }
std::pair<Net, Net> SplitNet(const Net& net) {
  Net work = net, left{net.front()}, right{net.back()};
  while (work.size() > 1) {
    for (std::size_t i = 0; i + 1 < work.size(); ++i)
      work[i] = {(work[i].X + work[i + 1].X) / 2, (work[i].Y + work[i + 1].Y) / 2, (work[i].W + work[i + 1].W) / 2};
    work.pop_back();left.push_back(work.front());right.push_back(work.back());
  }
  std::reverse(right.begin(), right.end());
  return {std::move(left), std::move(right)};
}
Net BezierNet(const Handle(Geom2d_BezierCurve)& curve) {
  Net net;
  for (int index = 1; index <= curve->NbPoles(); ++index) {
    const auto p = curve->Pole(index);const double weight = curve->Weight(index);
    net.push_back({p.X() * weight, p.Y() * weight, weight});
  }
  return net;
}
double ChordDistance(Point p, Point a, Point b) {
  const double dx = b.X - a.X, dy = b.Y - a.Y, length2 = dx * dx + dy * dy;
  const double t = length2 > 0 ? std::clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / length2, 0., 1.) : 0.;
  return Distance(p, {a.X + t * dx, a.Y + t * dy});
}
void SampleNet(const Net& net, Ring& output, int depth, const std::function<bool()>& cancelled) {
  Cancel(cancelled);
  const auto a = Cartesian(net.front()), b = Cartesian(net.back());
  double deviation = 0;
  for (const auto& p : net) deviation = std::max(deviation, ChordDistance(Cartesian(p), a, b));
  if (deviation <= chord) {
    if (output.size() >= pointLimit) throw std::invalid_argument("PLANE_CURVE_APPROXIMATION_LIMIT");
    output.push_back(b);return;
  }
  if (depth >= 40) throw std::invalid_argument("PLANE_CURVE_APPROXIMATION_LIMIT");
  const auto [left, right] = SplitNet(net);
  SampleNet(left, output, depth + 1, cancelled);SampleNet(right, output, depth + 1, cancelled);
}
void SampleBezier(const Handle(Geom2d_BezierCurve)& curve, Ring& output, const std::function<bool()>& cancelled) {
  const auto net = BezierNet(curve);
  if (output.empty()) output.push_back(Cartesian(net.front()));
  SampleNet(net, output, 0, cancelled);
}
Ring SampleSegment(const Map& segment, const std::function<bool()>& cancelled) {
  Cancel(cancelled);
  const auto type = String(segment, "type");
  if (type == "line") return ToRing(segment.at("points").To<Array>());
  if (type == "arc" || type == "ellipseArc") {
    const double start = Angle(segment, "startAngleDeg"), sweep = Number(segment, "sweepAngleDeg") * pi / 180;
    const double radius = type == "arc" ? Number(segment, "radius") : std::max(Number(segment, "radiusX"), Number(segment, "radiusY"));
    const double step = std::min(pi / 2, std::sqrt(8 * chord / radius));
    const double required = std::ceil(std::abs(sweep) / step);
    if (!std::isfinite(required) || required > pointLimit - 1) throw std::invalid_argument("PLANE_CURVE_APPROXIMATION_LIMIT");
    const int count = std::max(2, static_cast<int>(required));
    std::vector<double> parameters;
    for (int index = 0; index <= count; ++index) parameters.push_back(static_cast<double>(index) / count);
    const double rotation = type == "arc" ? 0 : Angle(segment, "rotationDeg");
    const double rx = type == "arc" ? radius : Number(segment, "radiusX"), ry = type == "arc" ? radius : Number(segment, "radiusY");
    const std::array<double, 2> extrema{
      std::atan2(-ry * std::sin(rotation), rx * std::cos(rotation)),
      std::atan2(ry * std::cos(rotation), rx * std::sin(rotation))};
    for (const double extreme : extrema) for (int side = 0; side < 2; ++side) {
      const double target = extreme + side * pi, travel = sweep > 0 ? Mod(target - start) : Mod(start - target);
      if (travel > 0 && travel < std::abs(sweep)) parameters.push_back(travel / std::abs(sweep));
    }
    std::sort(parameters.begin(), parameters.end());
    parameters.erase(std::unique(parameters.begin(), parameters.end()), parameters.end());
    Ring result;
    for (const double t : parameters) { Cancel(cancelled);result.push_back(ConicPoint(segment, start + sweep * t)); }
    return result;
  }
  const auto exact = ExactSegment(segment);
  Ring result;
  if (type == "bezier") SampleBezier(Handle(Geom2d_BezierCurve)::DownCast(exact), result, cancelled);
  else {
    Geom2dConvert_BSplineCurveToBezierCurve spans(Handle(Geom2d_BSplineCurve)::DownCast(exact));
    for (int index = 1; index <= spans.NbArcs(); ++index) SampleBezier(spans.Arc(index), result, cancelled);
  }
  return result;
}
struct LineSplineCheck {
  Handle(Geom2d_Curve) Line, Spline;
  Point A, B;
  double DX = 0, DY = 0, Length2 = 0;
  bool SplineClosed = false;
  std::size_t Work = 0;
  const std::function<bool()>& Cancelled;
  long double Signed(Point p) const {
    return static_cast<long double>(DX) * (static_cast<long double>(p.Y) - A.Y) -
           static_cast<long double>(DY) * (static_cast<long double>(p.X) - A.X);
  }
  double Projection(Point p) const {
    return ((p.X - A.X) * DX + (p.Y - A.Y) * DY) / Length2;
  }
  void Endpoint(Point p, double parameter) const {
    const double projection = Projection(p);
    if (projection < 0 || projection > 1) return;
    const double lineParameter = Line->FirstParameter() +
        projection * (Line->LastParameter() - Line->FirstParameter());
    const bool sharedEndpoint = !SplineClosed &&
        (CurvePointAtBoundary(Line, lineParameter, p, true) ||
         CurvePointAtBoundary(Line, lineParameter, p, false)) &&
        (CurvePointAtBoundary(Spline, parameter, p, true) ||
         CurvePointAtBoundary(Spline, parameter, p, false));
    if (!sharedEndpoint)
      throw std::invalid_argument("PLANE_MATERIAL_INTERSECTING_CURVES");
  }
  void Check(const Net& net, double first, double last, int depth = 0) {
    Cancel(Cancelled);
    if (++Work > 100000)
      throw std::invalid_argument("PLANE_MATERIAL_GEOMETRY_CHECK_FAILED: Bernstein work limit");
    double minProjection = std::numeric_limits<double>::max(), maxProjection = -minProjection;
    int previousSign = 0, variations = 0;
    std::vector<long double> coefficients;
    for (std::size_t index = 0; index < net.size(); ++index) {
      const auto& pole = net[index];
      const auto p = Cartesian(pole);
      const double projection = Projection(p);
      minProjection = std::min(minProjection, projection);
      maxProjection = std::max(maxProjection, projection);
      long double coefficient = Signed(p) * pole.W;
      if (index == 0 || index + 1 == net.size()) {
        const double parameter = index == 0 ? first : last;
        const bool wholeCurveEnd = parameter == Spline->FirstParameter() ||
                                   parameter == Spline->LastParameter();
        const bool sharedLineEnd =
            CurvePointAtBoundary(Line, Line->FirstParameter(), p, true) ||
            CurvePointAtBoundary(Line, Line->LastParameter(), p, false);
        if (wholeCurveEnd && sharedLineEnd) coefficient = 0;
      }
      coefficients.push_back(coefficient);
      const int sign = coefficient > 0 ? 1 : coefficient < 0 ? -1 : 0;
      if (sign) { if (previousSign && sign != previousSign) ++variations;previousSign = sign; }
    }
    // Positive rational weights keep the exact curve inside this projection hull.
    if (maxProjection < 0 || minProjection > 1) return;
    if (coefficients.front() == 0) Endpoint(Cartesian(net.front()), first);
    if (coefficients.back() == 0) Endpoint(Cartesian(net.back()), last);
    if (previousSign && !variations) return;
    // Bernstein/Descartes: one sign variation proves one root strictly inside
    // this interval. It cannot be dismissed using a midpoint near a chain end.
    if (variations == 1 && minProjection >= 0 && maxProjection <= 1)
      throw std::invalid_argument("PLANE_MATERIAL_INTERSECTING_CURVES");
    if (!previousSign) {
      if (maxProjection <= 0 || minProjection >= 1) return;
      const double firstProjection = Projection(Cartesian(net.front())),
                   lastProjection = Projection(Cartesian(net.back()));
      if ((firstProjection > 0 && firstProjection < 1) ||
          (lastProjection > 0 && lastProjection < 1))
        throw std::invalid_argument("PLANE_MATERIAL_OVERLAPPING_CURVES");
    }
    const double middle = first + (last - first) / 2;
    if (depth >= 64 || middle == first || middle == last)
      throw std::invalid_argument("PLANE_MATERIAL_GEOMETRY_CHECK_FAILED: Bernstein isolation limit");
    const auto [left, right] = SplitNet(net);
    Check(left, first, middle, depth + 1);Check(right, middle, last, depth + 1);
  }
};
} // namespace

bool CurvePointAtBoundary(const Handle(Geom2d_Curve)& curve, double parameter,
                          Point intersection, bool first) {
  const double start = curve->FirstParameter(), end = curve->LastParameter(), range = end - start;
  const double roundoff = 16 * std::numeric_limits<double>::epsilon() *
                          std::max(std::abs(start), std::abs(end));
  const double parameterTolerance =
      std::min(range * 1e-8, std::max(range * 1e-10, roundoff));
  const double boundary = first ? start : end;
  if (std::abs(parameter - boundary) > parameterTolerance) return false;
  const auto exact = curve->Value(boundary);
  const double coordinateScale = std::max({1., std::abs(exact.X()), std::abs(exact.Y()),
                                           std::abs(intersection.X), std::abs(intersection.Y)});
  return Distance(intersection, {exact.X(), exact.Y()}) <=
      1e-7 + 8 * std::numeric_limits<double>::epsilon() * coordinateScale;
}
void ValidateLineSplineMaterialIntersections(const Handle(Geom2d_Curve)& first,
                                             const Handle(Geom2d_Curve)& second,
                                             const std::function<bool()>& cancelled) {
  Handle(Geom2d_Curve) line, spline;
  if (Geom2dAdaptor_Curve(first).GetType() == GeomAbs_Line) { line = first;spline = second; }
  else if (Geom2dAdaptor_Curve(second).GetType() == GeomAbs_Line) { line = second;spline = first; }
  else return;
  const auto bezier = Handle(Geom2d_BezierCurve)::DownCast(spline);
  const auto bspline = Handle(Geom2d_BSplineCurve)::DownCast(spline);
  if (bezier.IsNull() && bspline.IsNull()) return;
  const auto a = line->Value(line->FirstParameter()), b = line->Value(line->LastParameter()),
             c = spline->Value(spline->FirstParameter()), d = spline->Value(spline->LastParameter());
  const double dx = b.X() - a.X(), dy = b.Y() - a.Y();
  LineSplineCheck check{line, spline, {a.X(), a.Y()}, {b.X(), b.Y()}, dx, dy,
                       dx * dx + dy * dy, c.Distance(d) <= 1e-9, 0, cancelled};
  if (!bezier.IsNull()) check.Check(BezierNet(bezier), spline->FirstParameter(), spline->LastParameter());
  else {
    Geom2dConvert_BSplineCurveToBezierCurve spans(bspline);
    TColStd_Array1OfReal knots(1, spans.NbArcs() + 1);spans.Knots(knots);
    for (int index = 1; index <= spans.NbArcs(); ++index)
      check.Check(BezierNet(spans.Arc(index)), knots(index), knots(index + 1));
  }
}

Map NormalizeMixedCurve(const Map& source) {
  if (!source.contains("segments")) throw std::invalid_argument("PLANE_CURVE_SEGMENTS_REQUIRED");
  const auto values = source.at("segments").To<Array>();
  if (values.empty() || values.size() > segmentLimit) throw std::invalid_argument("PLANE_INVALID_CURVE_SEGMENT_COUNT");
  Array segments;Point first{}, previous{};bool initial = true;
  for (const auto& value : values) {
    auto segment = NormalizeSegment(value.To<Map>());
    const auto [a, b] = Endpoints(segment);
    if (!initial && Distance(previous, a) > connectionTolerance) throw std::invalid_argument("PLANE_POLYCURVE_DISCONNECTED");
    if (initial) first = a;
    initial = false;previous = b;segments.emplace_back(std::move(segment));
  }
  const bool closed = Flag(source, "closed");
  if (closed && Distance(previous, first) > connectionTolerance) {
    if (segments.size() >= segmentLimit)
      throw std::invalid_argument("PLANE_INVALID_CURVE_SEGMENT_COUNT");
    segments.emplace_back(Map{{"type", Text("line")}, {"points", Array{WritePoint(previous), WritePoint(first)}}});
  }
  return {{"type", Text("polycurve")}, {"segments", segments}, {"closed", closed}};
}
Map CloseMixedCurve(const Map& source) {
  if (Flag(source, "closed")) throw std::invalid_argument("PLANE_POLYLINE_ALREADY_CLOSED");
  auto value = source;value["closed"] = true;return NormalizeMixedCurve(value);
}
std::vector<MixedCurveEdge> MixedCurveEdges(const Map& curve) {
  std::vector<MixedCurveEdge> result;
  try {
    for (const auto& value : curve.at("segments").To<Array>()) {
      const auto segment = value.To<Map>();const auto [a, b] = Endpoints(segment);
      const auto type = String(segment, "type");
      result.push_back({ExactSegment(segment), a, b, Distance(a, b) <= 1e-9,
                       type == "bezier" || type == "bspline"});
    }
  } catch (const Standard_Failure& error) {
    throw std::invalid_argument(Text("PLANE_INVALID_CURVE_SEGMENT: ") + error.GetMessageString());
  }
  return result;
}
Ring MixedCurvePath(const Map& curve, Array* snaps, const std::function<bool()>& cancelled) {
  Ring result;
  try {
    for (const auto& value : curve.at("segments").To<Array>()) {
      const auto segment = value.To<Map>();auto sampled = SampleSegment(segment, cancelled);
      if (result.size() + sampled.size() > pointLimit) throw std::invalid_argument("PLANE_CURVE_APPROXIMATION_LIMIT");
      if (result.empty()) result = std::move(sampled);
      else result.insert(result.end(), sampled.begin() + (Distance(result.back(), sampled.front()) <= 1e-12 ? 1 : 0), sampled.end());
      const auto [a, b] = Endpoints(segment);
      AddSnap(snaps, "end", a);AddSnap(snaps, "end", b);
      const auto exact = ExactSegment(segment);const auto middle = exact->Value((exact->FirstParameter() + exact->LastParameter()) / 2);
      AddSnap(snaps, "mid", {middle.X(), middle.Y()});
      if (segment.contains("center")) AddSnap(snaps, "center", ReadPoint(segment.at("center")));
    }
  } catch (const Standard_Failure& error) {
    throw std::invalid_argument(Text("PLANE_INVALID_CURVE_SEGMENT: ") + error.GetMessageString());
  }
  return result;
}
std::array<double, 4> MixedCurveBounds(const Map& curve) {
  Bnd_Box2d box;
  try {
    for (const auto& edge : MixedCurveEdges(curve))
      BndLib_Add2dCurve::AddOptimal(edge.Curve, edge.Curve->FirstParameter(), edge.Curve->LastParameter(), 1e-9, box);
    // OCCT adds a detection margin (at least Precision::Confusion for splines).
    // Dimensions describe the optimized curve extrema, without that padding;
    // the optimizer's numerical precision and sampling safety bound are separate.
    box.SetGap(0.);
    double minX, minY, maxX, maxY;box.Get(minX, minY, maxX, maxY);return {minX, minY, maxX, maxY};
  } catch (const Standard_Failure& error) {
    throw std::invalid_argument(Text("PLANE_INVALID_CURVE_SEGMENT: ") + error.GetMessageString());
  }
}
double MixedCurveApproximationBound(const Map& curve) {
  bool curved = false, initial = true;Point previous{}, first{};double gap = 0;
  for (const auto& value : curve.at("segments").To<Array>()) {
    const auto segment = value.To<Map>();curved = curved || String(segment, "type") != "line";
    const auto [a, b] = Endpoints(segment);
    if (initial) first = a;else gap = std::max(gap, Distance(previous, a));
    initial = false;previous = b;
  }
  if (Flag(curve, "closed")) gap = std::max(gap, Distance(previous, first));
  return (curved ? chord : 0.) + gap;
}
Map TransformMixedCurve(const Map& source, const Map& request) {
  const auto operation = String(request, "operation");
  const double dx = Number(request, "dx"), dy = Number(request, "dy"), angleDeg = Number(request, "angleDeg"), angle = angleDeg * pi / 180;
  const double factor = Number(request, "factor", 1), cx = Number(request, "cx"), cy = Number(request, "cy");
  const auto axis = String(request, "axis", "x");
  const auto transform = [&](Point p) -> Point {
    if (operation == "move" || operation == "copy") return {p.X + dx, p.Y + dy};
    if (operation == "scale") return {cx + factor * (p.X - cx), cy + factor * (p.Y - cy)};
    if (operation == "mirror") return axis == "x" ? Point{p.X, 2 * cy - p.Y} : Point{2 * cx - p.X, p.Y};
    return {cx + (p.X - cx) * std::cos(angle) - (p.Y - cy) * std::sin(angle), cy + (p.X - cx) * std::sin(angle) + (p.Y - cy) * std::cos(angle)};
  };
  Array segments;
  for (const auto& value : source.at("segments").To<Array>()) {
    auto segment = value.To<Map>();const auto type = String(segment, "type");
    for (const auto* key : {"points", "controlPoints"}) if (segment.contains(key)) {
      auto points = ToRing(segment.at(key).To<Array>());for (auto& p : points) p = transform(p);segment[key] = WritePoints(points);
    }
    if (segment.contains("center")) segment["center"] = WritePoint(transform(ReadPoint(segment.at("center"))));
    if (operation == "scale") for (const auto* key : {"radius", "radiusX", "radiusY"})
      if (segment.contains(key)) segment[key] = Number(segment, key) * factor;
    if (operation == "rotate") {
      if (type == "arc") segment["startAngleDeg"] = Number(segment, "startAngleDeg") + angleDeg;
      if (type == "ellipseArc") segment["rotationDeg"] = Number(segment, "rotationDeg") + angleDeg;
    }
    if (operation == "mirror") {
      if (type == "arc") segment["startAngleDeg"] = axis == "x" ? -Number(segment, "startAngleDeg") : 180. - Number(segment, "startAngleDeg");
      if (type == "ellipseArc") {
        segment["rotationDeg"] = axis == "x" ? -Number(segment, "rotationDeg") : 180. - Number(segment, "rotationDeg");
        segment["startAngleDeg"] = -Number(segment, "startAngleDeg");
      }
      if (type == "arc" || type == "ellipseArc") segment["sweepAngleDeg"] = -Number(segment, "sweepAngleDeg");
    }
    segments.emplace_back(std::move(segment));
  }
  return NormalizeMixedCurve({{"segments", segments}, {"closed", Flag(source, "closed")}});
}
} // namespace iCAX::GeometryServices::PlaneGeometryDetail
