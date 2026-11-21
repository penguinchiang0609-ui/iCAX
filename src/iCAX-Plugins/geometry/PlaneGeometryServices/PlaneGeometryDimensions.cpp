#include "PlaneGeometryAlgorithms.h"
#include "PlaneMixedCurves.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>

namespace iCAX::GeometryServices::PlaneGeometryDetail {
namespace {
using Point = iCAX::GeometryServices::PlaneNesting::Point;
constexpr double tau = 2 * std::numbers::pi;
Point PointAt(const iCAX::Data::Variant &value) {
  const auto points = ToRing(Array{value});
  return points.front();
}
Array Point2(Point p) { return {p.X, p.Y}; }
Array Point3(Point p) { return {p.X, p.Y, 0.}; }
Point Add(Point a, Point b) { return {a.X + b.X, a.Y + b.Y}; }
Point Sub(Point a, Point b) { return {a.X - b.X, a.Y - b.Y}; }
Point Mul(Point a, double s) { return {a.X * s, a.Y * s}; }
double Length(Point p) { return std::hypot(p.X, p.Y); }
double Mod(double a) { return std::fmod(std::fmod(a, tau) + tau, tau); }
double NormalizeDegrees(double a) {
  double normalized = std::fmod(a, 360.);
  if (normalized < 0.) normalized += 360.;
  return normalized == 0. ? 0. : normalized;
}
double SweepDegrees(const Map &curve) {
  double sweep = std::fmod(Number(curve, "endAngleDeg") -
                              Number(curve, "startAngleDeg"), 360.);
  if (sweep <= 0.) sweep += 360.;
  return sweep;
}
double Sweep(const Map &curve) {
  return SweepDegrees(curve) * std::numbers::pi / 180;
}
enum class Shape { ReadOnly, Radius, Arc, Line, Rectangle };
struct DrawingShape {
  Shape Kind = Shape::ReadOnly;
  Map Curve;
  std::vector<Point> Points;
  Point Origin{}, U{}, V{};
  double Width = 0, Height = 0;
  bool RepeatedTail = false;
};
bool StraightSegments(const Map &curve, std::size_t edges) {
  if (!curve.contains("bulges")) return true;
  const auto bulges = curve.at("bulges").To<Array>();
  for (std::size_t i = 0; i < std::min(edges, bulges.size()); ++i)
    if (Number(bulges[i]) != 0.) return false;
  return true;
}
DrawingShape Classify(const Map &geometry) {
  DrawingShape shape;
  const auto curves = geometry.at("curves").To<Array>();
  if (curves.size() != 1) return shape;
  shape.Curve = curves.front().To<Map>();
  const auto type = String(shape.Curve, "type");
  if (type == "circle" || type == "arc") {
    shape.Kind = type == "arc" ? Shape::Arc : Shape::Radius;
    shape.Origin = PointAt(shape.Curve.at("center"));
    shape.Width = Number(shape.Curve, "radius");
    return shape;
  }
  if (type != "line" && type != "polyline") return shape;
  shape.Points = ToRing(shape.Curve.at("points").To<Array>());
  const bool closed = shape.Curve.contains("closed") &&
                      shape.Curve.at("closed").To<bool>();
  if (!closed && shape.Points.size() == 2 &&
      StraightSegments(shape.Curve, 1)) {
    const auto delta = Sub(shape.Points[1], shape.Points[0]);
    shape.Width = Length(delta);
    if (shape.Width > 0.) {
      shape.Kind = Shape::Line;
      shape.Origin = shape.Points[0];
      shape.U = Mul(delta, 1 / shape.Width);
    }
    return shape;
  }
  if (type != "polyline" || !closed) return shape;
  if (shape.Points.size() == 5 &&
      Length(Sub(shape.Points.front(), shape.Points.back())) <= 1e-9) {
    shape.Points.pop_back();
    shape.RepeatedTail = true;
  }
  if (shape.Points.size() != 4 || !StraightSegments(shape.Curve, 4)) return shape;
  const auto u = Sub(shape.Points[1], shape.Points[0]);
  const auto v = Sub(shape.Points[3], shape.Points[0]);
  shape.Width = Length(u); shape.Height = Length(v);
  if (!(shape.Width > 0.) || !(shape.Height > 0.)) return shape;
  shape.U = Mul(u, 1 / shape.Width); shape.V = Mul(v, 1 / shape.Height);
  const double closureTolerance = 1e-9 * std::max({1., shape.Width, shape.Height});
  if (std::abs(shape.U.X * shape.V.X + shape.U.Y * shape.V.Y) > 1e-10 ||
      Length(Sub(shape.Points[2], Add(shape.Points[1], v))) > closureTolerance)
    return shape;
  shape.Kind = Shape::Rectangle;
  shape.Origin = shape.Points[0];
  return shape;
}
struct Bounds {
  double MinX = std::numeric_limits<double>::max(), MinY = MinX;
  double MaxX = -MinX, MaxY = -MinX;
  void Add(Point p) {
    MinX = std::min(MinX, p.X); MinY = std::min(MinY, p.Y);
    MaxX = std::max(MaxX, p.X); MaxY = std::max(MaxY, p.Y);
  }
  void Arc(Point center, double radius, double start, double sweep) {
    const auto at = [&](double a) {
      Add({center.X + radius * std::cos(a), center.Y + radius * std::sin(a)});
    };
    at(start); at(start + sweep);
    for (int i = 0; i < 4; ++i) {
      const double a = i * std::numbers::pi / 2;
      const double travel = sweep >= 0 ? Mod(a - start) : Mod(start - a);
      if (travel <= std::abs(sweep) + 1e-12) at(a);
    }
  }
};
Bounds ExactBounds(const Map &geometry) {
  Bounds bounds;
  for (const auto &value : geometry.at("curves").To<Array>()) {
    const auto curve = value.To<Map>();
    const auto type = String(curve, "type");
    if (type == "polycurve") {
      const auto exact = MixedCurveBounds(curve);
      bounds.Add({exact[0], exact[1]});
      bounds.Add({exact[2], exact[3]});
      continue;
    }
    if (type == "circle" || type == "arc") {
      bounds.Arc(PointAt(curve.at("center")), Number(curve, "radius"),
                 type == "circle" ? 0. : Number(curve, "startAngleDeg") * std::numbers::pi / 180,
                 type == "circle" ? tau : Sweep(curve));
      continue;
    }
    const auto points = ToRing(curve.at("points").To<Array>());
    for (const auto p : points) bounds.Add(p);
    const auto bulges = curve.contains("bulges") ? curve.at("bulges").To<Array>() : Array{};
    const bool closed = curve.contains("closed") && curve.at("closed").To<bool>();
    const auto edges = points.size() - 1 + (closed ? 1 : 0);
    for (std::size_t i = 0; i < std::min(edges, bulges.size()); ++i) {
      const double bulge = Number(bulges[i]);
      if (bulge == 0.) continue;
      const auto a = points[i], b = points[(i + 1) % points.size()];
      const Point center{(a.X + b.X) / 2 - (b.Y - a.Y) * (1 - bulge * bulge) / (4 * bulge),
                         (a.Y + b.Y) / 2 + (b.X - a.X) * (1 - bulge * bulge) / (4 * bulge)};
      bounds.Arc(center, Length(Sub(a, center)), std::atan2(a.Y - center.Y, a.X - center.X),
                 4 * std::atan(bulge));
    }
  }
  return bounds;
}
Map Dimension(const Text &id, const Text &parameter, const Text &label,
              double value, bool editable, Point start, Point end, Point direction) {
  const Text kind = !editable ? "bounds" : parameter == "radius" ? "radius" :
      parameter == "length" ? "length" : "linear";
  return {{"id", id + ":" + parameter}, {"entityId", id},
          {"parameter", parameter}, {"key", parameter}, {"label", label}, {"kind", kind},
          {"value", value}, {"editable", editable}, {"start", Point3(start)},
          {"end", Point3(end)}, {"offsetDirection", Point3(direction)}};
}
Map AngleDimension(const Text &id, const Text &parameter, const Text &label,
                   double value, Point center, double radius, double start,
                   double sweep) {
  const auto at = [&](double degrees) {
    const double a = std::remainder(degrees, 360.) * std::numbers::pi / 180;
    return Add(center, {radius * std::cos(a), radius * std::sin(a)});
  };
  auto dimension = Dimension(id, parameter, label, value, true,
                             at(start), at(start + sweep), {0., 0.});
  dimension["kind"] = Text("angle");
  dimension["unit"] = Text("deg");
  dimension["center"] = Point3(center);
  dimension["radius"] = radius;
  dimension["startAngleDeg"] = start;
  dimension["sweepAngleDeg"] = sweep;
  return dimension;
}
} // namespace

Array DescribeDrawingDimensions(const Map &geometry, const Text &entityId) {
  const auto shape = Classify(geometry);
  if (shape.Kind == Shape::Radius || shape.Kind == Shape::Arc) {
    const double a = shape.Kind == Shape::Radius ? 0. :
        Number(shape.Curve, "startAngleDeg") * std::numbers::pi / 180 + Sweep(shape.Curve) / 2;
    const Point end = Add(shape.Origin, {shape.Width * std::cos(a), shape.Width * std::sin(a)});
    Array dimensions{Dimension(entityId, "radius", "R", shape.Width, true, shape.Origin, end,
                                {-std::sin(a), std::cos(a)})};
    if (shape.Kind == Shape::Arc) {
      const double start = Number(shape.Curve, "startAngleDeg"), sweep = SweepDegrees(shape.Curve);
      const double normalizedStart = NormalizeDegrees(start);
      dimensions.emplace_back(AngleDimension(entityId, "startAngleDeg", "α", normalizedStart,
                                               shape.Origin, shape.Width, 0., normalizedStart));
      dimensions.emplace_back(AngleDimension(entityId, "sweepAngleDeg", "θ", sweep,
                                               shape.Origin, shape.Width, start, sweep));
    }
    return dimensions;
  }
  if (shape.Kind == Shape::Line)
    return {Dimension(entityId, "length", "L", shape.Width, true, shape.Origin,
                      shape.Points[1], {-shape.U.Y, shape.U.X})};
  if (shape.Kind == Shape::Rectangle)
    return {Dimension(entityId, "width", "W", shape.Width, true, shape.Origin,
                      shape.Points[1], Mul(shape.V, -1.)),
            Dimension(entityId, "height", "H", shape.Height, true, shape.Origin,
                      shape.Points[3], Mul(shape.U, -1.))};
  const auto b = ExactBounds(geometry);
  return {Dimension(entityId, "width", "W", b.MaxX - b.MinX, false,
                    {b.MinX, b.MinY}, {b.MaxX, b.MinY}, {0., -1.}),
          Dimension(entityId, "height", "H", b.MaxY - b.MinY, false,
                    {b.MinX, b.MinY}, {b.MinX, b.MaxY}, {-1., 0.})};
}

Array EditDrawingDimensionCurves(const Map &geometry, const Map &request) {
  const auto shape = Classify(geometry);
  if (shape.Kind == Shape::ReadOnly)
    throw std::invalid_argument("PLANE_DIMENSION_READ_ONLY");
  const std::set<Text> allowed = shape.Kind == Shape::Arc ?
      std::set<Text>{"radius", "startAngleDeg", "sweepAngleDeg"} :
      shape.Kind == Shape::Radius ? std::set<Text>{"radius"} :
      shape.Kind == Shape::Line ? std::set<Text>{"length"} : std::set<Text>{"width", "height"};
  bool supplied = false;
  for (const auto *key : {"radius", "length", "width", "height", "startAngleDeg", "sweepAngleDeg"}) {
    if (!request.contains(key)) continue;
    if (!allowed.contains(key))
      throw std::invalid_argument("PLANE_DIMENSION_PARAMETER_UNSUPPORTED");
    supplied = true;
    const double value = Number(request.at(key));
    if (Text(key) == "sweepAngleDeg" && (!(value > 0.) || value > 360.))
      throw std::invalid_argument("PLANE_INVALID_ARC_SWEEP");
    if (Text(key) != "startAngleDeg" && !(value > 0.))
      throw std::invalid_argument("PLANE_INVALID_DIMENSION");
  }
  if (!supplied) throw std::invalid_argument("PLANE_DIMENSION_VALUE_REQUIRED");
  auto curve = shape.Curve;
  if (shape.Kind == Shape::Radius || shape.Kind == Shape::Arc) {
    if (request.contains("radius")) curve["radius"] = Number(request, "radius");
    if (shape.Kind == Shape::Arc &&
        (request.contains("startAngleDeg") || request.contains("sweepAngleDeg"))) {
      const double start = request.contains("startAngleDeg") ?
          NormalizeDegrees(Number(request, "startAngleDeg")) : Number(curve, "startAngleDeg");
      const double sweep = request.contains("sweepAngleDeg") ?
          Number(request, "sweepAngleDeg") : SweepDegrees(curve);
      if (request.contains("startAngleDeg")) curve["startAngleDeg"] = start;
      // Equal rays are the geometry format's explicit full-circle convention.
      // Use it directly so subtracting two rounded angles cannot turn 360 into
      // an almost-zero sweep. A tiny positive request must not round to that
      // same convention when its start is too large to represent its end.
      curve["endAngleDeg"] = sweep == 360. ? start : start + sweep;
      const double storedSweep = SweepDegrees(curve);
      if (sweep < 360. && storedSweep == 360.)
        throw std::invalid_argument("PLANE_INVALID_ARC_SWEEP");
    }
  }
  if (shape.Kind == Shape::Line) {
    curve["points"] = Array{Point2(shape.Origin),
        Point2(Add(shape.Origin, Mul(shape.U, Number(request, "length"))))};
  }
  if (shape.Kind == Shape::Rectangle) {
    const auto u = request.contains("width") ? Mul(shape.U, Number(request, "width")) :
        Sub(shape.Points[1], shape.Origin);
    const auto v = request.contains("height") ? Mul(shape.V, Number(request, "height")) :
        Sub(shape.Points[3], shape.Origin);
    const auto endU = request.contains("width") ? Add(shape.Origin, u) : shape.Points[1];
    const auto endV = request.contains("height") ? Add(shape.Origin, v) : shape.Points[3];
    Array points{Point2(shape.Origin), Point2(endU),
                 Point2(Add(Add(shape.Origin, u), v)), Point2(endV)};
    if (shape.RepeatedTail) points.emplace_back(Point2(shape.Origin));
    curve["points"] = points;
    if (curve.contains("bulges")) curve["bulges"] = Array(points.size(), iCAX::Data::Variant(0.));
  }
  return {curve};
}
} // namespace iCAX::GeometryServices::PlaneGeometryDetail
