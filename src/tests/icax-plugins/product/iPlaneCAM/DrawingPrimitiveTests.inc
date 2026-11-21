void CheckDrawingPrimitives(const std::shared_ptr<iCAX::Product::CProductRuntime>& runtime,
    const std::filesystem::path& directory, const std::filesystem::path& fixtures) {
  auto catalog = runtime->OpenProjectCatalog("Independent drawing primitives", {}, "Independent drawing primitives", {});
  auto project = catalog->GetMainProject();
  auto endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  const auto previousDiagnosticScene = diagnosticScene; diagnosticScene = &project->GetMainScene();
  const auto service = project->GetMainScene().Services().Resolve<iCAX::GeometryServices::IPlaneGeometryService>();
  const std::vector<Map> requests{
      {{"primitive", Text("ellipse")}, {"center", Array{10., 15.}}, {"radiusX", 12.}, {"radiusY", 6.}, {"rotationDeg", 25.}},
      {{"primitive", Text("ellipseArc")}, {"center", Array{40., 15.}}, {"radiusX", 12.}, {"radiusY", 6.}, {"rotationDeg", 15.}, {"startAngleDeg", 40.}, {"sweepAngleDeg", -210.}},
      {{"primitive", Text("bezier")}, {"controlPoints", Array{Array{0., 0.}, Array{4., 16.}, Array{20., 16.}, Array{24., 0.}}}, {"weights", Array{1., 1.5, 1.5, 1.}}},
      {{"primitive", Text("bspline")}, {"controlPoints", Array{Array{0., 0.}, Array{3., 8.}, Array{6., 15.}, Array{12., 15.}, Array{18., 8.}, Array{24., 0.}}}, {"weights", Array{1., 1., 1.5, 1., 1., 1.}}},
      {{"primitive", Text("triangle")}, {"points", Array{Array{0., 0.}, Array{25., 0.}, Array{10., 20.}}}},
      {{"primitive", Text("polygon")}, {"center", Array{70., 15.}}, {"radius", 12.}, {"rotationDeg", 15.}},
      {{"primitive", Text("star")}, {"center", Array{110., 15.}}, {"outerRadius", 15.}, {"innerRadius", 6.}, {"rotationDeg", 90.}}};
  const auto query = [&](const Text& id, bool sketch = false) { return Call(endpoint, sketch ? "PlaneSketch" : "PlaneDrawing", "ReadGeometry", {{"id", id}}); };
  const auto row = [&](const Text& collection, const Text& id) {
    for (const auto& value : Model(endpoint).at(collection).To<Array>()) {
      const auto item = value.To<Map>(); if (item.at("id").To<Text>() == id) return item;
    }
    throw std::runtime_error("independent primitive EC row is missing");
  };
  const auto only = [&](const Map& geometry, const Text& primitive) {
    const auto curves = geometry.at("curves").To<Array>(); Check(curves.size() == 1, "one primitive must remain one authoritative exact curve");
    const auto curve = curves.front().To<Map>();
    if (primitive == "ellipse" || primitive == "ellipseArc" || primitive == "bezier" || primitive == "bspline") {
      Check(curve.at("type").To<Text>() == "polycurve" && !curve.contains("points"), "analytic primitives must not become sampled polylines");
      const auto segments = curve.at("segments").To<Array>(); Check(segments.size() == 1, "an independent analytic primitive must have exactly one exact segment");
      const auto segment = segments.front().To<Map>();
      Check(segment.at("type").To<Text>() == (primitive == "ellipse" ? "ellipseArc" : primitive), "independent exact segment type changed");
      if (primitive == "ellipse") Check(curve.at("closed").To<bool>() && std::abs(segment.at("sweepAngleDeg").To<double>()) == 360., "an ellipse must retain an exact closed full conic");
      if (primitive == "bspline") Check(segment.at("degree").To<int>() == 3 && segment.at("knots").To<Array>().size() == segment.at("controlPoints").To<Array>().size() + 4,
          "default cubic B-spline must preserve its complete clamped knot vector");
      if (primitive == "bezier" || primitive == "bspline") Check(segment.contains("controlPoints") && segment.contains("weights"), "rational primitive control points and weights must remain exact");
    } else {
      const auto count = primitive == "triangle" ? 3u : primitive == "polygon" ? 6u : 10u;
      Check(curve.at("type").To<Text>() == "polyline" && curve.at("closed").To<bool>() && curve.at("points").To<Array>().size() == count,
          "triangle, regular polygon and star must store their actual exact closed vertices, without duplicated endpoints");
    }
    return curve;
  };
  const auto capabilities = Model(endpoint).at("capabilities").To<Map>();
  const auto supported = capabilities.at("drawingPrimitives").To<Array>();
  for (const auto& request : requests) Check(std::find(supported.begin(), supported.end(), request.at("primitive")) != supported.end(), "native drawing capabilities must advertise every implemented primitive");
  const auto options = capabilities.at("drawingPrimitiveOptions").To<Map>();
  Check(options.at("polygon").To<Map>().at("sides").To<Map>() == Map{{"default", 6}, {"min", 3}, {"max", 64}} &&
        options.at("star").To<Map>().at("tips").To<Map>() == Map{{"default", 5}, {"min", 3}, {"max", 64}}, "primitive count metadata must match native defaults and limits");
  for (const Text& primitive : std::vector<Text>{"polygon", "star"})
    for (const int count : {3, 5, 64}) {
      const auto geometry = service->CreateGeometry({{"primitive", primitive}, {"center", Array{110., 15.}},
          {"radius", 15.}, {"outerRadius", 15.}, {"innerRadius", 6.}, {"rotationDeg", 90.},
          {primitive == "polygon" ? "sides" : "tips", count}});
      Check(service->BuildGeometry(geometry.at("curves").To<Array>(), true).at("holeCount").To<UInt>() == 0,
          "rotated regular polygons and stars at valid count limits must pass exact material validation");
    }
  struct Snapshot {
    Map ModelValue;
    std::map<Text, std::map<Text, std::pair<Map, uint64_t>>> Components;
    std::vector<std::tuple<iCAX::Data::uuid, Text>> Undo, Redo;
    std::size_t Resources = 0;
    bool operator==(const Snapshot&) const = default;
  };
  const auto snapshot = [&]() {
    Snapshot result; result.ModelValue = Call(endpoint, "PlaneScene", "ReadState");
    auto& scene = project->GetMainScene(); scene.Stop();
    try {
      auto& db = scene.Database();
      for (const auto& id : db.GetEntityIDs()) { const auto entity = db.GetEntity(id);
        for (const auto& type : entity->GetComponentClasses()) result.Components[iCAX::Data::to_string(id)][type] = {entity->GetComponent(type)->GetProperties(), db.GetComponentVersion(id, type)};
      }
      result.Undo = db.GetUndoArray(); result.Redo = db.GetRedoArray(); result.Resources = scene.Resources().GetManifest().size();
    } catch (...) { scene.Start(); throw; }
    scene.Start(); return result;
  };
  const auto reject = [&](const Text& group, const Text& method, const Map& request) {
    const auto before = snapshot(); bool rejected = false;
    try { Call(endpoint, group, method, request); } catch (const std::exception& error) { rejected = Text(error.what()).find("PLANE_") != Text::npos; }
    Check(rejected && snapshot() == before, "invalid primitive must reject atomically without changing EC fields/versions, resources, history or project revision");
  };
  const auto waitTask = [&](const Text& id) {
    for (int attempt = 0; attempt < 3000; ++attempt) {
      const auto task = Call(endpoint, "PlaneTask", "Read", {{"taskId", id}});
      if (task.at("status").To<Text>() != "Running") { Check(task.at("status").To<Text>() == "Completed", "primitive array task failed"); return task; }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("primitive array task timed out");
  };
  const auto edit = [&](const Text& id, Map request) { request["id"] = id; request["expectedGeometryRef"] = query(id).at("geometryRef"); return Call(endpoint, "PlaneDrawing", "Edit", request); };
  for (const auto& request : requests) {
    const auto primitive = request.at("primitive").To<Text>(); const auto source = service->CreateGeometry(request); const auto sourceCopy = source;
    only(source, primitive);
    const auto neutral = snapshot();
    for (const Map& transform : std::vector<Map>{{{"operation", Text("move")}, {"dx", 13.}, {"dy", -7.}}, {{"operation", Text("rotate")}, {"angleDeg", 37.}, {"cx", 8.}},
          {{"operation", Text("scale")}, {"factor", 1.5}, {"cx", 8.}}, {{"operation", Text("mirror")}, {"axis", Text("x")}, {"cy", 9.}}, {{"operation", Text("copy")}, {"dx", 150.}}}) {
      const auto changed = service->TransformGeometry(source, transform); only(changed, primitive);
      if (primitive == "bezier" || primitive == "bspline") {
        const auto a = only(source, primitive).at("segments").To<Array>()[0].To<Map>(), b = only(changed, primitive).at("segments").To<Array>()[0].To<Map>();
        Check(a.at("weights") == b.at("weights") && (primitive != "bspline" || a.at("knots") == b.at("knots")), "affine transformations must preserve rational spline weights and knots");
      }
    }
    Check(source == sourceCopy && snapshot() == neutral, "geometry algorithms must not mutate source data or any EC/resource/history state");
    const auto count = Model(endpoint).at("drawings").To<Array>().size();
    const auto id = Call(endpoint, "PlaneDrawing", "Create", request).at("id").To<Text>(); const auto original = query(id); only(original, primitive);
    Check(Model(endpoint).at("drawings").To<Array>().size() == count + 1, "one drawing command must create exactly one drawing EC");
    edit(id, {{"operation", Text("move")}, {"dx", 3.}, {"dy", 4.}}); const auto moved = query(id); only(moved, primitive);
    Call(endpoint, "Project", "Undo"); Check(query(id) == original, "primitive edit undo must restore exact curve identity, data and version");
    Call(endpoint, "Project", "Redo"); Check(query(id) == moved, "primitive edit redo must restore exact curve identity, data and version");
    for (const Map& duplicate : std::vector<Map>{{{"operation", Text("copy")}, {"dx", 150.}}, {{"operation", Text("array-rectangular")}, {"rows", 1}, {"columns", 2}, {"spacingX", 200.}},
          {{"operation", Text("array-polar")}, {"count", 3}, {"angleDeg", 120.}, {"async", true}}}) {
      const auto before = Model(endpoint).at("drawings"); auto reply = edit(id, duplicate); const bool async = reply.contains("taskId"); if (async) reply = waitTask(reply.at("taskId").To<Text>());
      for (const auto& value : reply.at(async ? "resultIds" : "ids").To<Array>()) only(query(value.To<Text>()), primitive);
      const auto after = Model(endpoint).at("drawings"); Call(endpoint, "Project", "Undo"); Check(Model(endpoint).at("drawings") == before, "primitive duplication must undo atomically in one step");
      Call(endpoint, "Project", "Redo"); Check(Model(endpoint).at("drawings") == after, "primitive duplication must redo the same typed EC identities");
    }
  }
  std::vector<Map> invalid;
  const auto bad = [&](std::size_t index, const Text& key, iCAX::Data::Variant value) { auto request = requests[index]; request[key] = std::move(value); invalid.push_back(std::move(request)); };
  bad(0, "radiusX", 0.); bad(0, "radiusY", -1.); bad(0, "rotationDeg", std::numeric_limits<double>::infinity());
  bad(1, "sweepAngleDeg", 0.); bad(1, "sweepAngleDeg", 361.); bad(1, "radiusY", 0.);
  bad(2, "controlPoints", Array{Array{0., 0.}, Array{0., 0.}}); bad(2, "weights", Array{1., -1., 1., 1.});
  bad(3, "degree", 3.5); bad(3, "degree", 6); bad(3, "knots", Array{0., 0., 1., 1.}); bad(3, "weights", Array{1., 0., 1., 1., 1., 1.});
  bad(4, "points", Array{Array{0., 0.}, Array{1., 1.}}); bad(4, "points", Array{Array{0., 0.}, Array{1., 1.}, Array{2., 2.}});
  bad(5, "radius", 0.); bad(5, "sides", 2); bad(5, "sides", 65); bad(5, "sides", 4.5); bad(5, "sides", true);
  bad(6, "innerRadius", 15.); bad(6, "innerRadius", 0.); bad(6, "tips", 2); bad(6, "tips", 65); bad(6, "tips", 4.5);
  for (const auto& request : invalid) reject("PlaneDrawing", "Create", request);
  // Both independent creation entry points use the same exact service. Closed
  // shapes become material immediately; open conics/splines can be closed by
  // the shared sketch command before material validation and real nesting.
  const auto drawingsBeforeSketch = Model(endpoint).at("drawings");
  std::vector<Text> owners; std::vector<std::filesystem::path> sourceFiles;
  for (std::size_t index = 0; index < requests.size(); ++index) {
    std::cerr << "[primitive material] " << requests[index].at("primitive").To<Text>() << '\n' << std::flush;
    const auto file = directory / ("primitive-owner-" + std::to_string(index) + ".dxf"); sourceFiles.push_back(file);
    std::filesystem::copy_file(fixtures / "part_with_hole.dxf", file);
    const auto owner = Call(endpoint, "PlanePart", "Import", {{"sourcePath", UTF8(file)}}).at("id").To<Text>(); owners.push_back(owner);
    const auto sketch = Call(endpoint, "PlaneSketch", "Open", {{"ownerId", owner}}).at("sketchId").To<Text>();
    for (const auto& value : row("sketches", sketch).at("curveIds").To<Array>()) Call(endpoint, "PlaneSketch", "Remove", {{"id", value.To<Text>()}});
    auto request = requests[index]; request["sketchId"] = sketch;
    const auto id = Call(endpoint, "PlaneSketch", "Create", request).at("id").To<Text>(); const auto primitive = request.at("primitive").To<Text>(); only(query(id, true), primitive);
    auto invalidSketch = invalid[index % invalid.size()]; invalidSketch["sketchId"] = sketch; reject("PlaneSketch", "Create", invalidSketch);
    if (primitive == "ellipseArc" || primitive == "bezier" || primitive == "bspline") {
      reject("PlaneSketch", "Complete", {{"id", sketch}});
      Call(endpoint, "PlaneSketch", "Edit", {{"id", id}, {"operation", Text("close")}});
    }
    Call(endpoint, "PlaneSketch", "Complete", {{"id", sketch}});
    Check(row("parts", owner).at("holeCount").To<UInt>() == 0 && row("sketches", sketch).at("status").To<Text>() == "Ready",
        "completed independent primitive must be validated as real material on the same owning part");
  }
  Check(Model(endpoint).at("drawings") == drawingsBeforeSketch, "owned sketch creation and material completion must not rewrite the worktable drawings");
  Call(endpoint, "PlaneStock", "CreateRectangle", {{"width", 400.}, {"height", 200.}});
  Call(endpoint, "PlaneNesting", "SetParameters", {{"partGap", 1.}, {"stockMargin", 2.}, {"timeLimitSeconds", 5.}});
  const auto task = Call(endpoint, "PlaneNesting", "Run").at("taskId").To<Text>(); Map state;
  for (int attempt = 0; attempt < 3000; ++attempt) { state = Call(endpoint, "PlaneNesting", "ReadTask", {{"taskId", task}}); if (state.at("status").To<Text>() != "Running") break; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  if (state.at("status").To<Text>() != "Completed") {
    std::cerr << "[primitive nesting failure] " << iCAX::Data::VariantSerializer::Serialize(state) << '\n' << std::flush;
    if (state.contains("resultId") && !state.at("resultId").To<Text>().empty())
      std::cerr << "[primitive partial report] " << iCAX::Data::VariantSerializer::Serialize(Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", state.at("resultId")}})) << '\n' << std::flush;
  }
  Check(state.at("status").To<Text>() == "Completed", "new exact primitives require an actual completed nesting solve");
  const auto report = Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", state.at("resultId")}});
  Check(report.at("placed").To<UInt>() == requests.size() && report.at("valid").To<bool>(), "real nesting must place all seven completed exact primitive materials");
  const auto saved = Model(endpoint); std::map<Text, Map> savedGeometry;
  for (const auto& value : saved.at("drawings").To<Array>()) { const auto id = value.To<Map>().at("id").To<Text>(); savedGeometry[id] = query(id); }
  const auto path = directory / "independent-primitives.ipcam"; runtime->SaveProjectFile(project->GetProjectID(), UTF8(path));
  Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()), "primitive catalog close failed"); for (const auto& file : sourceFiles) std::filesystem::remove(file);
  catalog = runtime->OpenProjectFile(UTF8(path)); project = catalog->GetMainProject(); endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID()); diagnosticScene = &project->GetMainScene();
  Check(Model(endpoint).at("drawings") == saved.at("drawings") && Model(endpoint).at("parts") == saved.at("parts"), "source-free reopening must preserve all independent primitive EC identities and material parts");
  for (const auto& [id, geometry] : savedGeometry) Check(query(id) == geometry, "source-free reopening must preserve exact primitive segments, poles, knots, weights and versions");
  Check(Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", state.at("resultId")}}) == report, "saved real nesting output for new exact primitives changed after reopening");
  Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()), "reopened primitive catalog close failed"); diagnosticScene = previousDiagnosticScene;
  std::cerr << "[phase] independent ellipse/ellipseArc/bezier/bspline/triangle/polygon/star exact service/typed EC/atomic validation/transform/array/history/sketch material/nesting/source-free persistence passed\n" << std::flush;
}
