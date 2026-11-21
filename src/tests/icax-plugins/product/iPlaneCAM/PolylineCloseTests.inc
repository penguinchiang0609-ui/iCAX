void CheckPolylineClosure(
    const std::shared_ptr<iCAX::Product::CProductRuntime> &runtime,
    const std::filesystem::path &directory) {
  auto catalog = runtime->OpenProjectCatalog("Polyline closure", {},
                                             "Polyline closure", {});
  auto project = catalog->GetMainProject();
  auto endpoint = runtime->GetSceneFrontendSDOEndpoint(
      project->GetProjectID(), project->GetMainSceneID());
  const auto previousDiagnosticScene = diagnosticScene;
  diagnosticScene = &project->GetMainScene();
  const auto drawingRow = [&](const Text &id) {
    for (const auto &value : Model(endpoint).at("drawings").To<Array>()) {
      auto row = value.To<Map>();
      if (row.at("id").To<Text>() == id)
        return row;
    }
    throw std::runtime_error("polyline drawing row missing");
  };
  const auto exact = [&](const Text &id) {
    return Call(endpoint, "PlaneDrawing", "ReadGeometry", {{"id", id}})
        .at("curves")
        .To<Array>()[0]
        .To<Map>();
  };
  const auto create = [&](Array points, bool closed = false) {
    return Call(endpoint, "PlaneDrawing", "Create",
                {{"primitive", Text("polyline")},
                 {"points", std::move(points)},
                 {"closed", closed}})
        .at("id")
        .To<Text>();
  };
  const auto close = [&](const Text &id) {
    return Call(endpoint, "PlaneDrawing", "Edit",
                {{"id", id}, {"operation", Text("close")}});
  };
  const auto rejectedClose = [&](const Text &id, const Text &code) {
    const auto before = Call(endpoint, "PlaneScene", "ReadState");
    const auto resources =
        project->GetMainScene().Resources().GetManifest().size();
    bool rejected = false;
    try {
      close(id);
    } catch (const std::exception &error) {
      rejected = Text(error.what()).find(code) != Text::npos;
    }
    Check(rejected, "close must reject with its declared validation reason");
    Check(Call(endpoint, "PlaneScene", "ReadState") == before,
          "rejected close must preserve all ECS values and versions");
    Check(project->GetMainScene().Resources().GetManifest().size() == resources,
          "rejected close must not publish Embedded resources");
  };
  const Array triangle{Array{0., 0.}, Array{20., 0.}, Array{20., 20.}};
  const auto target = create(triangle);
  auto row = drawingRow(target);
  Check(row.at("isPolyline").To<bool>() && !row.at("closed").To<bool>() &&
            row.at("canClosePolyline").To<bool>(),
        "one valid open polyline must advertise native close capability");
  const auto version = std::stoull(row.at("geometryVersion").To<Text>());
  Check(close(target).at("id").To<Text>() == target,
        "close must retain source object identity");
  row = drawingRow(target);
  auto curve = exact(target);
  Check(row.at("closed").To<bool>() && !row.at("canClosePolyline").To<bool>() &&
            curve.at("closed").To<bool>(),
        "close query state and exact geometry must agree");
  Check(std::stoull(row.at("geometryVersion").To<Text>()) == version + 1 &&
            curve.at("points").To<Array>() == triangle,
        "close must version the existing object without appending its first "
        "vertex");
  Call(endpoint, "Project", "Undo");
  Check(!exact(target).at("closed").To<bool>() &&
            drawingRow(target).at("canClosePolyline").To<bool>() &&
            std::stoull(drawingRow(target).at("geometryVersion").To<Text>()) ==
                version,
        "framework undo must restore open geometry and original version");
  Call(endpoint, "Project", "Redo");
  Check(exact(target).at("closed").To<bool>() &&
            std::stoull(drawingRow(target).at("geometryVersion").To<Text>()) ==
                version + 1,
        "framework redo must restore closed geometry and version");
  rejectedClose(target, "PLANE_POLYLINE_ALREADY_CLOSED");

  const auto createdClosed = create(triangle, true);
  Check(exact(createdClosed).at("closed").To<bool>() &&
            drawingRow(createdClosed).at("closed").To<bool>() &&
            !drawingRow(createdClosed).at("canClosePolyline").To<bool>(),
        "Create.closed=true must remain supported");
  rejectedClose(createdClosed, "PLANE_POLYLINE_ALREADY_CLOSED");
  const auto shortLine = create(Array{Array{0., 0.}, Array{20., 0.}});
  Check(!drawingRow(shortLine).at("canClosePolyline").To<bool>(),
        "two vertices cannot close a polyline");
  rejectedClose(shortLine, "PLANE_CLOSE_REQUIRES_THREE_DISTINCT_POINTS");
  const auto repeatedOnly = create(
      Array{Array{0., 0.}, Array{20., 0.}, Array{0., 0.}, Array{20., 0.}});
  rejectedClose(repeatedOnly, "PLANE_CLOSE_REQUIRES_THREE_DISTINCT_POINTS");
  const auto zeroEdge = create(
      Array{Array{0., 0.}, Array{20., 0.}, Array{20., 0.}, Array{20., 20.}});
  Check(!drawingRow(zeroEdge).at("canClosePolyline").To<bool>(),
        "zero adjacent edge must not advertise close");
  rejectedClose(zeroEdge, "PLANE_CLOSE_ZERO_LENGTH_EDGE");
  const auto repeatedTail =
      create(Array{Array{0., 0.}, Array{20., 0.}, Array{20., 20.},
                   Array{0., 0.}, Array{0., 0.}});
  Check(drawingRow(repeatedTail).at("canClosePolyline").To<bool>(),
        "repeated first-point tails must be canonicalizable");
  close(repeatedTail);
  Check(exact(repeatedTail).at("points").To<Array>() == triangle,
        "closing must remove repeated tail vertices without adding zero-length "
        "edges");

  const auto line = Call(endpoint, "PlaneDrawing", "Create",
                         {{"primitive", Text("line")},
                          {"points", Array{Array{0., 0.}, Array{20., 0.}}}})
                        .at("id")
                        .To<Text>();
  Check(!drawingRow(line).at("isPolyline").To<bool>() &&
            !drawingRow(line).at("canClosePolyline").To<bool>(),
        "ordinary line must not advertise polyline close");
  rejectedClose(line, "PLANE_CLOSE_REQUIRES_SINGLE_OPEN_POLYLINE");
  const auto circle = Call(endpoint, "PlaneDrawing", "Create",
                           {{"primitive", Text("circle")},
                            {"center", Array{0., 0.}},
                            {"radius", 5.}})
                          .at("id")
                          .To<Text>();
  rejectedClose(circle, "PLANE_CLOSE_REQUIRES_SINGLE_OPEN_POLYLINE");
  Check(Model(endpoint).at("parts").To<Array>().empty() &&
            Model(endpoint).at("stocks").To<Array>().empty(),
        "closing drawing geometry must not derive a part or stock");
  const auto stock = Call(endpoint, "PlaneStock", "CreateRectangle",
                          {{"width", 50.}, {"height", 50.}})
                         .at("id")
                         .To<Text>();
  rejectedClose(stock, "PLANE_DRAWING_REQUIRED");

  const auto unusedBulgePath = directory / "open-unused-bulge.dxf";
  const auto usedBulgePath = directory / "open-used-bulge-tail.dxf";
  const auto multiPath = directory / "multiple-curves.dxf";
  const Text prefix = "0\nLWPOLYLINE\n90\n3\n70\n0\n10\n0\n20\n0\n10\n20\n20\n0"
                      "\n10\n20\n20\n20\n42\n0.75\n";
  Write(unusedBulgePath, DXF(prefix));
  Write(usedBulgePath,
        DXF("0\nLWPOLYLINE\n90\n4\n70\n0\n10\n0\n20\n0\n10\n20\n20\n0\n10\n20\n"
            "20\n20\n42\n0.25\n10\n0\n20\n0\n42\n0.75\n"));
  Write(multiPath,
        DXF(prefix + "0\nLINE\n10\n100\n20\n100\n11\n110\n21\n100\n"));
  const auto importDrawing = [&](const std::filesystem::path &path) {
    return Call(endpoint, "PlaneDrawing", "Import",
                {{"sourcePath", UTF8(path)}})
        .at("id")
        .To<Text>();
  };
  const auto unusedBulge = importDrawing(unusedBulgePath);
  close(unusedBulge);
  curve = exact(unusedBulge);
  Check(curve.at("points").To<Array>() == triangle &&
            curve.at("bulges").To<Array>().size() == 3 &&
            curve.at("bulges").To<Array>().back().To<double>() == 0.,
        "new final-to-first segment must be straight, not reuse an unused "
        "open-tail bulge");
  const auto usedBulge = importDrawing(usedBulgePath);
  close(usedBulge);
  curve = exact(usedBulge);
  Check(curve.at("points").To<Array>() == triangle &&
            curve.at("bulges").To<Array>().size() == 3 &&
            curve.at("bulges").To<Array>().back().To<double>() == .25,
        "removing a repeated tail must retain the preceding existing curved "
        "segment");
  const auto multi = importDrawing(multiPath);
  Check(!drawingRow(multi).at("canClosePolyline").To<bool>(),
        "multi-curve drawing cannot close as one polyline");
  rejectedClose(multi, "PLANE_CLOSE_REQUIRES_SINGLE_OPEN_POLYLINE");

  const auto path = directory / "polyline-closed.ipcam";
  runtime->SaveProjectFile(project->GetProjectID(), UTF8(path));
  Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()),
        "polyline test catalog close failed");
  std::filesystem::remove(unusedBulgePath);
  std::filesystem::remove(usedBulgePath);
  std::filesystem::remove(multiPath);
  auto reopened = runtime->OpenProjectFile(UTF8(path));
  project = reopened->GetMainProject();
  diagnosticScene = &project->GetMainScene();
  endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(),
                                                  project->GetMainSceneID());
  Check(exact(target).at("closed").To<bool>() &&
            exact(target).at("points").To<Array>() == triangle &&
            !drawingRow(target).at("canClosePolyline").To<bool>() &&
            std::stoull(drawingRow(target).at("geometryVersion").To<Text>()) ==
                version + 1,
        "closed state, identity and version must survive native save/reopen");
  Check(exact(usedBulge).at("bulges").To<Array>().back().To<double>() == .25,
        "exact existing bulge must survive save/reopen without its source DXF");
  Check(runtime->CloseProjectCatalog(reopened->GetCatalogID()),
        "reopened polyline test catalog close failed");
  diagnosticScene = previousDiagnosticScene;
  std::cerr << "[phase] polyline close "
               "validation/atomicity/undo/redo/save/reopen passed\n"
            << std::flush;
}
