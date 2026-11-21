void CheckDrawingDimensions(
    const std::shared_ptr<iCAX::Product::CProductRuntime> &runtime,
    const std::filesystem::path &directory) {
  auto catalog = runtime->OpenProjectCatalog("Drawing dimensions", {},
                                             "Drawing dimensions", {});
  auto project = catalog->GetMainProject();
  auto endpoint = runtime->GetSceneFrontendSDOEndpoint(
      project->GetProjectID(), project->GetMainSceneID());
  const auto previousDiagnosticScene = diagnosticScene;
  diagnosticScene = &project->GetMainScene();
  const auto geometry = [&](const Text &id) {
    return Call(endpoint, "PlaneDrawing", "ReadGeometry", {{"id", id}});
  };
  const auto curve = [&](const Text &id) {
    return geometry(id).at("curves").To<Array>().front().To<Map>();
  };
  const auto row = [&](const Text &id) {
    for (const auto &value : Model(endpoint).at("drawings").To<Array>()) {
      const auto item = value.To<Map>();
      if (item.at("id").To<Text>() == id) return item;
    }
    throw std::runtime_error("dimension drawing row missing");
  };
  const auto dimension = [&](const Text &id, const Text &key) {
    for (const auto &value : geometry(id).at("dimensions").To<Array>()) {
      const auto item = value.To<Map>();
      if (item.at("parameter").To<Text>() == key) return item;
    }
    throw std::runtime_error("dimension descriptor missing");
  };
  const auto create = [&](Map request) {
    return Call(endpoint, "PlaneDrawing", "Create", request).at("id").To<Text>();
  };
  const auto edit = [&](const Text &id, Map values) {
    values["id"] = id; values["operation"] = Text("dimensions");
    values["expectedGeometryRef"] = geometry(id).at("geometryRef");
    return Call(endpoint, "PlaneDrawing", "Edit", values);
  };
  struct RepositorySnapshot {
    std::map<Text, std::uint64_t> Versions;
    std::vector<std::tuple<iCAX::Data::uuid, Text>> Undo, Redo;
    bool operator==(const RepositorySnapshot &) const = default;
  };
  const auto repositorySnapshot = [&](const Text &id) {
    const auto targetID = iCAX::Data::uuid::from_string(id);
    Check(targetID.has_value(), "dimension snapshot target ID invalid");
    auto &scene = project->GetMainScene();
    // All SDO calls have completed in this isolated test scene. Pause its
    // worker for a consistent repository read, preserving the runtime's
    // production frame handler and resuming before the next SDO request.
    Check(scene.IsRunning(), "dimension snapshot scene must be running");
    scene.Stop();
    RepositorySnapshot snapshot;
    try {
      auto &repository = scene.Database();
      const auto target = repository.GetEntity(*targetID);
      Check(static_cast<bool>(target), "dimension snapshot target missing");
      for (const auto &component : target->GetComponentClasses())
        snapshot.Versions[component] = repository.GetComponentVersion(*targetID, component);
      snapshot.Undo = repository.GetUndoArray();
      snapshot.Redo = repository.GetRedoArray();
    } catch (...) {
      scene.Start();
      throw;
    }
    scene.Start();
    return snapshot;
  };
  const auto reject = [&](Map request, const Text &code) {
    const auto before = Call(endpoint, "PlaneScene", "ReadState");
    const auto resources = project->GetMainScene().Resources().GetManifest().size();
    const auto beforeRepository = repositorySnapshot(request.at("id").To<Text>());
    bool rejected = false;
    try { Call(endpoint, "PlaneDrawing", "Edit", request); }
    catch (const std::exception &error) {
      rejected = Text(error.what()).find(code) != Text::npos;
    }
    Check(rejected, "invalid dimension edit must report its declared reason");
    Check(Call(endpoint, "PlaneScene", "ReadState") == before &&
              project->GetMainScene().Resources().GetManifest().size() == resources &&
              repositorySnapshot(request.at("id").To<Text>()) == beforeRepository,
          "dimension rejection must preserve ECS/component versions, resources and undo/redo history");
  };
  const auto invalid = [&](const Text &id, Map values, const Text &code) {
    values["id"] = id; values["operation"] = Text("dimensions");
    values["expectedGeometryRef"] = geometry(id).at("geometryRef");
    reject(values, code);
  };
  const auto nearlyEqual = [](double a, double b) { return std::abs(a - b) < 1e-9; };
  const auto pointIs = [&](const iCAX::Data::Variant &value, double x, double y) {
    const auto p = value.To<Array>();
    return p.size() >= 2 && nearlyEqual(p[0].To<double>(), x) && nearlyEqual(p[1].To<double>(), y);
  };
  const auto circle = create({{"primitive", Text("circle")},
                              {"center", Array{3., 5.}}, {"radius", 7.125}});
  const auto initial = geometry(circle), initialRow = row(circle);
  const auto version = std::stoull(initialRow.at("geometryVersion").To<Text>());
  const auto descriptor = dimension(circle, "radius");
  Check(descriptor.at("id").To<Text>() == circle + ":radius" &&
            descriptor.at("entityId").To<Text>() == circle &&
            descriptor.at("key").To<Text>() == "radius" &&
            descriptor.at("label").To<Text>() == "R" &&
            descriptor.at("kind").To<Text>() == "radius" &&
            descriptor.at("editable").To<bool>() &&
            descriptor.at("start").To<Array>().size() == 3 &&
            descriptor.at("end").To<Array>().size() == 3 &&
            descriptor.at("offsetDirection").To<Array>().size() == 3 &&
            nearlyEqual(descriptor.at("value").To<double>(), 7.125),
        "native radius descriptor must retain exact geometry and stable identity");
  const auto stateBeforeQuery = Call(endpoint, "PlaneScene", "ReadState");
  const auto resourcesBeforeQuery = project->GetMainScene().Resources().GetManifest().size();
  Check(geometry(circle) == initial && geometry(circle) == initial &&
            Call(endpoint, "PlaneScene", "ReadState") == stateBeforeQuery &&
            project->GetMainScene().Resources().GetManifest().size() == resourcesBeforeQuery,
        "dimension queries must not initialize, transact or publish resources");
  Check(edit(circle, {{"radius", 9.375}}).at("id").To<Text>() == circle &&
            nearlyEqual(curve(circle).at("radius").To<double>(), 9.375) &&
            curve(circle).at("center") == initial.at("curves").To<Array>().front().To<Map>().at("center"),
        "radius edit must retain the same object and exact center");
  const auto edited = geometry(circle), editedRow = row(circle);
  Check(std::stoull(editedRow.at("geometryVersion").To<Text>()) == version + 1 &&
            edited.at("geometryRef") != initial.at("geometryRef") &&
            editedRow.at("previewRef") != initialRow.at("previewRef"),
        "dimension edit must advance exact and displayed geometry identity");
  Call(endpoint, "Project", "Undo");
  Check(geometry(circle) == initial, "dimension undo must restore exact original geometry and refs");
  Call(endpoint, "Project", "Redo");
  Check(geometry(circle) == edited, "dimension redo must restore exact edited geometry and refs");
  Call(endpoint, "Project", "Undo");
  edit(circle, {{"radius", 12.5}});
  const auto branch = geometry(circle);
  Check(branch.at("geometryVersion") == edited.at("geometryVersion") &&
            branch.at("geometryRef") != edited.at("geometryRef"),
        "undo branch must demonstrate equal numeric versions with distinct geometry refs");
  reject({{"id", circle}, {"operation", Text("dimensions")}, {"radius", 15.},
          {"expectedGeometryRef", edited.at("geometryRef")}}, "PLANE_STALE_GEOMETRY");
  reject({{"id", circle}, {"operation", Text("dimensions")}, {"radius", 15.},
          {"expectedGeometryRef", initial.at("geometryRef")}}, "PLANE_STALE_GEOMETRY");
  reject({{"id", circle}, {"operation", Text("dimensions")}, {"radius", 15.}},
         "PLANE_DIMENSION_GEOMETRY_REF_REQUIRED");
  reject({{"id", circle}, {"operation", Text("dimensions")}, {"radius", 15.},
          {"expectedGeometryRef", Map{{"url", Text("bad")}, {"version", 1.}}}},
         "PLANE_DIMENSION_GEOMETRY_REF_REQUIRED");
  invalid(circle, {{"radius", 0.}}, "PLANE_INVALID_DIMENSION");
  invalid(circle, {{"radius", -2.}}, "PLANE_INVALID_DIMENSION");
  invalid(circle, {{"radius", Text("5")}}, "PLANE_NUMERIC_VALUE_REQUIRED");
  invalid(circle, {{"radius", true}}, "PLANE_NUMERIC_VALUE_REQUIRED");
  invalid(circle, {{"radius", 1e10}}, "PLANE_INVALID_NUMBER");
  invalid(circle, {{"width", 5.}}, "PLANE_DIMENSION_PARAMETER_UNSUPPORTED");
  invalid(circle, {{"startAngleDeg", 0.}}, "PLANE_DIMENSION_PARAMETER_UNSUPPORTED");
  invalid(circle, {{"sweepAngleDeg", 90.}}, "PLANE_DIMENSION_PARAMETER_UNSUPPORTED");
  invalid(circle, {}, "PLANE_DIMENSION_VALUE_REQUIRED");
  invalid(circle, {{"radius", 5.}, {"expectedRevision", Text("not-current")}},
          "PLANE_STALE_REVISION");

  const auto arc = create({{"primitive", Text("arc")}, {"center", Array{10., 20.}},
                          {"radius", 5.}, {"startAngleDeg", 37.}, {"endAngleDeg", 213.}});
  const auto beforeArc = curve(arc);
  const auto initialArc = geometry(arc);
  const auto arcStart = dimension(arc, "startAngleDeg"), arcSweep = dimension(arc, "sweepAngleDeg");
  Check(initialArc.at("dimensions").To<Array>().size() == 3 &&
            arcStart.at("id").To<Text>() == arc + ":startAngleDeg" &&
            arcStart.at("key").To<Text>() == "startAngleDeg" &&
            arcStart.at("kind").To<Text>() == "angle" &&
            arcStart.at("unit").To<Text>() == "deg" &&
            arcStart.at("label").To<Text>() == "α" &&
            arcSweep.at("label").To<Text>() == "θ" &&
            arcStart.at("editable").To<bool>() && arcSweep.at("editable").To<bool>() &&
            pointIs(arcStart.at("center"), 10., 20.) &&
            nearlyEqual(arcStart.at("radius").To<double>(), 5.) &&
            nearlyEqual(arcStart.at("value").To<double>(), 37.) &&
            nearlyEqual(arcStart.at("startAngleDeg").To<double>(), 0.) &&
            nearlyEqual(arcStart.at("sweepAngleDeg").To<double>(), 37.) &&
            nearlyEqual(arcSweep.at("value").To<double>(), 176.) &&
            nearlyEqual(arcSweep.at("startAngleDeg").To<double>(), 37.) &&
            nearlyEqual(arcSweep.at("sweepAngleDeg").To<double>(), 176.) &&
            pointIs(arcStart.at("start"), 15., 20.),
        "arc queries must derive editable start/sweep angle descriptors with degree units and world anchors");
  edit(arc, {{"radius", 8.25}});
  const auto afterArc = curve(arc);
  Check(afterArc.at("center") == beforeArc.at("center") &&
            afterArc.at("startAngleDeg") == beforeArc.at("startAngleDeg") &&
            afterArc.at("endAngleDeg") == beforeArc.at("endAngleDeg") &&
            nearlyEqual(afterArc.at("radius").To<double>(), 8.25),
        "arc radius edit must preserve exact center and both angular limits");
  Check(edit(arc, {{"startAngleDeg", -10.}}).at("id").To<Text>() == arc,
        "arc angle edits must keep the same ECS entity");
  const auto negativeStart = curve(arc);
  Check(nearlyEqual(negativeStart.at("startAngleDeg").To<double>(), 350.) &&
            nearlyEqual(negativeStart.at("endAngleDeg").To<double>(), 526.) &&
            nearlyEqual(dimension(arc, "startAngleDeg").at("value").To<double>(), 350.) &&
            nearlyEqual(dimension(arc, "sweepAngleDeg").at("value").To<double>(), 176.) &&
            negativeStart.at("center") == afterArc.at("center") &&
            negativeStart.at("radius") == afterArc.at("radius"),
        "negative start angles normalize while keeping the existing sweep, center and radius");
  edit(arc, {{"sweepAngleDeg", 25.}});
  const auto crossingZero = curve(arc);
  Check(crossingZero.at("startAngleDeg") == negativeStart.at("startAngleDeg") &&
            nearlyEqual(crossingZero.at("endAngleDeg").To<double>(), 375.) &&
            nearlyEqual(dimension(arc, "sweepAngleDeg").at("value").To<double>(), 25.) &&
            crossingZero.at("center") == afterArc.at("center") &&
            crossingZero.at("radius") == afterArc.at("radius"),
        "sweep angle edits must cross zero in the positive direction and preserve their start ray");
  const auto beforeCombinedArc = geometry(arc);
  const auto beforeCombinedVersion = std::stoull(row(arc).at("geometryVersion").To<Text>());
  edit(arc, {{"radius", 12.5}, {"startAngleDeg", -390.}, {"sweepAngleDeg", 270.}});
  const auto combinedArc = geometry(arc), combinedCurve = curve(arc);
  Check(nearlyEqual(combinedCurve.at("startAngleDeg").To<double>(), 330.) &&
            nearlyEqual(combinedCurve.at("endAngleDeg").To<double>(), 600.) &&
            nearlyEqual(combinedCurve.at("radius").To<double>(), 12.5) &&
            combinedCurve.at("center") == afterArc.at("center") &&
            nearlyEqual(dimension(arc, "sweepAngleDeg").at("value").To<double>(), 270.) &&
            std::stoull(row(arc).at("geometryVersion").To<Text>()) == beforeCombinedVersion + 1,
        "radius/start/sweep must apply together in one geometry revision including a major arc");
  Call(endpoint, "Project", "Undo");
  Check(geometry(arc) == beforeCombinedArc, "arc angle undo must restore all dimensions and references together");
  Call(endpoint, "Project", "Redo");
  Check(geometry(arc) == combinedArc, "arc angle redo must restore all edited dimensions and references together");
  reject({{"id", arc}, {"operation", Text("dimensions")}, {"sweepAngleDeg", 45.},
          {"expectedGeometryRef", beforeCombinedArc.at("geometryRef")}}, "PLANE_STALE_GEOMETRY");
  edit(arc, {{"sweepAngleDeg", 360.}});
  const auto fullArc = curve(arc), fullSweep = dimension(arc, "sweepAngleDeg");
  Check(fullArc.at("startAngleDeg") == combinedCurve.at("startAngleDeg") &&
            fullArc.at("endAngleDeg") == fullArc.at("startAngleDeg") &&
            fullSweep.at("value").To<double>() == 360. &&
            fullSweep.at("sweepAngleDeg").To<double>() == 360. &&
            fullSweep.at("start") == fullSweep.at("end"),
        "a 360-degree sweep must retain a full arc and equal ray endpoints rather than becoming a zero arc");
  edit(arc, {{"startAngleDeg", 0.}});
  Check(dimension(arc, "startAngleDeg").at("value").To<double>() == 0. &&
            dimension(arc, "startAngleDeg").at("sweepAngleDeg").To<double>() == 0. &&
            dimension(arc, "sweepAngleDeg").at("value").To<double>() == 360. &&
            curve(arc).at("endAngleDeg").To<double>() == 0.,
        "a zero start angle is valid and distinct from the full positive sweep");
  invalid(arc, {{"sweepAngleDeg", 0.}}, "PLANE_INVALID_ARC_SWEEP");
  invalid(arc, {{"sweepAngleDeg", -1.}}, "PLANE_INVALID_ARC_SWEEP");
  invalid(arc, {{"sweepAngleDeg", 360.0001}}, "PLANE_INVALID_ARC_SWEEP");
  invalid(arc, {{"sweepAngleDeg", 720.}}, "PLANE_INVALID_ARC_SWEEP");
  invalid(arc, {{"sweepAngleDeg", Text("90")}}, "PLANE_NUMERIC_VALUE_REQUIRED");
  invalid(arc, {{"sweepAngleDeg", true}}, "PLANE_NUMERIC_VALUE_REQUIRED");
  invalid(arc, {{"startAngleDeg", Text("0")}}, "PLANE_NUMERIC_VALUE_REQUIRED");
  invalid(arc, {{"startAngleDeg", false}}, "PLANE_NUMERIC_VALUE_REQUIRED");
  invalid(arc, {{"startAngleDeg", 1e10}}, "PLANE_INVALID_NUMBER");
  invalid(arc, {{"startAngleDeg", std::nan("")}}, "PLANE_INVALID_NUMBER");
  invalid(arc, {{"startAngleDeg", HUGE_VAL}}, "PLANE_INVALID_NUMBER");
  invalid(arc, {{"sweepAngleDeg", std::nan("")}}, "PLANE_INVALID_NUMBER");
  invalid(arc, {{"sweepAngleDeg", HUGE_VAL}}, "PLANE_INVALID_NUMBER");
  invalid(arc, {{"radius", 6.}, {"startAngleDeg", 15.}, {"sweepAngleDeg", 0.}}, "PLANE_INVALID_ARC_SWEEP");
  edit(arc, {{"startAngleDeg", 37.}, {"sweepAngleDeg", 90.}});
  invalid(arc, {{"sweepAngleDeg", 1e-20}}, "PLANE_INVALID_ARC_SWEEP");
  edit(arc, {{"startAngleDeg", 0.}, {"sweepAngleDeg", 1e-20}});
  Check(curve(arc).at("endAngleDeg").To<double>() == 1e-20 &&
            dimension(arc, "sweepAngleDeg").at("value").To<double>() == 1e-20,
        "a representable tiny positive sweep at zero must remain tiny rather than becoming a full circle");
  for (const double start : {0.1, 37., 359.9}) {
    edit(arc, {{"startAngleDeg", start}, {"sweepAngleDeg", 360.}});
    Check(curve(arc).at("startAngleDeg") == curve(arc).at("endAngleDeg") &&
              dimension(arc, "sweepAngleDeg").at("value").To<double>() == 360.,
          "full sweeps must remain 360 at fractional and large start rays");
  }
  const auto beforeFullRadius = curve(arc);
  edit(arc, {{"radius", 4.5}});
  Check(curve(arc).at("startAngleDeg") == beforeFullRadius.at("startAngleDeg") &&
            curve(arc).at("endAngleDeg") == beforeFullRadius.at("endAngleDeg") &&
            dimension(arc, "sweepAngleDeg").at("value").To<double>() == 360.,
        "radius-only editing must preserve the exact stored full-circle angular limits");
  std::cerr << "[phase] native arc angle descriptors/edit/atomicity/history passed\n" << std::flush;
  const auto line = create({{"primitive", Text("line")},
                            {"points", Array{Array{5., 7.}, Array{8., 11.}}}});
  Check(nearlyEqual(dimension(line, "length").at("value").To<double>(), 5.),
        "line length must come from exact endpoints");
  edit(line, {{"length", 12.5}});
  const auto linePoints = curve(line).at("points").To<Array>();
  Check(pointIs(linePoints[0], 5., 7.) && pointIs(linePoints[1], 12.5, 17.),
        "line length edit must preserve first point and direction");
  invalid(line, {{"width", 4.}}, "PLANE_DIMENSION_PARAMETER_UNSUPPORTED");
  const auto openLine = create({{"primitive", Text("polyline")}, {"closed", false},
                                {"points", Array{Array{2., 3.}, Array{2., 8.}}}});
  edit(openLine, {{"length", 7.125}});
  Check(pointIs(curve(openLine).at("points").To<Array>()[1], 2., 10.125),
        "straight two-point open polyline must support the same exact length edit");

  const Array rotatedPoints{Array{10., 20.}, Array{16., 28.},
                            Array{12., 31.}, Array{6., 23.}};
  const auto rectangle = create({{"primitive", Text("polyline")}, {"closed", true},
                                 {"points", rotatedPoints}});
  Check(nearlyEqual(dimension(rectangle, "width").at("value").To<double>(), 10.) &&
            nearlyEqual(dimension(rectangle, "height").at("value").To<double>(), 5.) &&
            pointIs(dimension(rectangle, "width").at("end"), 16., 28.) &&
            pointIs(dimension(rectangle, "height").at("end"), 6., 23.) &&
            pointIs(dimension(rectangle, "width").at("offsetDirection"), .8, -.6),
        "rotated rectangle dimensions must use its local axes and outward directions");
  const auto rectangleVersion = std::stoull(row(rectangle).at("geometryVersion").To<Text>());
  edit(rectangle, {{"width", 20.25}, {"height", 9.5}});
  auto points = curve(rectangle).at("points").To<Array>();
  Check(points[0] == rotatedPoints[0] && pointIs(points[1], 22.15, 36.2) &&
            pointIs(points[2], 14.55, 41.9) && pointIs(points[3], 2.4, 25.7) &&
            std::stoull(row(rectangle).at("geometryVersion").To<Text>()) == rectangleVersion + 1,
        "both rectangle dimensions must apply in one edit preserving anchor and local axes");
  const auto beforeSingleAxis = curve(rectangle);
  edit(rectangle, {{"height", 3.125}});
  Check(curve(rectangle).at("points").To<Array>()[0] == rotatedPoints[0] &&
            curve(rectangle).at("points").To<Array>()[1] == beforeSingleAxis.at("points").To<Array>()[1] &&
            nearlyEqual(dimension(rectangle, "width").at("value").To<double>(), 20.25) &&
            nearlyEqual(dimension(rectangle, "height").at("value").To<double>(), 3.125),
        "single local height edit must preserve the exact width edge");
  invalid(rectangle, {{"width", 22.}, {"height", -2.}}, "PLANE_INVALID_DIMENSION");
  invalid(rectangle, {{"width", Text("4")}}, "PLANE_NUMERIC_VALUE_REQUIRED");
  invalid(rectangle, {{"radius", 4.}}, "PLANE_DIMENSION_PARAMETER_UNSUPPORTED");
  const auto nonRectangle = create({{"primitive", Text("polyline")}, {"closed", true},
      {"points", Array{Array{0., 0.}, Array{20., 0.}, Array{24., 8.}, Array{4., 8.}}}});
  const auto trapezoid = create({{"primitive", Text("polyline")}, {"closed", true},
      {"points", Array{Array{0., 0.}, Array{20., 0.}, Array{18., 8.}, Array{0., 8.}}}});
  for (const auto &id : {nonRectangle, trapezoid}) {
    Check(!dimension(id, "width").at("editable").To<bool>() &&
              dimension(id, "width").at("kind").To<Text>() == "bounds",
          "general parallelogram and trapezoid must remain read-only bounds");
    invalid(id, {{"width", 5.}}, "PLANE_DIMENSION_READ_ONLY");
  }
  const auto compositePath = directory / "dimension-composite.dxf";
  Write(compositePath, DXF("0\nCIRCLE\n10\n0\n20\n0\n40\n4.125\n"
                           "0\nLINE\n10\n20\n20\n5\n11\n25\n21\n5\n"));
  const auto composite = Call(endpoint, "PlaneDrawing", "Import", {{"sourcePath", UTF8(compositePath)}})
                             .at("id").To<Text>();
  Check(nearlyEqual(dimension(composite, "width").at("value").To<double>(), 29.125) &&
            nearlyEqual(dimension(composite, "height").at("value").To<double>(), 9.125) &&
            !dimension(composite, "height").at("editable").To<bool>(),
        "composite bounds must include exact circular extrema and remain read-only");
  invalid(composite, {{"height", 10.}}, "PLANE_DIMENSION_READ_ONLY");
  Check(Model(endpoint).at("parts").To<Array>().empty() &&
            Model(endpoint).at("stocks").To<Array>().empty(),
        "drawing dimension edits must not produce parts or stock");
  const auto finalCircle = geometry(circle), finalArc = geometry(arc),
             finalLine = geometry(line), finalRectangle = geometry(rectangle);
  const auto path = directory / "drawing-dimensions.ipcam";
  runtime->SaveProjectFile(project->GetProjectID(), UTF8(path));
  Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()),
        "dimension project close failed");
  std::filesystem::remove(compositePath);
  auto reopened = runtime->OpenProjectFile(UTF8(path));
  project = reopened->GetMainProject(); diagnosticScene = &project->GetMainScene();
  endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  Check(geometry(circle) == finalCircle && geometry(arc) == finalArc &&
            geometry(line) == finalLine && geometry(rectangle) == finalRectangle,
        "precise dimensions, identities, versions and geometry refs must survive save/reopen");
  Check(nearlyEqual(dimension(composite, "width").at("value").To<double>(), 29.125),
        "read-only bounds must derive after reopen without source DXF");
  Check(runtime->CloseProjectCatalog(reopened->GetCatalogID()),
        "reopened dimension project close failed");
  diagnosticScene = previousDiagnosticScene;
  std::cerr << "[phase] native drawing dimensions/atomicity/history/persistence passed\n" << std::flush;
}
