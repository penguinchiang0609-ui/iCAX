// One authoritative curve and EC, with analytic arcs and exact polynomial /
// rational spline data. Display paths are deliberately never the saved oracle.
Array MixedPolylineSegments(bool closed = false) {
  Array result{
      Map{{"type", Text("line")}, {"points", Array{Array{0., 0.}, Array{20., 0.}}}},
      Map{{"type", Text("arc")}, {"center", Array{20., 10.}}, {"radius", 10.}, {"startAngleDeg", -90.}, {"sweepAngleDeg", 90.}},
      Map{{"type", Text("ellipseArc")}, {"center", Array{40., 10.}}, {"radiusX", 10.}, {"radiusY", 5.}, {"rotationDeg", 0.}, {"startAngleDeg", 180.}, {"sweepAngleDeg", -90.}},
      Map{{"type", Text("bezier")}, {"controlPoints", Array{Array{40., 15.}, Array{45., 15.}, Array{45., 20.}, Array{50., 20.}}}},
      Map{{"type", Text("bspline")}, {"degree", 3},
          {"controlPoints", Array{Array{50., 20.}, Array{53., 20.}, Array{54., 25.}, Array{57., 20.}, Array{58., 25.}, Array{60., 25.}}},
          {"knots", Array{0., 0., 0., 0., 1., 2., 3., 3., 3., 3.}}, {"weights", Array{1., 1., 1.5, 1., 1., 1.}}}};
  if (closed) {
    result.emplace_back(Map{{"type", Text("line")}, {"points", Array{Array{60., 25.}, Array{60., 40.}}}});
    result.emplace_back(Map{{"type", Text("line")}, {"points", Array{Array{60., 40.}, Array{0., 40.}}}});
    result.emplace_back(Map{{"type", Text("line")}, {"points", Array{Array{0., 40.}, Array{0., 0.}}}});
  }
  return result;
}

void CheckMixedPolylines(
    const std::shared_ptr<iCAX::Product::CProductRuntime> &runtime,
    const std::filesystem::path &directory,
    const std::filesystem::path &fixtures) {
  auto catalog = runtime->OpenProjectCatalog("Mixed polylines", {}, "Mixed polylines", {});
  auto project = catalog->GetMainProject();
  auto endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  const auto previousDiagnosticScene = diagnosticScene;diagnosticScene = &project->GetMainScene();
  const auto service = project->GetMainScene().Services().Resolve<iCAX::GeometryServices::IPlaneGeometryService>();
  const auto mixedNearlyEqual = [](double a, double b) { return std::abs(a - b) < 1e-7; };
  const auto pointIs = [&](const iCAX::Data::Variant &value, double x, double y) {
    const auto p = value.To<Array>();return p.size() == 2 && mixedNearlyEqual(p[0].To<double>(), x) && mixedNearlyEqual(p[1].To<double>(), y);
  };
  const auto only = [](const Map &geometry) {
    const auto curves = geometry.at("curves").To<Array>();
    Check(curves.size() == 1, "a mixed polyline must remain one exact curve");
    const auto curve = curves.front().To<Map>();
    Check(curve.at("type").To<Text>() == "polycurve" && !curve.contains("points"),
          "mixed geometry must retain a polycurve instead of authoritative tessellated points");
    return curve;
  };
  const auto checkTypes = [&](const Map &curve, std::size_t count = 5) {
    const auto segments = curve.at("segments").To<Array>();
    Check(segments.size() == count, "mixed exact segment count changed");
    const std::vector<Text> types{"line", "arc", "ellipseArc", "bezier", "bspline"};
    for (std::size_t index = 0; index < types.size(); ++index)
      Check(segments[index].To<Map>().at("type").To<Text>() == types[index], "mixed exact segment type/order changed");
    const auto spline = segments[4].To<Map>();
    Check(spline.at("controlPoints").To<Array>().size() == 6 && spline.at("degree").To<int>() == 3 &&
              spline.at("knots").To<Array>() == MixedPolylineSegments()[4].To<Map>().at("knots").To<Array>() &&
              spline.at("weights").To<Array>() == MixedPolylineSegments()[4].To<Map>().at("weights").To<Array>(),
          "six-pole rational B-spline knots/degree/weights must remain exact");
  };
  const auto row = [&](const Text &collection, const Text &id) {
    for (const auto &value : Model(endpoint).at(collection).To<Array>()) {
      const auto item = value.To<Map>();if (item.at("id").To<Text>() == id) return item;
    }
    throw std::runtime_error("mixed-polyline acceptance row missing: " + collection + "/" + id);
  };
  const auto query = [&](const Text &id, bool sketch = false) {
    return Call(endpoint, sketch ? "PlaneSketch" : "PlaneDrawing", "ReadGeometry", {{"id", id}});
  };
  struct Snapshot {
    std::map<Text, std::map<Text, std::pair<Map, std::uint64_t>>> Components;
    std::vector<std::tuple<iCAX::Data::uuid, Text>> Undo, Redo;
    bool operator==(const Snapshot &) const = default;
  };
  const auto snapshot = [&]() {
    auto &scene = project->GetMainScene();scene.Stop();Snapshot result;
    try {
      auto &repository = scene.Database();
      for (const auto &id : repository.GetEntityIDs()) {
        const auto entity = repository.GetEntity(id);
        for (const auto &type : entity->GetComponentClasses())
          result.Components[iCAX::Data::to_string(id)][type] = {entity->GetComponent(type)->GetProperties(), repository.GetComponentVersion(id, type)};
      }
      result.Undo = repository.GetUndoArray();result.Redo = repository.GetRedoArray();
    } catch (...) { scene.Start();throw; }
    scene.Start();return result;
  };
  const auto throwsCode = [](auto action, const Text &prefix) {
    try { action(); } catch (const std::exception &error) {
      const bool matched = Text(error.what()).find(prefix) != Text::npos;
      if (!matched) std::cerr << "[mixed rejection] expected " << prefix << ", actual " << error.what() << '\n' << std::flush;
      return matched;
    }
    std::cerr << "[mixed rejection] expected " << prefix << ", actual no exception\n" << std::flush;
    return false;
  };
  const auto reject = [&](const Text &group, const Text &method, const Map &request, const Text &prefix) {
    const auto before = Call(endpoint, "PlaneScene", "ReadState");const auto components = snapshot();
    const auto resources = project->GetMainScene().Resources().GetManifest().size();
    Check(throwsCode([&] { Call(endpoint, group, method, request); }, prefix), "mixed invalid command did not reject with its geometry reason");
    Check(Call(endpoint, "PlaneScene", "ReadState") == before && snapshot() == components &&
              project->GetMainScene().Resources().GetManifest().size() == resources,
          "mixed invalid command must preserve every EC property/version, resource and undo/redo entry");
  };
  const auto waitTask = [&](const Text &id) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
      const auto state = Call(endpoint, "PlaneTask", "Read", {{"taskId", id}});
      if (state.at("status").To<Text>() != "Running") return state;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("mixed-polyline native task timed out");
  };
  const auto request = [&](bool closed = false) {
    return Map{{"primitive", Text("polyline")}, {"segments", MixedPolylineSegments(closed)}, {"closed", closed}};
  };
  const auto edit = [&](const Text &id, Map values) {
    values["id"] = id;values["expectedGeometryRef"] = query(id).at("geometryRef");
    return Call(endpoint, "PlaneDrawing", "Edit", values);
  };

  const auto beforeService = snapshot();const auto resourcesBeforeService = project->GetMainScene().Resources().GetManifest().size();
  auto serviceRequest = request();serviceRequest["id"] = Text("business-id");serviceRequest["ownerId"] = Text("business-owner");serviceRequest["async"] = true;
  auto withMetadata = serviceRequest.at("segments").To<Array>();auto first = withMetadata[0].To<Map>();first["name"] = Text("business-name");withMetadata[0] = first;serviceRequest["segments"] = withMetadata;
  const auto neutral = service->CreateGeometry(serviceRequest), sourceCurve = only(neutral);
  checkTypes(sourceCurve);
  const auto previousPrecision = std::cerr.precision(17);
  std::cerr << "[mixed bounds] width=" << neutral.at("width").To<double>()
            << " height=" << neutral.at("height").To<double>()
            << " approximationBoundMm=" << neutral.at("approximationBoundMm").To<double>() << '\n' << std::flush;
  std::cerr.precision(previousPrecision);
  Check(mixedNearlyEqual(neutral.at("width").To<double>(), 60.) && mixedNearlyEqual(neutral.at("height").To<double>(), 25.) &&
            neutral.at("approximationBoundMm").To<double>() > 0. && neutral.at("approximationBoundMm").To<double>() <= .050001,
        "mixed exact bounds and conservative curve approximation error are required");
  Check(!neutral.contains("id") && !neutral.contains("ownerId") && !neutral.contains("async") &&
            !sourceCurve.at("segments").To<Array>()[0].To<Map>().contains("name"),
        "neutral mixed service must strip business metadata recursively");
  auto transformed = only(service->TransformGeometry(neutral, {{"operation", Text("rotate")}, {"angleDeg", 90.}, {"cx", 10.}, {"cy", 20.}}));
  checkTypes(transformed);
  Check(pointIs(transformed.at("segments").To<Array>()[0].To<Map>().at("points").To<Array>()[0], 30., 10.) &&
            pointIs(transformed.at("segments").To<Array>()[1].To<Map>().at("center"), 20., 30.),
        "mixed rotation must transform exact controls and centers about the requested pivot");
  transformed = only(service->TransformGeometry(neutral, {{"operation", Text("mirror")}, {"axis", Text("x")}, {"cy", 7.}}));
  checkTypes(transformed);
  const auto mirroredSegments = transformed.at("segments").To<Array>();
  Check(pointIs(mirroredSegments[1].To<Map>().at("center"), 20., 4.) && mixedNearlyEqual(mirroredSegments[1].To<Map>().at("sweepAngleDeg").To<double>(), -90.) &&
            mixedNearlyEqual(mirroredSegments[2].To<Map>().at("sweepAngleDeg").To<double>(), 90.) &&
            pointIs(mirroredSegments[4].To<Map>().at("controlPoints").To<Array>()[2], 54., -11.),
        "reflection must preserve chain order, reflect controls and reverse signed curve sweeps");
  transformed = only(service->TransformGeometry(neutral, {{"operation", Text("scale")}, {"factor", 2.}, {"cx", 0.}, {"cy", 0.}}));
  checkTypes(transformed);
  Check(mixedNearlyEqual(transformed.at("segments").To<Array>()[1].To<Map>().at("radius").To<double>(), 20.) &&
            pointIs(transformed.at("segments").To<Array>()[4].To<Map>().at("controlPoints").To<Array>().back(), 120., 50.),
        "scaling must preserve exact mixed topology instead of replacing segments with chords");
  const auto neutralArray = service->ArrayGeometry(neutral, {{"operation", Text("array-rectangular")}, {"rows", 2}, {"columns", 2}, {"spacingX", 80.}, {"spacingY", 50.}});
  Check(neutralArray.size() == 4 && neutralArray.front().To<Map>() == neutral, "neutral mixed arrays must include the unchanged identity once");
  for (const auto &value : neutralArray) checkTypes(only(value.To<Map>()));
  Check(throwsCode([&] { service->BuildGeometry(neutral.at("curves").To<Array>(), false, [] { return true; }); }, "PLANE_GEOMETRY_CANCELLED") &&
            throwsCode([&] { service->ArrayGeometry(neutral, {{"operation", Text("array-polar")}, {"count", 4}}, [] { return true; }); }, "PLANE_GEOMETRY_CANCELLED"),
        "mixed geometry algorithms must honor cancellation before publishing candidates");
  const auto materialCurve = only(service->CreateGeometry(request(true)));
  const auto material = service->BuildGeometry(Array{materialCurve, Map{{"type", Text("circle")}, {"center", Array{10., 30.}}, {"radius", 2.}}}, true);
  Check(material.at("holeCount").To<UInt>() == 1 && material.at("holes").To<Array>().size() == 1 &&
            material.at("curves").To<Array>()[0].To<Map>() == materialCurve,
        "a valid closed mixed material with a hole must retain exact source geometry");
  Check(throwsCode([&] { service->BuildGeometry(neutral.at("curves").To<Array>(), true); }, "PLANE_MATERIAL_"),
        "an open mixed chain must never be accepted as material");
  for (const auto &type : {Text("bezier"), Text("bspline")}) {
    Map loopSegment{{"type", type}, {"controlPoints", Array{Array{0., 0.}, Array{20., 0.}, Array{0., 20.}, Array{0., 0.}}}};
    if (type == "bspline") loopSegment["degree"] = 3;
    const auto loop = service->BuildGeometry(Array{Map{{"type", Text("polycurve")}, {"segments", Array{loopSegment}}, {"closed", true}}}, true);
    Check(loop.at("holeCount").To<UInt>() == 0 && loop.at("outer").To<Array>().size() >= 3 &&
              loop.at("curves").To<Array>()[0].To<Map>().at("segments").To<Array>().size() == 1,
          "a simple one-segment closed Bezier/B-spline must remain valid material without a spurious self-intersection");
  }
  const Map tinyDomainCrossing{{"type", Text("bspline")}, {"degree", 1},
      {"controlPoints", Array{Array{0., 0.}, Array{3., 2.}, Array{0., 3.}, Array{2., 0.}, Array{0., 0.}}},
      {"knots", Array{0., 0., 2e-12, 4e-12, 6e-12, 8e-12, 8e-12}}};
  Check(throwsCode([&] { service->BuildGeometry(Array{Map{{"type", Text("polycurve")}, {"segments", Array{tinyDomainCrossing}}, {"closed", true}}}, true); }, "PLANE_MATERIAL_"),
        "a legal tiny spline parameter domain must not let endpoint tolerance hide an internal closed-curve crossing");
  Check(snapshot() == beforeService && project->GetMainScene().Resources().GetManifest().size() == resourcesBeforeService,
        "neutral mixed computations and failures must not mutate EC/resources/history");

  const auto countBefore = Model(endpoint).at("drawings").To<Array>().size();
  const auto drawing = Call(endpoint, "PlaneDrawing", "Create", request()).at("id").To<Text>();
  const auto original = query(drawing);checkTypes(only(original));
  Check(Model(endpoint).at("drawings").To<Array>().size() == countBefore + 1 &&
            row("drawings", drawing).at("isPolyline").To<bool>() && row("drawings", drawing).at("canClosePolyline").To<bool>(),
        "five mixed segments must create one drawing EC with native close capability");
  const auto layer = Call(endpoint, "PlaneLayer", "Create", {{"name", Text("Mixed exact curves")}}).at("id").To<Text>();
  Call(endpoint, "PlaneLayer", "Assign", {{"ids", Array{drawing}}, {"layerId", layer}});
  edit(drawing, {{"operation", Text("move")}, {"dx", 10.}, {"dy", -5.}});
  const auto moved = query(drawing);checkTypes(only(moved));
  Check(row("drawings", drawing).at("layerId").To<Text>() == layer &&
            pointIs(only(moved).at("segments").To<Array>()[4].To<Map>().at("controlPoints").To<Array>().back(), 70., 20.),
        "native edit must preserve typed layer binding and transform exact spline poles");
  reject("PlaneDrawing", "Edit", {{"id", drawing}, {"operation", Text("move")}, {"dx", 1.}, {"expectedGeometryRef", original.at("geometryRef")}}, "PLANE_STALE_GEOMETRY");
  Call(endpoint, "Project", "Undo");Check(query(drawing) == original, "mixed edit undo must restore exact payload/ref/version and stable identity");
  Call(endpoint, "Project", "Redo");Check(query(drawing) == moved, "mixed edit redo must restore exact payload/ref/version and stable identity");
  const auto arrayTask = edit(drawing, {{"operation", Text("array-rectangular")}, {"rows", 2}, {"columns", 2}, {"spacingX", 80.}, {"spacingY", 50.}, {"async", true}}).at("taskId").To<Text>();
  const auto arrayResult = waitTask(arrayTask);
  Check(arrayResult.at("status").To<Text>() == "Completed", "mixed array must use the real framework async task");
  Check(Model(endpoint).at("drawings").To<Array>().size() == countBefore + 4 && query(drawing) == moved,
        "async mixed array must retain its source and publish the remaining three ECs atomically");
  for (const auto &value : Model(endpoint).at("drawings").To<Array>()) {
    const auto item = value.To<Map>();checkTypes(only(query(item.at("id").To<Text>())));
    Check(item.at("layerId").To<Text>() == layer, "array copies must retain the typed source layer");
  }
  edit(drawing, {{"operation", Text("close")}});
  const auto closed = query(drawing);const auto closedCurve = only(closed);checkTypes(closedCurve, 6);
  Check(closedCurve.at("closed").To<bool>() && row("drawings", drawing).at("closed").To<bool>() && !row("drawings", drawing).at("canClosePolyline").To<bool>() &&
            closedCurve.at("segments").To<Array>().back().To<Map>().at("type").To<Text>() == "line" &&
            pointIs(closedCurve.at("segments").To<Array>().back().To<Map>().at("points").To<Array>().back(), 10., -5.),
        "closing must preserve all existing exact segments and append only the needed last-to-first line");
  Call(endpoint, "Project", "Undo");Check(query(drawing) == moved, "mixed close undo must restore the exact open chain");
  Call(endpoint, "Project", "Redo");Check(query(drawing) == closed, "mixed close redo must restore the exact closed chain");

  auto bad = request();auto badSegments = MixedPolylineSegments();auto segment = badSegments[1].To<Map>();segment["center"] = Array{25., 10.};badSegments[1] = segment;bad["segments"] = badSegments;
  reject("PlaneDrawing", "Create", bad, "PLANE_");
  for (const auto &[key, value] : std::vector<std::pair<Text, iCAX::Data::Variant>>{
           {"degree", 6}, {"knots", Array{0., 0., 0., 1.}}, {"weights", Array{1., 1., -1., 1., 1., 1.}}}) {
    bad = request();badSegments = MixedPolylineSegments();segment = badSegments[4].To<Map>();segment[key] = value;badSegments[4] = segment;bad["segments"] = badSegments;
    reject("PlaneDrawing", "Create", bad, "PLANE_");
  }
  bad = request();badSegments = MixedPolylineSegments();segment = badSegments[0].To<Map>();segment["type"] = Text("unsupported-curve");badSegments[0] = segment;bad["segments"] = badSegments;
  reject("PlaneDrawing", "Create", bad, "PLANE_");
  const Array crossing{
      Map{{"type", Text("line")}, {"points", Array{Array{0., 0.}, Array{20., 20.}}}},
      Map{{"type", Text("line")}, {"points", Array{Array{20., 20.}, Array{0., 20.}}}},
      Map{{"type", Text("line")}, {"points", Array{Array{0., 20.}, Array{20., 0.}}}},
      Map{{"type", Text("line")}, {"points", Array{Array{20., 0.}, Array{0., 0.}}}}};
  Check(throwsCode([&] { service->BuildGeometry(Array{Map{{"type", Text("polycurve")}, {"segments", crossing}, {"closed", true}}}, true); }, "PLANE_MATERIAL_"),
        "self-crossing exact chains must fail material validation");
  const Array nearEndpointCrossing{
      Map{{"type", Text("line")}, {"points", Array{Array{0., 0.}, Array{1., 0.}}}},
      Map{{"type", Text("bezier")}, {"controlPoints", Array{Array{1., 0.}, Array{.9996, -.0003}, Array{.9998, .0003}, Array{1., 1.}}}},
      Map{{"type", Text("line")}, {"points", Array{Array{1., 1.}, Array{0., 1.}}}},
      Map{{"type", Text("line")}, {"points", Array{Array{0., 1.}, Array{0., 0.}}}}};
  Check(throwsCode([&] { service->BuildGeometry(Array{Map{{"type", Text("polycurve")}, {"segments", nearEndpointCrossing}, {"closed", true}}}, true); }, "PLANE_MATERIAL_INTERSECTING_CURVES"),
        "a second interior crossing 3.2e-5mm from the shared endpoint must not be swallowed by a loose endpoint tolerance");
  auto nearEndpointSpline = nearEndpointCrossing;auto splineSegment = nearEndpointSpline[1].To<Map>();
  splineSegment["type"] = Text("bspline");splineSegment["degree"] = 3;nearEndpointSpline[1] = splineSegment;
  Check(throwsCode([&] { service->BuildGeometry(Array{Map{{"type", Text("polycurve")}, {"segments", nearEndpointSpline}, {"closed", true}}}, true); }, "PLANE_MATERIAL_INTERSECTING_CURVES"),
        "the same near-endpoint interior crossing must be detected after B-spline-to-Bezier span conversion");
  auto sameSideCurve = nearEndpointCrossing;auto sameSideSegment = sameSideCurve[1].To<Map>();
  sameSideSegment["controlPoints"] = Array{Array{1., 0.}, Array{.9996, .0003}, Array{.9998, .0003}, Array{1., 1.}};sameSideCurve[1] = sameSideSegment;
  const auto sameSideMaterial = service->BuildGeometry(Array{Map{{"type", Text("polycurve")}, {"segments", sameSideCurve}, {"closed", true}}}, true);
  Check(sameSideMaterial.at("holeCount").To<UInt>() == 0 && sameSideMaterial.at("outer").To<Array>().size() >= 3 &&
            sameSideMaterial.at("curves").To<Array>()[0].To<Map>().at("segments").To<Array>()[1].To<Map>().at("controlPoints") == sameSideSegment.at("controlPoints"),
        "a near-line Bezier wholly on one side must remain valid instead of being rejected merely for nearby control points");
  const auto partPath = directory / "mixed-sketch-part.dxf";
  std::filesystem::copy_file(fixtures / "part_with_hole.dxf", partPath);
  const auto part = Call(endpoint, "PlanePart", "Import", {{"sourcePath", UTF8(partPath)}}).at("id").To<Text>();
  const auto sketch = Call(endpoint, "PlaneSketch", "Open", {{"ownerId", part}}).at("sketchId").To<Text>();
  const auto ownerBefore = row("parts", part);const auto idsBefore = row("sketches", sketch).at("curveIds").To<Array>();
  std::map<Text, Map> oldCurves;
  for (const auto &value : idsBefore) { const auto id = value.To<Text>();oldCurves[id] = query(id, true);Call(endpoint, "PlaneSketch", "Remove", {{"id", id}}); }
  auto sketchRequest = request();sketchRequest["sketchId"] = sketch;
  const auto draft = Call(endpoint, "PlaneSketch", "Create", sketchRequest).at("id").To<Text>();
  reject("PlaneSketch", "Complete", {{"id", sketch}}, "PLANE_MATERIAL_");
  Check(row("parts", part) == ownerBefore, "open mixed sketch completion must leave its owner unchanged");
  Call(endpoint, "PlaneSketch", "Remove", {{"id", draft}});
  Call(endpoint, "PlaneSketch", "Create", {{"sketchId", sketch}, {"primitive", Text("polyline")}, {"segments", nearEndpointCrossing}, {"closed", true}});
  reject("PlaneSketch", "Complete", {{"id", sketch}}, "PLANE_MATERIAL_INTERSECTING_CURVES");
  Check(row("parts", part) == ownerBefore, "near-endpoint mixed curve crossing must preserve its owner and all ECS/history atomically");
  Call(endpoint, "PlaneSketch", "Cancel", {{"id", sketch}});
  Check(row("parts", part) == ownerBefore && row("sketches", sketch).at("curveIds").To<Array>() == idsBefore,
        "mixed sketch cancellation must restore the original owner and curve IDs");
  for (const auto &[id, geometry] : oldCurves) Check(query(id, true) == geometry, "mixed sketch cancel changed an exact original curve");
  Call(endpoint, "PlaneSketch", "Open", {{"ownerId", part}});
  for (const auto &value : idsBefore) Call(endpoint, "PlaneSketch", "Remove", {{"id", value.To<Text>()}});
  sketchRequest = request(true);sketchRequest["sketchId"] = sketch;
  const auto mixedOuter = Call(endpoint, "PlaneSketch", "Create", sketchRequest).at("id").To<Text>();
  const auto hole = Call(endpoint, "PlaneSketch", "Create", {{"sketchId", sketch}, {"primitive", Text("circle")}, {"center", Array{10., 30.}}, {"radius", 2.}}).at("id").To<Text>();
  Check(row("parts", part) == ownerBefore && row("sketches", sketch).at("curveIds").To<Array>().size() == 2,
        "closed mixed drafting must stay in one separate EC without mutating its material owner");
  const auto outerExact = query(mixedOuter, true), holeExact = query(hole, true);
  const auto completeTask = Call(endpoint, "PlaneSketch", "Complete", {{"id", sketch}, {"async", true}}).at("taskId").To<Text>();
  Check(waitTask(completeTask).at("status").To<Text>() == "Completed", "mixed material completion must execute through a real async task");
  const auto committed = row("parts", part);
  Check(committed.at("id").To<Text>() == part && committed.at("holeCount").To<UInt>() == 1 && mixedNearlyEqual(committed.at("width").To<double>(), 60.) && mixedNearlyEqual(committed.at("height").To<double>(), 40.) &&
            std::stoull(committed.at("geometryVersion").To<Text>()) == std::stoull(ownerBefore.at("geometryVersion").To<Text>()) + 1,
        "mixed material completion must commit one version to the same hole-bearing owner");
  Call(endpoint, "Project", "Undo");Check(row("parts", part) == ownerBefore, "mixed owner completion undo must restore the same original owner");
  Call(endpoint, "Project", "Redo");Check(row("parts", part) == committed && query(mixedOuter, true) == outerExact, "mixed owner completion redo must restore exact curves and owner atomically");

  Call(endpoint, "PlaneStock", "CreateRectangle", {{"width", 100.}, {"height", 80.}});
  Call(endpoint, "PlaneNesting", "SetParameters", {{"partGap", 2.}, {"stockMargin", 3.}, {"timeLimitSeconds", .1}});
  const auto nestTask = Call(endpoint, "PlaneNesting", "Run").at("taskId").To<Text>();
  Map nestState;
  for (int attempt = 0; attempt < 2000; ++attempt) {
    nestState = Call(endpoint, "PlaneNesting", "ReadTask", {{"taskId", nestTask}});
    if (nestState.at("status").To<Text>() != "Running") break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  Check(nestState.at("status").To<Text>() == "Completed", "exact mixed owner must execute through the real nesting service/task");
  const auto nestReport = Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", nestState.at("resultId")}});
  const double mixedBound = committed.at("approximationBoundMm").To<double>();
  Check(nestReport.at("valid").To<bool>() && nestReport.at("placed").To<UInt>() == 1 && nestReport.at("required").To<UInt>() == 1 &&
            mixedBound > 0. && mixedBound <= .050001 &&
            nestReport.at("calculationPartGap").To<double>() >= 2. + 2. * mixedBound &&
            nestReport.at("calculationStockMargin").To<double>() >= 3. + mixedBound,
        "real mixed-curve nesting must place the hole-bearing demand once and include curve discretization bounds in its calculation gap/margin");

  const auto savedState = Model(endpoint);const auto path = directory / "mixed-polylines.ipcam";
  runtime->SaveProjectFile(project->GetProjectID(), UTF8(path));
  Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()), "mixed-polyline catalog close failed");
  std::filesystem::remove(partPath);
  auto reopened = runtime->OpenProjectFile(UTF8(path));project = reopened->GetMainProject();diagnosticScene = &project->GetMainScene();
  endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  Check(query(drawing) == closed && query(mixedOuter, true) == outerExact && query(hole, true) == holeExact &&
            row("parts", part) == committed && Model(endpoint).at("drawings") == savedState.at("drawings") &&
            Model(endpoint).at("sketches") == savedState.at("sketches") && Model(endpoint).at("layers") == savedState.at("layers"),
        "save/reopen must preserve exact mixed curves, poles/knots/weights, IDs, layer links, closure and owners without source files");
  checkTypes(only(query(drawing)), 6);checkTypes(only(query(mixedOuter, true)), 8);
  Check(runtime->CloseProjectCatalog(reopened->GetCatalogID()), "reopened mixed-polyline catalog close failed");
  diagnosticScene = previousDiagnosticScene;
  std::cerr << "[phase] mixed exact polycurve/service/transform/material/EC/layer/async/history/atomicity/persistence passed\n" << std::flush;
}
