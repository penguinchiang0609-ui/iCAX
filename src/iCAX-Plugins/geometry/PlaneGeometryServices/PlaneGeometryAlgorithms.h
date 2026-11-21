#pragma once
#include "GeometryServices/PlaneGeometryService.h"

namespace iCAX::GeometryServices::PlaneGeometryDetail {
using namespace PlaneGeometryData;
Map CreateGeometry(const Map &);
Map BuildGeometry(const Array &, bool material, const std::function<bool()> &cancelled = {});
// Closed-loop topology only; unlike BuildGeometry(material=true), this allows
// multiple independent outer loops and does not classify material islands.
void ValidateClosedCurveIntersections(const Array &, const std::function<bool()> &cancelled = {});
Map ImportGeometry(const CAD::SCadExchangeDocument &, bool material, const Text &explicitUnits = "");
Map TransformGeometry(Map, const Map &);
Array ArrayGeometry(const Map &, const Map &, const std::function<bool()> &cancelled = {});
Map DescribeDrawingClosure(const Map &);
Array DescribeDrawingDimensions(const Map &, const Text &entityId);
Array EditDrawingDimensionCurves(const Map &, const Map &);
Map PlaceGeometry(Map, const PlaneNesting::Placement &);
} // namespace iCAX::GeometryServices::PlaneGeometryDetail
