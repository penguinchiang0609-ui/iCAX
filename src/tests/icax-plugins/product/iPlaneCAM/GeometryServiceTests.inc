void CheckPlaneGeometryService(iCAX::Project::CProjectScene &scene) {
  const auto service = scene.Services().Resolve<iCAX::GeometryServices::IPlaneGeometryService>();
  const auto nearlyEqual = [](double a, double b) { return std::abs(a - b) < 1e-9; };
  const auto onlyCurve = [](const Map &geometry) {
    const auto curves = geometry.at("curves").To<Array>();
    Check(curves.size() == 1, "service test requires one exact curve");return curves.front().To<Map>();
  };
  const auto pointIs = [&](const iCAX::Data::Variant &value, double x, double y) {
    const auto point = value.To<Array>();return point.size() == 2 && nearlyEqual(point[0].To<double>(), x) && nearlyEqual(point[1].To<double>(), y);
  };
  const auto isNeutral = [](const Map &geometry) {
    for (const auto *key : {"id", "name", "ownerId", "expectedRevision", "expectedGeometryRef", "async"})
      if (geometry.contains(key)) return false;
    for (const auto &value : geometry.at("curves").To<Array>()) {
      const auto curve = value.To<Map>();
      for (const auto *key : {"id", "name", "ownerId", "expectedRevision", "expectedGeometryRef", "async"})
        if (curve.contains(key)) return false;
    }
    return true;
  };
  const auto throwsCode = [](auto action, const Text &code) {
    try { action(); } catch (const std::exception &error) {
      const auto matched = Text(error.what()).find(code) != Text::npos;
      if (!matched) std::cerr << "[geometry rejection] expected " << code << ", actual " << error.what() << "\n" << std::flush;
      return matched;
    }
    std::cerr << "[geometry rejection] expected " << code << ", actual no exception\n" << std::flush;
    return false;
  };
  const auto line = service->CreateGeometry({{"primitive", Text("line")}, {"points", Array{Array{10., 20.}, Array{14., 20.}}},
      {"id", Text("business-id")}, {"name", Text("business-name")}, {"expectedRevision", Text("7")}, {"async", true}});
  Check(isNeutral(line), "neutral geometry must not copy business request fields into its result");
  const auto rotated = service->TransformGeometry(line, {{"operation", Text("rotate")}, {"angleDeg", 90.}, {"cx", 10.}, {"cy", 20.}, {"name", Text("not geometry")}});
  auto points = onlyCurve(rotated).at("points").To<Array>();
  Check(pointIs(points[0], 10., 20.) && pointIs(points[1], 10., 24.) && isNeutral(rotated),
        "neutral rotation must respect its explicit pivot and keep metadata out");
  const auto arc = service->CreateGeometry({{"primitive", Text("arc")}, {"center", Array{5., 9.}}, {"radius", 4.}, {"startAngleDeg", 20.}, {"endAngleDeg", 110.}});
  const auto mirroredArc = onlyCurve(service->TransformGeometry(arc, {{"operation", Text("mirror")}, {"axis", Text("x")}, {"cy", 7.}}));
  Check(pointIs(mirroredArc.at("center"), 5., 5.) && nearlyEqual(mirroredArc.at("radius").To<double>(), 4.) &&
            nearlyEqual(mirroredArc.at("startAngleDeg").To<double>(), -110.) && nearlyEqual(mirroredArc.at("endAngleDeg").To<double>(), -20.),
        "reflection must reverse arc traversal while preserving its material locus and radius");
  const auto bulged = service->CreateGeometry({{"primitive", Text("polyline")}, {"closed", false},
      {"points", Array{Array{0., 0.}, Array{10., 0.}}}, {"bulges", Array{1., 0.}}});
  const auto mirroredBulge = onlyCurve(service->TransformGeometry(bulged, {{"operation", Text("mirror")}, {"axis", Text("y")}, {"cx", 3.}}));
  points = mirroredBulge.at("points").To<Array>();
  Check(pointIs(points[0], 6., 0.) && pointIs(points[1], -4., 0.) && mirroredBulge.at("bulges").To<Array>()[0].To<double>() == -1.,
        "reflection must transform a bulged curve and invert its signed bulge");
  const auto rectangular = service->ArrayGeometry(line, {{"operation", Text("array-rectangular")}, {"rows", 2}, {"columns", 3}, {"spacingX", 10.}, {"spacingY", 20.}, {"id", Text("not geometry")}});
  Check(rectangular.size() == 6 && rectangular.front().To<Map>() == line &&
            pointIs(onlyCurve(rectangular.back().To<Map>()).at("points").To<Array>()[0], 30., 40.),
        "rectangular arrays must include identity once and apply row/column spacing");
  const auto polar = service->ArrayGeometry(line, {{"operation", Text("array-polar")}, {"count", 4}, {"cx", 10.}, {"cy", 20.}});
  Check(polar.size() == 4 && polar.front().To<Map>() == line &&
            pointIs(onlyCurve(polar[1].To<Map>()).at("points").To<Array>()[1], 10., 24.) &&
            pointIs(onlyCurve(polar[2].To<Map>()).at("points").To<Array>()[1], 6., 20.) &&
            pointIs(onlyCurve(polar[3].To<Map>()).at("points").To<Array>()[1], 10., 16.),
        "full polar arrays must not duplicate the final 360-degree item");
  for (const auto &geometry : rectangular) Check(isNeutral(geometry.To<Map>()), "array output must remain neutral");
  const auto edited = service->TransformGeometry(line, {{"operation", Text("nodes")}, {"points", Array{Array{11., 21.}, Array{15., 21.}}}});
  Check(pointIs(onlyCurve(edited).at("points").To<Array>()[0], 11., 21.) && line != edited && isNeutral(edited),
        "node edits must return a new exact geometry candidate");
  Check(throwsCode([&] { service->TransformGeometry(line, {{"operation", Text("nodes")}, {"points", Array{iCAX::Data::Variant(Array{1., 1.})}}}); }, "PLANE_NODE_EDIT_POINT_COUNT_CHANGED"),
        "invalid neutral node edits must reject a changed point count");
  Check(throwsCode([&] { service->ArrayGeometry(line, {{"operation", Text("array-rectangular")}, {"rows", 501}, {"columns", 1}}); }, "PLANE_INVALID_ARRAY_COUNT"),
        "invalid neutral arrays must reject excess item count rather than truncate");
  Check(throwsCode([&] { service->ArrayGeometry(line, {{"operation", Text("array-polar")}, {"count", 4}}, [] { return true; }); }, "PLANE_GEOMETRY_CANCELLED") &&
            throwsCode([&] { service->BuildGeometry(line.at("curves").To<Array>(), false, [] { return true; }); }, "PLANE_GEOMETRY_CANCELLED"),
        "neutral array and geometry validation must honor cancellation");
  const Map materialOuter{{"type", Text("polyline")}, {"closed", true},
      {"points", Array{Array{0., 0.}, Array{100., 0.}, Array{100., 80.}, Array{0., 80.}}}};
  const Array materialCurves{materialOuter, Map{{"type", Text("circle")}, {"center", Array{25., 25.}}, {"radius", 10.}}};
  const auto material = service->BuildGeometry(materialCurves, true);
  for (const Map &request : std::vector<Map>{{{"operation", Text("move")}, {"dx", 12.}, {"dy", -6.}},
           {{"operation", Text("rotate")}, {"angleDeg", 37.}, {"cx", 7.}, {"cy", 3.}},
           {{"operation", Text("mirror")}, {"axis", Text("x")}},
           {{"operation", Text("scale")}, {"factor", 1.25}},
           {{"operation", Text("copy")}, {"dx", 250.}}}) {
    const auto changed = service->TransformGeometry(material, request);
    Check(changed.at("holeCount").To<UInt>() == 1 && changed.at("holes").To<Array>().size() == 1 &&
              service->BuildGeometry(changed.at("curves").To<Array>(), true).at("holeCount").To<UInt>() == 1,
          "neutral material transforms must retain consistent exact-curve topology, outer/holes and hole count");
  }
  const auto freeCurves = service->TransformGeometry(service->BuildGeometry(materialCurves, false), {{"operation", Text("move")}, {"dx", 1.}});
  Check(!freeCurves.contains("holes") && !freeCurves.contains("holeCount"), "free drawing curves must not silently acquire material-hole semantics from nesting");
  const auto singleMaterial = service->BuildGeometry(Array{materialOuter}, true);
  Check(throwsCode([&] { service->TransformGeometry(singleMaterial, {{"operation", Text("nodes")},
      {"points", Array{Array{0., 0.}, Array{100., 80.}, Array{100., 0.}, Array{0., 80.}}}}); }, "PLANE_MATERIAL_INTERSECTING_CURVES"),
      "topology-changing material node edits must still pass actual material validation rather than inherit a stale hole count");
  std::cerr << "[phase] injected geometry service neutrality/pivot/mirror/array/nodes/cancellation passed\n" << std::flush;
}
