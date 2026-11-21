#pragma once
#include "PlaneGeometryAlgorithms.h"
#include <Geom2d_Curve.hxx>
#include <array>
#include <vector>

namespace iCAX::GeometryServices::PlaneGeometryDetail {
struct MixedCurveEdge {
  Handle(Geom2d_Curve) Curve;
  PlaneNesting::Point A, B;
  bool Closed = false;
  bool CheckSelfIntersection = false;
};
Map NormalizeMixedCurve(const Map&);
Map CloseMixedCurve(const Map&);
Map TransformMixedCurve(const Map&, const Map&);
PlaneNesting::Ring MixedCurvePath(const Map&, Array* snaps = nullptr,
                                const std::function<bool()>& cancelled = {});
std::vector<MixedCurveEdge> MixedCurveEdges(const Map&);
std::array<double, 4> MixedCurveBounds(const Map&);
double MixedCurveApproximationBound(const Map&);
bool CurvePointAtBoundary(const Handle(Geom2d_Curve)&, double parameter,
                          PlaneNesting::Point, bool first);
void ValidateLineSplineMaterialIntersections(const Handle(Geom2d_Curve)&,
                                             const Handle(Geom2d_Curve)&,
                                             const std::function<bool()>& cancelled = {});
} // namespace iCAX::GeometryServices::PlaneGeometryDetail
