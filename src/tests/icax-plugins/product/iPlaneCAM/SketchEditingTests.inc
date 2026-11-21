void CheckSketchEditing(
    const std::shared_ptr<iCAX::Product::CProductRuntime> &runtime,
    const std::filesystem::path &directory,
    const std::filesystem::path &fixtures) {
  auto catalog = runtime->OpenProjectCatalog("Sketch editing", {}, "Sketch editing", {});
  auto project = catalog->GetMainProject();
  auto endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  const auto previousDiagnosticScene = diagnosticScene;
  diagnosticScene = &project->GetMainScene();
  const auto row = [&](const Text &collection, const Text &id) {
    for (const auto &value : Model(endpoint).at(collection).To<Array>()) {
      const auto item = value.To<Map>();
      if (item.at("id").To<Text>() == id) return item;
    }
    throw std::runtime_error("sketch acceptance row missing: " + collection + "/" + id);
  };
  const auto query = [&](const Text &id) {
    return Call(endpoint, "PlaneSketch", "ReadGeometry", {{"id", id}});
  };
  const auto curveIDs = [&](const Text &id) { return row("sketches", id).at("curveIds").To<Array>(); };
  const auto geometryIdentity = [](const Map &object) {
    Map result;
    for (const auto *key : {"id", "geometryRef", "geometryVersion", "previewRef", "width", "height", "holeCount", "quantity", "sourceName", "sourceUnits", "units"})
      if (object.contains(key)) result[key] = object.at(key);
    return result;
  };
  struct Snapshot {
    std::map<Text, std::map<Text, std::uint64_t>> Versions;
    std::vector<std::tuple<iCAX::Data::uuid, Text>> Undo, Redo;
    bool operator==(const Snapshot &) const = default;
  };
  const auto snapshot = [&]() {
    auto &scene = project->GetMainScene();
    Check(scene.IsRunning(), "sketch snapshot requires a running independent scene");
    scene.Stop();
    Snapshot result;
    try {
      auto &repository = scene.Database();
      for (const auto &id : repository.GetEntityIDs()) {
        const auto entity = repository.GetEntity(id);
        for (const auto &component : entity->GetComponentClasses())
          result.Versions[iCAX::Data::to_string(id)][component] = repository.GetComponentVersion(id, component);
      }
      result.Undo = repository.GetUndoArray(); result.Redo = repository.GetRedoArray();
    } catch (...) { scene.Start();throw; }
    scene.Start();return result;
  };
  const auto reject = [&](const Text &group, const Text &method, const Map &request, const Text &code) {
    const auto before = Call(endpoint, "PlaneScene", "ReadState");
    const auto resources = project->GetMainScene().Resources().GetManifest().size();
    const auto components = snapshot();
    bool rejected = false;
    try { Call(endpoint, group, method, request); }
    catch (const std::exception &error) { rejected = Text(error.what()).find(code) != Text::npos; }
    Check(rejected, "sketch/layer rejection must preserve its declared reason");
    Check(Call(endpoint, "PlaneScene", "ReadState") == before &&
              project->GetMainScene().Resources().GetManifest().size() == resources && snapshot() == components,
          "rejected sketch/layer command must preserve all ECS versions, resources and undo/redo");
  };
  const auto edit = [&](const Text &id, Map values) {
    values["id"] = id;values["expectedGeometryRef"] = query(id).at("geometryRef");
    return Call(endpoint, "PlaneSketch", "Edit", values);
  };
  const auto waitEditTask = [&](const Text &id) {
    Map state;
    for (int attempt = 0; attempt < 2000; ++attempt) {
      state = Call(endpoint, "PlaneTask", "Read", {{"taskId", id}});
      if (state.at("status").To<Text>() != "Running") return state;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("native sketch task did not finish");
  };
  const auto completeThenSameFrame = [&](const Text &sketch, const Text &group, const Text &method, const Map &payload) {
    auto &scene = project->GetMainScene();
    scene.Stop();
    std::vector<std::uint64_t> ids;
    try {
      for (const auto &[requestGroup, requestMethod, requestPayload] : std::vector<std::tuple<Text, Text, Map>>{
               {"PlaneSketch", "Complete", {{"id", sketch}, {"async", true}}},
               {group, method, payload}}) {
        iCAX::Interaction::CSDOFrame request;
        request.nCallID = ++callSequence;
        request.nMethodCode = iCAX::Interaction::MakeSDOMethodCode(requestGroup.c_str(), requestMethod.c_str());
        request.nKind = iCAX::Interaction::ESDOFrameKind::Request;
        const auto bytes = iCAX::Data::VariantSerializer::Serialize(requestPayload);
        request.Payload.assign(bytes.begin(), bytes.end());
        ids.push_back(request.nCallID);endpoint.Send(request);
        std::cerr << "[call " << request.nCallID << "] " << requestGroup << "." << requestMethod << " queued before same frame\n" << std::flush;
      }
    } catch (...) { scene.Start();throw; }
    scene.Start();
    std::map<std::uint64_t, Map> results;
    for (int attempt = 0; attempt < 2000 && results.size() < ids.size(); ++attempt) {
      for (const auto &response : endpoint.Receive()) {
        if (std::find(ids.begin(), ids.end(), response.nCallID) == ids.end() ||
            response.nKind != iCAX::Interaction::ESDOFrameKind::Response) continue;
        Check(response.nStatus == iCAX::Interaction::EInvocationStatus::Ok,
              "same-frame sketch Complete/Edit requests were not accepted");
        results[response.nCallID] = iCAX::Data::VariantSerializer::Deserialize(Text(response.Payload.begin(), response.Payload.end())).To<Map>();
      }
      if (results.size() < ids.size()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(results.size() == ids.size(), "same-frame sketch responses timed out");
    return results.at(ids.front()).at("taskId").To<Text>();
  };
  const auto curveType = [&](const Text &id) {
    const auto curves = query(id).at("curves").To<Array>();
    Check(curves.size() == 1, "a sketch curve EC must own one exact curve");
    return curves.front().To<Map>().at("type").To<Text>();
  };
  const auto findCurve = [&](const Text &sketch, const Text &type) {
    for (const auto &id : curveIDs(sketch)) if (curveType(id.To<Text>()) == type) return id.To<Text>();
    throw std::runtime_error("sketch acceptance curve type missing: " + type);
  };
  const auto beforeService = snapshot();
  const auto resourcesBeforeService = project->GetMainScene().Resources().GetManifest().size();
  CheckPlaneGeometryService(project->GetMainScene());
  Check(snapshot() == beforeService && project->GetMainScene().Resources().GetManifest().size() == resourcesBeforeService,
        "injected geometry computation must not mutate the caller's ECS, resources or history");
  const auto partPath = directory / "sketch-part.dxf", stockPath = directory / "sketch-stock-with-hole.dxf";
  std::filesystem::copy_file(fixtures / "part_with_hole.dxf", partPath);
  Write(stockPath, DXF("0\nLWPOLYLINE\n90\n6\n70\n1\n10\n0\n20\n0\n10\n500\n20\n0\n10\n500\n20\n220\n10\n400\n20\n220\n10\n400\n20\n300\n10\n0\n20\n300\n"
                      "0\nCIRCLE\n10\n450\n20\n50\n40\n12\n"));
  const auto part = Call(endpoint, "PlanePart", "Import", {{"sourcePath", UTF8(partPath)}}).at("id").To<Text>();
  const auto stock = Call(endpoint, "PlaneStock", "Import", {{"sourcePath", UTF8(stockPath)}}).at("id").To<Text>();
  const auto partOriginal = geometryIdentity(row("parts", part)), stockOriginal = geometryIdentity(row("stocks", stock));
  Check(row("parts", part).at("holeCount").To<UInt>() == 1 && row("stocks", stock).at("holeCount").To<UInt>() == 1,
        "actual part and irregular stock imports must both retain their holes");
  const auto drawing = Call(endpoint, "PlaneDrawing", "Create", {{"primitive", Text("line")}, {"points", Array{Array{0., 0.}, Array{3., 4.}}}}).at("id").To<Text>();
  reject("PlaneSketch", "Open", {{"ownerId", drawing}}, "PLANE_SKETCH_OWNER_REQUIRED");
  bool wrongChannel = false;
  try { Call(runtime->GetProductFrontendSDOEndpoint(), "PlaneSketch", "Open", {{"ownerId", part}}); }
  catch (const std::exception &error) { wrongChannel = Text(error.what()).find("PLANE_SCENE_CHANNEL_REQUIRED") != Text::npos; }
  Check(wrongChannel, "sketch business cannot run on the product channel");

  Call(endpoint, "PlaneNesting", "SetParameters", {{"partGap", 2.}, {"stockMargin", 3.}, {"timeLimitSeconds", .5}});
  const auto task = Call(endpoint, "PlaneNesting", "Run").at("taskId").To<Text>();
  Map taskState;
  for (int attempt = 0; attempt < 2000; ++attempt) {
    taskState = Call(endpoint, "PlaneNesting", "ReadTask", {{"taskId", task}});
    if (taskState.at("status").To<Text>() != "Running") break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  Check(taskState.at("status").To<Text>() == "Completed", "sketch baseline must use a real completed nesting task");
  const auto result = taskState.at("resultId").To<Text>();
  const auto isStale = [&]() { return Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", result}}).at("stale").To<bool>(); };
  Check(!isStale(), "baseline nesting result must start current");

  const auto layer = Call(endpoint, "PlaneLayer", "Create", {{"name", Text("Sketch owned layer")}}).at("id").To<Text>();
  Call(endpoint, "PlaneLayer", "Assign", {{"ids", Array{part}}, {"layerId", layer}});
  Check(geometryIdentity(row("parts", part)) == partOriginal && !isStale(), "layer assignment must preserve geometry and nesting freshness");
  Call(endpoint, "PlaneLayer", "Update", {{"id", layer}, {"color", Text("#AABBCC")}});
  Check(geometryIdentity(row("parts", part)) == partOriginal && !isStale(), "layer recolor must preserve geometry and nesting freshness");
  reject("PlaneLayer", "Remove", {{"id", layer}}, "PLANE_LAYER_IN_USE");
  Call(endpoint, "PlaneLayer", "Update", {{"id", layer}, {"locked", true}});
  reject("PlaneSketch", "Open", {{"ownerId", part}}, "PLANE_OBJECT_LOCKED");
  Call(endpoint, "PlaneLayer", "Update", {{"id", layer}, {"locked", false}});
  Check(!isStale(), "layer lock changes must not invalidate unchanged geometry");
  reject("PlaneLayer", "Assign", {{"ids", Array{stock, Text("00000000-0000-0000-0000-000000000001")}}, {"layerId", layer}}, "PLANE_");
  Check(row("stocks", stock).at("layerId").To<Text>() != layer, "batch layer failure must not partially assign its first valid target");

  const auto sketch = Call(endpoint, "PlaneSketch", "Open", {{"ownerId", part}}).at("sketchId").To<Text>();
  const auto idsBefore = curveIDs(sketch);
  Check(idsBefore.size() == 2 && row("parts", part).at("sketchId").To<Text>() == sketch &&
            row("sketches", sketch).at("ownerKind").To<Text>() == "parts",
        "part sketch must have a formal owner relationship and separate exact curve EC identities");
  const auto ownerAfterOpen = row("parts", part);
  const auto outer = findCurve(sketch, "polyline"), hole = findCurve(sketch, "circle");
  const auto outerOriginal = query(outer), holeOriginal = query(hole);
  edit(outer, {{"operation", Text("move")}, {"dx", 5.}, {"dy", 3.}});
  const auto extra = Call(endpoint, "PlaneSketch", "Create", {{"sketchId", sketch}, {"primitive", Text("circle")}, {"center", Array{55., 30.}}, {"radius", 3.}}).at("id").To<Text>();
  Check(row("sketchCurves", extra).at("sketchId").To<Text>() == sketch, "new curves must belong to their formal sketch EC");
  Check(row("parts", part) == ownerAfterOpen && !isStale(), "sketch drafting must leave its owner and existing nesting result untouched");
  reject("PlaneSketch", "Edit", {{"id", outer}, {"operation", Text("move")}, {"dx", 1.}, {"expectedGeometryRef", outerOriginal.at("geometryRef")}}, "PLANE_STALE_GEOMETRY");
  Call(endpoint, "PlaneSketch", "Cancel", {{"id", sketch}});
  Check(row("parts", part) == ownerAfterOpen && geometryIdentity(row("parts", part)) == partOriginal &&
            query(outer) == outerOriginal && query(hole) == holeOriginal && curveIDs(sketch) == idsBefore &&
            row("sketches", sketch).at("status").To<Text>() == "Ready" && !isStale(),
        "cancel must restore original curve IDs/refs and leave the exact owner and task input unchanged");
  reject("PlaneSketch", "Create", {{"sketchId", sketch}, {"primitive", Text("circle")}, {"center", Array{1., 1.}}, {"radius", 1.}}, "PLANE_SKETCH_NOT_EDITING");

  Check(Call(endpoint, "PlaneSketch", "Open", {{"ownerId", part}}).at("sketchId").To<Text>() == sketch && curveIDs(sketch) == idsBefore,
        "reopening a sketch must keep its persistent identity and curves");
  edit(outer, {{"operation", Text("scale")}, {"factor", 1.25}});
  const auto newHole = Call(endpoint, "PlaneSketch", "Create", {{"sketchId", sketch}, {"primitive", Text("circle")}, {"center", Array{60., 35.}}, {"radius", 3.}}).at("id").To<Text>();
  const auto invalidLine = Call(endpoint, "PlaneSketch", "Create", {{"sketchId", sketch}, {"primitive", Text("line")}, {"points", Array{Array{10., 10.}, Array{15., 10.}}}}).at("id").To<Text>();
  reject("PlaneSketch", "Complete", {{"id", sketch}}, "PLANE_MATERIAL_");
  Check(row("parts", part) == ownerAfterOpen, "an invalid open contour must not change the owner");
  Call(endpoint, "PlaneSketch", "Remove", {{"id", invalidLine}});
  const auto editedIDs = curveIDs(sketch);
  const auto draftOuter = query(outer), draftHole = query(newHole);
  const auto completed = Call(endpoint, "PlaneSketch", "Complete", {{"id", sketch}});
  const auto partCompleted = row("parts", part);
  Check(completed.at("id").To<Text>() == part && partCompleted.at("id") == ownerAfterOpen.at("id") &&
            std::stoull(partCompleted.at("geometryVersion").To<Text>()) == std::stoull(ownerAfterOpen.at("geometryVersion").To<Text>()) + 1 &&
            std::abs(partCompleted.at("width").To<double>() - 100.) < 1e-8 && partCompleted.at("holeCount").To<UInt>() == 2 &&
            partCompleted.at("geometryRef") != ownerAfterOpen.at("geometryRef") && partCompleted.at("previewRef") != ownerAfterOpen.at("previewRef") &&
            curveIDs(sketch) == editedIDs && isStale(),
        "valid completion must update the same owner once, retain curve identities and stale its prior nesting result");
  Call(endpoint, "Project", "Undo");
  Check(row("parts", part) == ownerAfterOpen && !isStale() && row("sketches", sketch).at("status").To<Text>() == "Editing",
        "undo completion must restore the owner and current nesting input together");
  Call(endpoint, "Project", "Redo");
  Check(row("parts", part) == partCompleted && isStale() && curveIDs(sketch) == editedIDs,
        "redo completion must restore the committed geometry and stale result");

  const auto stockSketch = Call(endpoint, "PlaneSketch", "Open", {{"ownerId", stock}}).at("sketchId").To<Text>();
  const auto stockIDs = curveIDs(stockSketch);
  const auto stockOuter = findCurve(stockSketch, "polyline"), stockHole = findCurve(stockSketch, "circle");
  const auto stockHoleBefore = query(stockHole);
  const auto stockOwnerOpen = row("stocks", stock);
  edit(stockOuter, {{"operation", Text("move")}, {"dx", 4.}, {"dy", -2.}});
  edit(stockHole, {{"operation", Text("move")}, {"dx", 4.}, {"dy", -2.}});
  Check(row("stocks", stock) == stockOwnerOpen && geometryIdentity(row("stocks", stock)) == stockOriginal,
        "stock sketch drafts must not mutate the physical stock definition");
  const auto stockTask = Call(endpoint, "PlaneSketch", "Complete", {{"id", stockSketch}, {"async", true}}).at("taskId").To<Text>();
  Check(waitEditTask(stockTask).at("status").To<Text>() == "Completed", "stock completion must execute through the real framework async task");
  const auto stockCompleted = row("stocks", stock);
  Check(stockCompleted.at("id").To<Text>() == stock && stockCompleted.at("holeCount").To<UInt>() == 1 &&
            stockCompleted.at("geometryRef") != stockOwnerOpen.at("geometryRef") && curveIDs(stockSketch) == stockIDs &&
            std::stoull(stockCompleted.at("geometryVersion").To<Text>()) == std::stoull(stockOwnerOpen.at("geometryVersion").To<Text>()) + 1,
        "irregular stock and its hole must commit through their own stable sketch without becoming a drawing or part");
  const auto stockHoleAfter = query(stockHole);
  Check(stockHoleAfter.at("curves") != stockHoleBefore.at("curves"), "stock hole edit did not reach the formal sketch curve");
  Check(Call(endpoint, "PlaneSketch", "Open", {{"ownerId", stock}}).at("sketchId").To<Text>() == stockSketch,
        "async completion must retain the formal stock sketch identity");
  const auto staleTask = completeThenSameFrame(stockSketch, "PlaneSketch", "Edit",
      {{"id", stockOuter}, {"operation", Text("move")}, {"dx", 1.}, {"expectedGeometryRef", query(stockOuter).at("geometryRef")}});
  Check(waitEditTask(staleTask).at("status").To<Text>() == "Stale" && row("stocks", stock) == stockCompleted &&
            row("sketches", stockSketch).at("status").To<Text>() == "Editing",
        "a real async completion based on an older curve snapshot must not overwrite its unchanged owner");
  Call(endpoint, "PlaneSketch", "Cancel", {{"id", stockSketch}});
  Check(row("stocks", stock) == stockCompleted && query(stockHole) == stockHoleAfter,
        "cancel after an async stale result must restore the accepted formal sketch");
  Call(endpoint, "PlaneSketch", "Open", {{"ownerId", stock}});
  const auto curveLayer = Call(endpoint, "PlaneLayer", "Create", {{"name", Text("Async curve lock")}}).at("id").To<Text>();
  Call(endpoint, "PlaneLayer", "Assign", {{"ids", Array{stockOuter}}, {"layerId", curveLayer}});
  const auto resourcesBeforeLockedTask = project->GetMainScene().Resources().GetManifest().size();
  const auto lockedTask = completeThenSameFrame(stockSketch, "PlaneLayer", "Update", {{"id", curveLayer}, {"locked", true}});
  const auto lockedTaskState = waitEditTask(lockedTask);
  std::cerr << "[sketch locked task actual] " << iCAX::Data::VariantSerializer::Serialize(lockedTaskState) << '\n' << std::flush;
  const auto lockedTaskReason = lockedTaskState.at("error").To<Text>();
  Check(lockedTaskState.at("status").To<Text>() == "Stale" && !lockedTaskReason.empty() &&
            lockedTaskReason.find("输入") != Text::npos && lockedTaskReason.find("修改") != Text::npos &&
            row("stocks", stock) == stockCompleted && row("sketches", stockSketch).at("status").To<Text>() == "Editing" &&
            project->GetMainScene().Resources().GetManifest().size() == resourcesBeforeLockedTask,
        "a changed/locked source layer must mark async completion Stale with its input-change reason, preserving the owner/editing sketch and publishing no resources");
  Call(endpoint, "PlaneLayer", "Update", {{"id", curveLayer}, {"locked", false}});
  Call(endpoint, "PlaneSketch", "Cancel", {{"id", stockSketch}});
  Call(endpoint, "PlaneLayer", "Remove", {{"id", curveLayer}});
  Check(Model(endpoint).at("drawings").To<Array>().size() == 1 && Model(endpoint).at("parts").To<Array>().size() == 1 && Model(endpoint).at("stocks").To<Array>().size() == 1,
        "sketch work must preserve drawing/part/stock domain separation");

  const auto beforeQueries = Call(endpoint, "PlaneScene", "ReadState");
  const auto beforeQuerySnapshot = snapshot();
  const auto beforeQueryResources = project->GetMainScene().Resources().GetManifest().size();
  Check(query(outer) == draftOuter && query(newHole) == draftHole && Call(endpoint, "PlaneScene", "ReadState") == beforeQueries &&
            project->GetMainScene().Resources().GetManifest().size() == beforeQueryResources && snapshot() == beforeQuerySnapshot,
        "sketch queries must remain read-only for ECS, resources and history");
  const auto savedState = Model(endpoint), savedOuter = query(outer), savedHole = query(hole), savedNewHole = query(newHole);
  const auto projectPath = directory / "sketch-editing.ipcam";
  runtime->SaveProjectFile(project->GetProjectID(), UTF8(projectPath));
  Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()), "sketch catalog close failed");
  std::filesystem::remove(partPath);std::filesystem::remove(stockPath);
  auto reopened = runtime->OpenProjectFile(UTF8(projectPath));
  project = reopened->GetMainProject();diagnosticScene = &project->GetMainScene();
  endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  Check(row("parts", part) == partCompleted && row("stocks", stock) == stockCompleted &&
            Model(endpoint).at("sketches") == savedState.at("sketches") && curveIDs(sketch) == editedIDs && curveIDs(stockSketch) == stockIDs &&
            query(outer) == savedOuter && query(hole) == savedHole && query(newHole) == savedNewHole && query(stockHole) == stockHoleAfter && isStale(),
        "formal sketch/curve EC identities, owners, exact resources and result staleness must survive save/reopen without source DXFs");
  Check(runtime->CloseProjectCatalog(reopened->GetCatalogID()), "reopened sketch catalog close failed");
  diagnosticScene = previousDiagnosticScene;
  std::cerr << "[phase] native sketch EC/layers/cancel/commit/atomicity/history/persistence passed\n" << std::flush;
}
