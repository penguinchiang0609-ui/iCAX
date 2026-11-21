#include "PlaneGeometryAlgorithms.h"
#include "PlaneMixedCurves.h"
#include "OpenCascadeResourceImport/CadDocumentImportService.h"
#include <GCE2d_MakeSegment.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <IntRes2d_IntersectionPoint.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <gp_Circ2d.hxx>
#include <limits>
#include <numbers>
#include <type_traits>
namespace iCAX::GeometryServices::PlaneGeometryDetail {
namespace N = iCAX::GeometryServices::PlaneNesting;
namespace {
constexpr double pi = std::numbers::pi, tolerance = .001, chord = .05;
void CheckCancellation(const std::function<bool()> &cancelled) {
  if (cancelled && cancelled()) throw std::runtime_error("PLANE_GEOMETRY_CANCELLED");
}
double Scalar(const Text &text) {
  std::size_t n = 0;
  const double value = std::stod(text, &n);
  while (n < text.size() && std::isspace(static_cast<unsigned char>(text[n])))
    ++n;
  if (n != text.size() || !std::isfinite(value) || std::abs(value) > 1e9)
    throw std::invalid_argument("PLANE_DXF_INVALID_NUMBER");
  return value;
}
using Point = N::Point;
Point Pt(const iCAX::Data::Variant &v) {
  auto a = v.To<Array>();
  if (a.size() != 2)
    throw std::invalid_argument("PLANE_POINT_REQUIRES_XY");
  return {Number(a[0]), Number(a[1])};
}
Array P(Point p) { return {p.X, p.Y}; }
double Dist(Point a, Point b) { return std::hypot(a.X - b.X, a.Y - b.Y); }
Array Points(const N::Ring &ring) {
  Array a;
  for (auto p : ring)
    a.emplace_back(P(p));
  return a;
}
Map ClosedPolylineCurve(Map curve) {
  if (String(curve, "type") != "polyline")
    throw std::invalid_argument("PLANE_CLOSE_REQUIRES_SINGLE_OPEN_POLYLINE");
  if (curve.contains("closed") && curve.at("closed").To<bool>())
    throw std::invalid_argument("PLANE_POLYLINE_ALREADY_CLOSED");
  auto points = ToRing(curve.at("points").To<Array>());
  const auto sourceBulges =
      curve.contains("bulges") ? curve.at("bulges").To<Array>() : Array{};
  constexpr double samePoint = 1e-9;
  bool removedTail = false;
  while (points.size() > 1 &&
         Dist(points.front(), points.back()) <= samePoint) {
    points.pop_back();
    removedTail = true;
  }
  if (points.size() < 3)
    throw std::invalid_argument("PLANE_CLOSE_REQUIRES_THREE_DISTINCT_POINTS");
  N::Ring distinct;
  for (std::size_t i = 0; i < points.size(); ++i) {
    if (Dist(points[i], points[(i + 1) % points.size()]) <= samePoint)
      throw std::invalid_argument("PLANE_CLOSE_ZERO_LENGTH_EDGE");
    if (distinct.size() < 3 &&
        std::none_of(distinct.begin(), distinct.end(),
                     [&](Point p) { return Dist(points[i], p) <= samePoint; }))
      distinct.push_back(points[i]);
  }
  if (distinct.size() < 3)
    throw std::invalid_argument("PLANE_CLOSE_REQUIRES_THREE_DISTINCT_POINTS");
  Array bulges;
  for (std::size_t i = 0; i < points.size(); ++i) {
    // An open curve's last bulge has no segment. Only an existing repeated
    // endpoint makes the preceding bulge an already used final segment.
    const bool existingSegment = i + 1 < points.size() || removedTail;
    bulges.emplace_back(existingSegment && i < sourceBulges.size()
                            ? Number(sourceBulges[i])
                            : 0.);
  }
  curve["points"] = Points(points);
  curve["bulges"] = std::move(bulges);
  curve["closed"] = true;
  return curve;
}
void Snap(Array &a, const char *type, Point p) {
  a.emplace_back(Map{{"type", Text(type)}, {"point", P(p)}});
}
double Sweep(double start, double end) {
  double s = std::fmod(end - start, 360.);
  if (s <= 0)
    s += 360.;
  return s;
}
N::Ring Arc(Point c, double r, double start, double sweep) {
  if (!(r > 0) || !std::isfinite(r))
    throw std::invalid_argument("PLANE_INVALID_RADIUS");
  const double maxAngle = 2 * std::acos(std::clamp(1 - chord / r, -1., 1.));
  const double required = std::ceil(std::abs(sweep * pi / 180) / maxAngle);
  if (!std::isfinite(required) || required > 100000)
    throw std::invalid_argument("PLANE_CURVE_TOO_LARGE");
  const auto count = std::max(2, static_cast<int>(required));
  std::vector<double> parameters;
  for (int i = 0; i <= count; ++i)
    parameters.push_back(static_cast<double>(i) / count);
  // Include the analytic extrema as well as the chord-bound samples. The
  // resulting bounds describe the original circular curve, not its chords.
  const auto firstQuadrant =
      static_cast<long long>(std::ceil(std::min(start, start + sweep) / 90.));
  const auto lastQuadrant =
      static_cast<long long>(std::floor(std::max(start, start + sweep) / 90.));
  for (auto quadrant = firstQuadrant; quadrant <= lastQuadrant; ++quadrant) {
    const double t = (quadrant * 90. - start) / sweep;
    if (t > 0 && t < 1)
      parameters.push_back(t);
  }
  std::sort(parameters.begin(), parameters.end());
  parameters.erase(std::unique(parameters.begin(), parameters.end()),
                   parameters.end());
  N::Ring path;
  for (double t : parameters) {
    const auto a = std::remainder(start + sweep * t, 360.) * pi / 180;
    path.push_back({c.X + r * std::cos(a), c.Y + r * std::sin(a)});
  }
  return path;
}
N::Ring CurvePath(const Map &curve, Array *snaps = nullptr,
                  const std::function<bool()> &cancelled = {}) {
  const auto type = String(curve, "type");
  if (type == "polycurve")
    return MixedCurvePath(curve, snaps, cancelled);
  N::Ring path;
  if (type == "circle" || type == "arc") {
    const auto center = Pt(curve.at("center"));
    const auto r = Number(curve, "radius"),
               start = Number(curve, "startAngleDeg"),
               sweep = type == "circle"
                           ? 360.
                           : Sweep(start, Number(curve, "endAngleDeg"));
    path = Arc(center, r, start, sweep);
    if (snaps) {
      Snap(*snaps, "center", center);
      if (type == "arc") {
        Snap(*snaps, "end", path.front());
        Snap(*snaps, "end", path.back());
        const double a = (start + sweep / 2) * pi / 180;
        Snap(*snaps, "mid",
             {center.X + r * std::cos(a), center.Y + r * std::sin(a)});
      }
    }
  } else {
    const auto vertices = ToRing(curve.at("points").To<Array>());
    if (vertices.size() < 2)
      throw std::invalid_argument("PLANE_NOT_ENOUGH_POINTS");
    const bool closed =
        curve.contains("closed") && curve.at("closed").To<bool>();
    const auto bulges =
        curve.contains("bulges") ? curve.at("bulges").To<Array>() : Array{};
    path.push_back(vertices[0]);
    const auto edges = vertices.size() - 1 + (closed ? 1 : 0);
    for (std::size_t i = 0; i < edges; ++i) {
      auto a = vertices[i], b = vertices[(i + 1) % vertices.size()];
      const double bulge = i < bulges.size() ? Number(bulges[i]) : 0;
      if (snaps)
        Snap(*snaps, "end", a);
      if (bulge == 0.) {
        path.push_back(b);
        if (snaps)
          Snap(*snaps, "mid", {(a.X + b.X) / 2, (a.Y + b.Y) / 2});
      } else {
        const double len = Dist(a, b);
        if (len <= tolerance)
          throw std::invalid_argument("PLANE_DEGENERATE_ARC");
        const Point center{
            (a.X + b.X) / 2 - (b.Y - a.Y) * (1 - bulge * bulge) / (4 * bulge),
            (a.Y + b.Y) / 2 + (b.X - a.X) * (1 - bulge * bulge) / (4 * bulge)};
        const double radius = Dist(a, center),
                     start =
                         std::atan2(a.Y - center.Y, a.X - center.X) * 180 / pi,
                     sweep = 4 * std::atan(bulge) * 180 / pi;
        auto arc = Arc(center, radius, start, sweep);
        path.insert(path.end(), arc.begin() + 1, arc.end());
        if (snaps) {
          Snap(*snaps, "center", center);
          const auto angle = (start + sweep / 2) * pi / 180;
          Snap(*snaps, "mid",
               {center.X + radius * std::cos(angle),
                center.Y + radius * std::sin(angle)});
        }
      }
    }
    if (snaps && !closed)
      Snap(*snaps, "end", vertices.back());
  }
  return path;
}
double SignedArea(const N::Ring &r) {
  double area = 0;
  for (std::size_t i = 0; i < r.size(); ++i) {
    auto a = r[i], b = r[(i + 1) % r.size()];
    area += a.X * b.Y - b.X * a.Y;
  }
  return area / 2;
}
bool Inside(Point p, const N::Ring &r) {
  bool inside = false;
  for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
    auto a = r[i], b = r[j];
    if ((a.Y > p.Y) != (b.Y > p.Y) &&
        p.X < (b.X - a.X) * (p.Y - a.Y) / (b.Y - a.Y) + a.X)
      inside = !inside;
  }
  return inside;
}
void ValidateMaterialCurves(const Array &curves, const std::function<bool()> &cancelled) {
  struct Edge {
    Handle(Geom2d_Curve) Curve;
    Point A, B;
    bool Closed = false;
    bool CheckSelfIntersection = false;
  };
  std::vector<Edge> edges;
  const auto circular = [&](Point center, double radius, double start,
                            double sweep, bool closed) {
    if (!(radius > 0))
      throw std::invalid_argument("PLANE_INVALID_RADIUS");
    Handle(Geom2d_Circle) circle = new Geom2d_Circle(gp_Circ2d(
        gp_Ax2d(gp_Pnt2d(center.X, center.Y), gp_Dir2d(1, 0)), radius));
    const auto point = [&](double degrees) {
      const double a = degrees * pi / 180;
      return Point{center.X + radius * std::cos(a),
                   center.Y + radius * std::sin(a)};
    };
    if (closed) {
      edges.push_back({circle, {}, {}, true});
      return;
    }
    const double first = std::min(start, start + sweep) * pi / 180,
                 last = std::max(start, start + sweep) * pi / 180;
    Handle(Geom2d_Curve) trimmed = new Geom2d_TrimmedCurve(circle, first, last);
    edges.push_back({trimmed, point(start), point(start + sweep), false});
  };
  try {
    for (const auto &value : curves) {
      CheckCancellation(cancelled);
      const auto c = value.To<Map>();
      const auto type = String(c, "type");
      if (type == "polycurve") {
        for (const auto &edge : MixedCurveEdges(c))
          edges.push_back({edge.Curve, edge.A, edge.B, edge.Closed,
                           edge.CheckSelfIntersection});
        continue;
      }
      if (type == "circle" || type == "arc") {
        circular(Pt(c.at("center")), Number(c, "radius"),
                 Number(c, "startAngleDeg"),
                 type == "circle" ? 360.
                                  : Sweep(Number(c, "startAngleDeg"),
                                          Number(c, "endAngleDeg")),
                 type == "circle");
        continue;
      }
      const auto points = ToRing(c.at("points").To<Array>());
      if (points.size() < 2)
        throw std::invalid_argument("PLANE_NOT_ENOUGH_POINTS");
      const auto bulges =
          c.contains("bulges") ? c.at("bulges").To<Array>() : Array{};
      const bool closed = c.contains("closed") && c.at("closed").To<bool>();
      const auto count = points.size() - 1 + (closed ? 1 : 0);
      for (std::size_t i = 0; i < count; ++i) {
        CheckCancellation(cancelled);
        const auto a = points[i], b = points[(i + 1) % points.size()];
        if (Dist(a, b) <= 1e-9)
          throw std::invalid_argument("PLANE_DEGENERATE_MATERIAL_EDGE");
        const double bulge = i < bulges.size() ? Number(bulges[i]) : 0.;
        if (bulge == 0.) {
          Handle(Geom2d_Curve) segment =
              GCE2d_MakeSegment(gp_Pnt2d(a.X, a.Y), gp_Pnt2d(b.X, b.Y)).Value();
          edges.push_back({segment, a, b, false});
        } else {
          const Point center{
              (a.X + b.X) / 2 - (b.Y - a.Y) * (1 - bulge * bulge) / (4 * bulge),
              (a.Y + b.Y) / 2 +
                  (b.X - a.X) * (1 - bulge * bulge) / (4 * bulge)};
          circular(center, Dist(a, center),
                   std::atan2(a.Y - center.Y, a.X - center.X) * 180 / pi,
                   4 * std::atan(bulge) * 180 / pi, false);
        }
      }
    }
    if (edges.size() > 2048)
      throw std::invalid_argument("PLANE_MATERIAL_EDGE_LIMIT");
    const auto atBoundary = [](const Edge &edge, double parameter,
                               Point intersection, bool first) {
      return CurvePointAtBoundary(edge.Curve, parameter, intersection, first);
    };
    for (const auto &edge : edges) {
      CheckCancellation(cancelled);
      if (!edge.CheckSelfIntersection) continue;
      Geom2dAPI_InterCurveCurve self(edge.Curve, 1e-8);
      if (!self.Intersector().IsDone())
        throw std::invalid_argument(
            "PLANE_MATERIAL_GEOMETRY_CHECK_FAILED: self-intersection did not complete");
      if (self.NbSegments())
        throw std::invalid_argument("PLANE_MATERIAL_SELF_INTERSECTING_CURVE");
      for (int index = 1; index <= self.NbPoints(); ++index) {
        const auto &point = self.Intersector().Point(index);
        const double u = point.ParamOnFirst(), v = point.ParamOnSecond();
        const auto exact = point.Value();
        const Point p{exact.X(), exact.Y()};
        const bool closingEndpoint = edge.Closed &&
            ((atBoundary(edge, u, p, true) && atBoundary(edge, v, p, false)) ||
             (atBoundary(edge, v, p, true) && atBoundary(edge, u, p, false)));
        if (!closingEndpoint)
          throw std::invalid_argument("PLANE_MATERIAL_SELF_INTERSECTING_CURVE");
      }
    }
    std::vector<Point> ends;
    for (const auto &edge : edges)
      if (!edge.Closed) {
        ends.push_back(edge.A);
        ends.push_back(edge.B);
      }
    for (auto p : ends) {
      CheckCancellation(cancelled);
      const auto degree = std::count_if(ends.begin(), ends.end(), [&](Point q) {
        return Dist(p, q) <= tolerance;
      });
      if (degree != 2)
        throw std::invalid_argument(
            "PLANE_MATERIAL_JUNCTION_NOT_CLOSED_SIMPLE");
    }
    for (std::size_t i = 0; i < edges.size(); ++i)
      for (std::size_t j = i + 1; j < edges.size(); ++j) {
        CheckCancellation(cancelled);
        ValidateLineSplineMaterialIntersections(edges[i].Curve, edges[j].Curve, cancelled);
        Geom2dAPI_InterCurveCurve intersection(edges[i].Curve, edges[j].Curve,
                                               1e-8);
        if (!intersection.Intersector().IsDone())
          throw std::invalid_argument(
              "PLANE_MATERIAL_GEOMETRY_CHECK_FAILED: curve intersection did not complete");
        if (intersection.NbSegments())
          throw std::invalid_argument("PLANE_MATERIAL_OVERLAPPING_CURVES");
        // Non-overlapping straight segments with the exact same declared
        // endpoint can intersect only there. OCCT can return a slightly
        // extrapolated parameter for rotated edges; this geometric fact
        // avoids rejecting a valid polygon/star without loosening spline
        // endpoint tolerances or accepting overlapping straight segments.
        const auto samePoint = [](Point a, Point b) { return a.X == b.X && a.Y == b.Y; };
        const bool sharedLineEndpoint = !edges[i].Closed && !edges[j].Closed &&
            Geom2dAdaptor_Curve(edges[i].Curve).GetType() == GeomAbs_Line &&
            Geom2dAdaptor_Curve(edges[j].Curve).GetType() == GeomAbs_Line &&
            (samePoint(edges[i].A, edges[j].A) || samePoint(edges[i].A, edges[j].B) ||
             samePoint(edges[i].B, edges[j].A) || samePoint(edges[i].B, edges[j].B));
        if (sharedLineEndpoint) continue;
        for (int k = 1; k <= intersection.NbPoints(); ++k) {
          const auto &point = intersection.Intersector().Point(k);
          const auto q = point.Value();
          const Point p{q.X(), q.Y()};
          const auto isEnd = [&](const Edge &edge, double parameter) {
            return !edge.Closed && (atBoundary(edge, parameter, p, true) ||
                                    atBoundary(edge, parameter, p, false));
          };
          if (!isEnd(edges[i], point.ParamOnFirst()) ||
              !isEnd(edges[j], point.ParamOnSecond()))
            throw std::invalid_argument("PLANE_MATERIAL_INTERSECTING_CURVES");
        }
      }
  } catch (const Standard_Failure &e) {
    throw std::invalid_argument(Text("PLANE_MATERIAL_GEOMETRY_CHECK_FAILED: ") +
                                e.GetMessageString());
  }
}
Map Build(Array curves, bool material, const std::function<bool()> &cancelled = {}) {
  CheckCancellation(cancelled);
  for (auto &value : curves) {
    CheckCancellation(cancelled);
    const auto curve = value.To<Map>();
    if (String(curve, "type") == "polycurve")
      value = NormalizeMixedCurve(curve);
  }
  if (material)
    ValidateMaterialCurves(curves, cancelled);
  Array paths, snaps;
  std::vector<N::Ring> rings;
  double minX = std::numeric_limits<double>::max(), minY = minX, maxX = -minX,
         maxY = -minX;
  for (const auto &c : curves) {
    CheckCancellation(cancelled);
    auto curve = c.To<Map>();
    auto ring = CurvePath(curve, &snaps, cancelled);
    for (auto p : ring) {
      if (!std::isfinite(p.X) || !std::isfinite(p.Y) || std::abs(p.X) > 1e9 ||
          std::abs(p.Y) > 1e9)
        throw std::invalid_argument("PLANE_COORDINATE_OUT_OF_RANGE");
      minX = std::min(minX, p.X);
      maxX = std::max(maxX, p.X);
      minY = std::min(minY, p.Y);
      maxY = std::max(maxY, p.Y);
    }
    if (String(curve, "type") == "polycurve") {
      const auto bounds = MixedCurveBounds(curve);
      minX = std::min(minX, bounds[0]); minY = std::min(minY, bounds[1]);
      maxX = std::max(maxX, bounds[2]); maxY = std::max(maxY, bounds[3]);
    }
    const bool closed = Dist(ring.front(), ring.back()) <= tolerance;
    paths.emplace_back(Map{{"points", Points(ring)}, {"closed", closed}});
    rings.push_back(std::move(ring));
  }
  if (curves.empty())
    throw std::invalid_argument("PLANE_EMPTY_GEOMETRY");
  double approximationBound = 0;
  for (const auto &value : curves) {
    const auto c = value.To<Map>();
    if (String(c, "type") == "polycurve")
      approximationBound = std::max(approximationBound, MixedCurveApproximationBound(c));
    if (String(c, "type") == "arc" || String(c, "type") == "circle")
      approximationBound = std::max(approximationBound, chord);
    if (c.contains("bulges"))
      for (const auto &bulge : c.at("bulges").To<Array>())
        if (Number(bulge) != 0.)
          approximationBound = std::max(approximationBound, chord);
  }
  Map geometry{
      {"schemaVersion", 1},    {"units", Text("mm")},
      {"curves", curves},      {"paths", paths},
      {"snapPoints", snaps},   {"width", maxX - minX},
      {"height", maxY - minY}, {"approximationBoundMm", approximationBound}};
  if (material) {
    std::vector<N::Ring> joined;
    double joinDeviation = 0;
    while (!rings.empty()) {
      auto path = std::move(rings.back());
      rings.pop_back();
      while (Dist(path.front(), path.back()) > tolerance) {
        bool found = false;
        for (auto it = rings.begin(); it != rings.end(); ++it) {
          if (Dist(path.back(), it->back()) <= tolerance)
            std::reverse(it->begin(), it->end());
          if (Dist(path.back(), it->front()) <= tolerance) {
            joinDeviation =
                std::max(joinDeviation, Dist(path.back(), it->front()));
            path.insert(path.end(), it->begin() + 1, it->end());
            rings.erase(it);
            found = true;
            break;
          }
        }
        if (!found)
          throw std::invalid_argument("PLANE_MATERIAL_CONTOUR_OPEN");
      }
      joinDeviation = std::max(joinDeviation, Dist(path.front(), path.back()));
      path.pop_back();
      if (path.size() < 3 || std::abs(SignedArea(path)) < tolerance * tolerance)
        throw std::invalid_argument("PLANE_DEGENERATE_MATERIAL");
      joined.push_back(std::move(path));
    }
    std::sort(joined.begin(), joined.end(), [](const auto &a, const auto &b) {
      return std::abs(SignedArea(a)) > std::abs(SignedArea(b));
    });
    if (SignedArea(joined[0]) < 0)
      std::reverse(joined[0].begin(), joined[0].end());
    Array holes;
    for (std::size_t i = 1; i < joined.size(); ++i) {
      for (auto p : joined[i])
        if (!Inside(p, joined[0]))
          throw std::invalid_argument(
              "PLANE_MULTIPLE_OUTER_CONTOURS_UNSUPPORTED");
      for (std::size_t j = 1; j < i; ++j)
        if (Inside(joined[i][0], joined[j]))
          throw std::invalid_argument("PLANE_NESTED_ISLANDS_UNSUPPORTED");
      if (SignedArea(joined[i]) > 0)
        std::reverse(joined[i].begin(), joined[i].end());
      holes.emplace_back(Points(joined[i]));
    }
    geometry["outer"] = Points(joined[0]);
    geometry["holes"] = holes;
    geometry["holeCount"] = static_cast<UInt>(holes.size());
    geometry["approximationBoundMm"] = approximationBound + joinDeviation;
  }
  return geometry;
}
} // namespace
Map BuildGeometry(const Array &curves, bool material, const std::function<bool()> &cancelled) { return Build(curves, material, cancelled); }
void ValidateClosedCurveIntersections(const Array &curves, const std::function<bool()> &cancelled) { ValidateMaterialCurves(curves, cancelled); }
Map CreateGeometry(const Map &request) {
  const auto primitive = String(request, "primitive");
  if (primitive == "ellipse" || primitive == "ellipseArc" || primitive == "bezier" || primitive == "bspline") {
    Map segment{{"type", primitive == "ellipse" ? Text("ellipseArc") : primitive}};
    for (const auto *key : {"center", "radiusX", "radiusY", "rotationDeg", "startAngleDeg", "sweepAngleDeg",
                            "controlPoints", "degree", "knots", "weights"})
      if (request.contains(key)) segment[key] = request.at(key);
    if (primitive == "ellipse") { segment["startAngleDeg"] = 0.; segment["sweepAngleDeg"] = 360.; }
    if (primitive == "bspline" && !segment.contains("degree")) segment["degree"] = 3;
    const bool closed = (primitive == "ellipse" || primitive == "ellipseArc") && std::abs(Number(segment, "sweepAngleDeg")) == 360.;
    return Build(Array{NormalizeMixedCurve({{"segments", Array{segment}}, {"closed", closed}})}, false);
  }
  if (primitive == "triangle" || primitive == "polygon" || primitive == "star") {
    N::Ring vertices;
    if (primitive == "triangle") {
      vertices = ToRing(request.at("points").To<Array>());
      if (vertices.size() != 3) throw std::invalid_argument("PLANE_TRIANGLE_REQUIRES_THREE_POINTS");
    } else {
      const bool star = primitive == "star";
      const auto center = Pt(request.at("center"));
      const auto radius = Number(request, star ? "outerRadius" : "radius");
      const auto inner = star ? Number(request, "innerRadius") : radius;
      if (!(radius > 0) || !(inner > 0) || (star && !(inner < radius)))
        throw std::invalid_argument(star ? "PLANE_INVALID_STAR_RADII" : "PLANE_INVALID_POLYGON_RADIUS");
      const auto rawCount = Number(request, star ? "tips" : "sides", star ? 5. : 6.);
      if (rawCount != std::floor(rawCount) || rawCount < 3 || rawCount > 64)
        throw std::invalid_argument(star ? "PLANE_INVALID_STAR_TIPS" : "PLANE_INVALID_POLYGON_SIDES");
      const auto count = static_cast<std::size_t>(rawCount) * (star ? 2 : 1);
      const double rotation = std::remainder(Number(request, "rotationDeg"), 360.) * pi / 180.;
      for (std::size_t index = 0; index < count; ++index) {
        const double angle = rotation + 2 * pi * index / count, r = star && index % 2 ? inner : radius;
        vertices.push_back({center.X + r * std::cos(angle), center.Y + r * std::sin(angle)});
      }
    }
    for (std::size_t index = 0; index < vertices.size(); ++index)
      for (std::size_t other = index + 1; other < vertices.size(); ++other)
        if (Dist(vertices[index], vertices[other]) <= 1e-9) throw std::invalid_argument("PLANE_DEGENERATE_DRAWING_PRIMITIVE");
    if (primitive == "triangle") {
      const auto a = vertices[0], b = vertices[1], c = vertices[2];
      const double area2 = (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
      const double edge = std::max({Dist(a, b), Dist(b, c), Dist(c, a)});
      if (std::abs(area2) <= 1e-9 * edge) throw std::invalid_argument("PLANE_DEGENERATE_DRAWING_PRIMITIVE");
    }
    return Build(Array{Map{{"type", Text("polyline")}, {"closed", true}, {"points", Points(vertices)}}}, false);
  }
  Map curve{{"type", primitive}};
  // Geometry resources carry only geometric parameters, never owner IDs,
  // command revisions, async flags or names supplied by a business caller.
  for (const auto *key : {"points", "closed", "bulges", "center", "radius", "startAngleDeg", "endAngleDeg"})
    if (request.contains(key)) curve[key] = request.at(key);
  if (primitive == "polyline" && request.contains("segments")) {
    curve = NormalizeMixedCurve(request);
  } else if (primitive == "rect") {
    const auto points = request.at("points").To<Array>();
    if (points.size() != 1)
      throw std::invalid_argument("PLANE_RECT_ORIGIN_REQUIRED");
    const auto p = Pt(points[0]);
    const double w = Number(request, "width"), h = Number(request, "height");
    if (w <= 0 || h <= 0)
      throw std::invalid_argument("PLANE_INVALID_RECTANGLE");
    curve = {{"type", Text("polyline")},
             {"points", Array{P(p), P({p.X + w, p.Y}), P({p.X + w, p.Y + h}),
                              P({p.X, p.Y + h})}},
             {"closed", true}};
  } else if (primitive == "line") {
    if (request.at("points").To<Array>().size() != 2)
      throw std::invalid_argument("PLANE_LINE_REQUIRES_TWO_POINTS");
    curve["closed"] = false;
  } else if (primitive != "polyline" && primitive != "circle" &&
             primitive != "arc")
    throw std::invalid_argument("PLANE_DRAW_PRIMITIVE_UNSUPPORTED");
  return Build({curve}, false);
}
Map ImportGeometry(const iCAX::CAD::SCadExchangeDocument &source, bool material,
                   const Text &explicitUnits) {
  double scale = 1;
  Text inputUnits, unitSource;
  const auto units = source.Document.Header.find("$INSUNITS");
  if (units != source.Document.Header.end())
    for (const auto &g : units->second)
      if (g.Code == 70) {
        const double raw = Scalar(g.Value);
        if (raw != std::floor(raw))
          throw std::invalid_argument("PLANE_DXF_UNIT_UNSUPPORTED");
        const int code = static_cast<int>(raw);
        if (code == 1) {
          scale = 25.4;
          inputUnits = "in";
        } else if (code == 5) {
          scale = 10;
          inputUnits = "cm";
        } else if (code == 6) {
          scale = 1000;
          inputUnits = "m";
        } else if (code == 4) {
          scale = 1;
          inputUnits = "mm";
        } else if (code != 0)
          throw std::invalid_argument("PLANE_DXF_UNIT_UNSUPPORTED");
        if (code != 0)
          unitSource = "DXF.INSUNITS";
      }
  if (inputUnits.empty()) {
    inputUnits = explicitUnits;
    unitSource = "Explicit";
    if (inputUnits == "mm")
      scale = 1;
    else if (inputUnits == "cm")
      scale = 10;
    else if (inputUnits == "m")
      scale = 1000;
    else if (inputUnits == "in")
      scale = 25.4;
    else
      throw std::invalid_argument(
          "PLANE_DXF_UNITS_REQUIRED: provide units mm, cm, m or in");
  }
  Array curves;
  const auto entities = source.Document.Sections.find("ENTITIES");
  if (entities == source.Document.Sections.end())
    throw std::invalid_argument("PLANE_DXF_NO_ENTITIES");
  for (const auto &record : entities->second) {
    const auto number = [&](int code, double fallback = 0.) {
      for (const auto &g : record.Values)
        if (g.Code == code)
          return Scalar(g.Value);
      return fallback;
    };
    if (record.Type == "ENDSEC" || record.Type == "SEQEND")
      continue;
    if (record.Type == "TEXT" || record.Type == "MTEXT" ||
        record.Type == "DIMENSION" || record.Type == "HATCH" ||
        record.Type == "POINT")
      throw std::invalid_argument("PLANE_DXF_ENTITY_UNSUPPORTED: " +
                                  record.Type);
    if (std::abs(number(30)) > tolerance || std::abs(number(31)) > tolerance ||
        std::abs(number(38)) > tolerance || std::abs(number(210)) > tolerance ||
        std::abs(number(220)) > tolerance ||
        std::abs(number(230, 1) - 1) > tolerance)
      throw std::invalid_argument("PLANE_DXF_NON_XY_ENTITY");
    Map curve;
    if (record.Type == "LINE")
      curve = {{"type", Text("line")},
               {"points", Array{P({number(10) * scale, number(20) * scale}),
                                P({number(11) * scale, number(21) * scale})}},
               {"closed", false}};
    else if (record.Type == "CIRCLE" || record.Type == "ARC") {
      curve = {{"type", Text(record.Type == "CIRCLE" ? "circle" : "arc")},
               {"center", P({number(10) * scale, number(20) * scale})},
               {"radius", number(40) * scale}};
      if (record.Type == "ARC") {
        curve["startAngleDeg"] = number(50);
        curve["endAngleDeg"] = number(51);
      }
    } else if (record.Type == "LWPOLYLINE") {
      Array vertices, bulges;
      Point p;
      double bulge = 0;
      bool have = false, haveY = false;
      const auto flush = [&]() {
        if (have) {
          if (!haveY)
            throw std::invalid_argument("PLANE_DXF_VERTEX_MISSING_Y");
          vertices.emplace_back(P(p));
          bulges.emplace_back(bulge);
        }
      };
      for (const auto &g : record.Values) {
        if (g.Code == 10) {
          flush();
          p = {Scalar(g.Value) * scale, 0};
          bulge = 0;
          have = true;
          haveY = false;
        } else if (g.Code == 20) {
          p.Y = Scalar(g.Value) * scale;
          haveY = true;
        } else if (g.Code == 42)
          bulge = Scalar(g.Value);
      }
      flush();
      curve = {{"type", Text("polyline")},
               {"points", vertices},
               {"bulges", bulges},
               {"closed", (static_cast<int>(number(70)) & 1) != 0}};
    } else
      throw std::invalid_argument("PLANE_DXF_ENTITY_UNSUPPORTED: " +
                                  record.Type);
    curves.emplace_back(std::move(curve));
  }
  auto geometry = Build(std::move(curves), material);
  geometry["sourceUnits"] = inputUnits;
  geometry["unitSource"] = unitSource;
  return geometry;
}
Map DescribeDrawingClosure(const Map &geometry) {
  const auto curves = geometry.at("curves").To<Array>();
  const auto type = curves.size() == 1 ? String(curves.front().To<Map>(), "type") : Text{};
  const bool isPolyline = type == "polyline" || type == "polycurve";
  bool closed = false, canClose = false;
  if (isPolyline) {
    const auto curve = curves.front().To<Map>();
    closed = curve.contains("closed") && curve.at("closed").To<bool>();
    if (!closed) {
      try {
        if (type == "polycurve") (void)CloseMixedCurve(curve);
        else (void)ClosedPolylineCurve(curve);
        canClose = true;
      } catch (const std::invalid_argument &) {
      }
    }
  } else {
    const auto paths = geometry.at("paths").To<Array>();
    closed =
        !paths.empty() && std::all_of(paths.begin(), paths.end(),
                                      [](const iCAX::Data::Variant &value) {
                                        const auto path = value.To<Map>();
                                        return path.contains("closed") &&
                                               path.at("closed").To<bool>();
                                      });
  }
  return {{"isPolyline", isPolyline},
          {"closed", closed},
          {"canClosePolyline", canClose}};
}
Map TransformGeometry(Map geometry, const Map &request) {
  const auto operation = String(request, "operation");
  // Material topology belongs to the geometry document. Rebuild the
  // transformed/edited material from its exact curves so its outer, holes
  // and count stay consistent; free drawing curves keep their own meaning.
  const bool material = geometry.contains("outer") || geometry.contains("holes") || geometry.contains("holeCount");
  if (operation == "dimensions")
    return Build(EditDrawingDimensionCurves(geometry, request), material);
  if (operation == "nodes") {
    const auto curves = geometry.at("curves").To<Array>();
    if (curves.size() != 1) throw std::invalid_argument("PLANE_NODE_EDIT_REQUIRES_SINGLE_CURVE");
    auto curve = curves.front().To<Map>();
    const auto type = String(curve, "type");
    if (type != "line" && type != "polyline")
      throw std::invalid_argument("PLANE_NODE_EDIT_REQUIRES_POINT_CURVE");
    const auto points = request.at("points").To<Array>();
    if (points.size() != curve.at("points").To<Array>().size())
      throw std::invalid_argument("PLANE_NODE_EDIT_POINT_COUNT_CHANGED");
    curve["points"] = points;
    return Build({curve}, material);
  }
  if (operation == "close") {
    const auto curves = geometry.at("curves").To<Array>();
    if (curves.size() != 1)
      throw std::invalid_argument("PLANE_CLOSE_REQUIRES_SINGLE_OPEN_POLYLINE");
    const auto curve = curves.front().To<Map>();
    return Build(Array{iCAX::Data::Variant(String(curve, "type") == "polycurve"
                     ? CloseMixedCurve(curve) : ClosedPolylineCurve(curve))}, material);
  }
  const double dx = Number(request, "dx"), dy = Number(request, "dy"),
               angle = Number(request, "angleDeg") * pi / 180,
               factor = Number(request, "factor", 1),
               cx = Number(request, "cx"), cy = Number(request, "cy");
  const auto axis = String(request, "axis", "x");
  if (operation != "move" && operation != "copy" && operation != "rotate" && operation != "scale" && operation != "mirror")
    throw std::invalid_argument("PLANE_DRAW_EDIT_UNSUPPORTED");
  if (operation == "mirror" && axis != "x" && axis != "y")
    throw std::invalid_argument("PLANE_INVALID_MIRROR_AXIS");
  if (operation == "scale" && factor <= 0)
    throw std::invalid_argument("PLANE_INVALID_SCALE");
  const auto transform = [&](Point p) {
    if (operation == "move" || operation == "copy")
      return Point{p.X + dx, p.Y + dy};
    if (operation == "mirror")
      return axis == "x" ? Point{p.X, 2 * cy - p.Y} : Point{2 * cx - p.X, p.Y};
    if (operation == "scale")
      return Point{cx + (p.X - cx) * factor, cy + (p.Y - cy) * factor};
    return Point{cx + (p.X - cx) * std::cos(angle) - (p.Y - cy) * std::sin(angle),
                 cy + (p.X - cx) * std::sin(angle) + (p.Y - cy) * std::cos(angle)};
  };
  Array curves;
  for (const auto &value : geometry.at("curves").To<Array>()) {
    auto curve = value.To<Map>();
    if (String(curve, "type") == "polycurve") {
      curves.emplace_back(TransformMixedCurve(curve, request));
      continue;
    }
    if (curve.contains("points")) {
      auto ring = ToRing(curve.at("points").To<Array>());
      for (auto &p : ring)
        p = transform(p);
      curve["points"] = Points(ring);
    }
    if (curve.contains("center"))
      curve["center"] = P(transform(Pt(curve.at("center"))));
    if (operation == "scale" && curve.contains("radius"))
      curve["radius"] = Number(curve, "radius") * factor;
    if (operation == "rotate" && String(curve, "type") == "arc") {
      curve["startAngleDeg"] =
          Number(curve, "startAngleDeg") + Number(request, "angleDeg");
      curve["endAngleDeg"] =
          Number(curve, "endAngleDeg") + Number(request, "angleDeg");
    }
    if (operation == "mirror") {
      if (curve.contains("bulges")) {
        auto bulges = curve.at("bulges").To<Array>();
        for (auto &bulge : bulges) bulge = -Number(bulge);
        curve["bulges"] = std::move(bulges);
      }
      if (String(curve, "type") == "arc") {
        const double oldStart = Number(curve, "startAngleDeg"), oldEnd = Number(curve, "endAngleDeg");
        const auto reflectedAngle = [&](double degrees) { return axis == "x" ? -degrees : 180. - degrees; };
        curve["startAngleDeg"] = reflectedAngle(oldEnd);
        curve["endAngleDeg"] = reflectedAngle(oldStart);
      }
    }
    curves.emplace_back(std::move(curve));
  }
  return Build(std::move(curves), material);
}
Array ArrayGeometry(const Map &geometry, const Map &request, const std::function<bool()> &cancelled) {
  CheckCancellation(cancelled);
  const auto operation = String(request, "operation");
  const auto count = [&](const Text &key, double fallback) {
    const double value = Number(request, key, fallback);
    if (value != std::floor(value) || value < 1 || value > 500)
      throw std::invalid_argument("PLANE_INVALID_ARRAY_COUNT");
    return static_cast<int>(value);
  };
  Array result{geometry};
  if (operation == "array-rectangular") {
    const int rows = count("rows", 1), columns = count("columns", 1);
    if (rows * columns < 2 || rows * columns > 500)
      throw std::invalid_argument("PLANE_INVALID_ARRAY_COUNT");
    const double sx = Number(request, "spacingX"), sy = Number(request, "spacingY");
    for (int row = 0; row < rows; ++row)
      for (int column = 0; column < columns; ++column) {
        CheckCancellation(cancelled);
        if (!row && !column) continue;
        result.emplace_back(TransformGeometry(geometry, {{"operation", Text("copy")}, {"dx", column * sx}, {"dy", row * sy}}));
      }
  } else if (operation == "array-polar") {
    const int instances = count("count", 2);
    if (instances < 2) throw std::invalid_argument("PLANE_INVALID_ARRAY_COUNT");
    const double totalAngle = Number(request, "angleDeg", 360.), cx = Number(request, "cx"), cy = Number(request, "cy");
    if (totalAngle == 0 || std::abs(totalAngle) > 360.)
      throw std::invalid_argument("PLANE_INVALID_ARRAY_ANGLE");
    const double step = totalAngle / (std::abs(totalAngle) == 360. ? instances : instances - 1);
    for (int index = 1; index < instances; ++index) {
      CheckCancellation(cancelled);
      result.emplace_back(TransformGeometry(geometry, {{"operation", Text("rotate")}, {"angleDeg", index * step}, {"cx", cx}, {"cy", cy}}));
    }
  } else throw std::invalid_argument("PLANE_DRAW_EDIT_UNSUPPORTED");
  return result;
}
Map PlaceGeometry(Map geometry, const N::Placement &pose) {
  const double a = pose.AngleDeg * pi / 180;
  Array paths;
  for (const auto &v : geometry.at("paths").To<Array>()) {
    auto path = v.To<Map>();
    auto ring = ToRing(path.at("points").To<Array>());
    for (auto &p : ring) {
      const double x = p.X * std::cos(a) - p.Y * std::sin(a),
                   y = p.X * std::sin(a) + p.Y * std::cos(a);
      p = {x + pose.X, (pose.Flip ? -y : y) + pose.Y};
    }
    path["points"] = Points(ring);
    paths.emplace_back(std::move(path));
  }
  geometry["paths"] = paths;
  return geometry;
}
} // namespace iCAX::GeometryServices::PlaneGeometryDetail
