void CheckColoredLayers(
    const std::shared_ptr<iCAX::Product::CProductRuntime>& runtime,
    const std::filesystem::path& directory,
    const std::filesystem::path& fixtures) {
  auto catalog = runtime->OpenProjectCatalog("Colored layers", {}, "Colored layers", {});
  auto project = catalog->GetMainProject();
  auto endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  const auto previousDiagnosticScene = diagnosticScene;diagnosticScene = &project->GetMainScene();
  const auto row = [&](const Text& collection, const Text& id) {
    for (const auto& value : Model(endpoint).at(collection).To<Array>()) {
      const auto item = value.To<Map>();if (item.at("id").To<Text>() == id) return item;
    }
    throw std::runtime_error("colored layer acceptance row missing: " + collection + "/" + id);
  };
  const auto geometryIdentity = [](const Map& value) {
    Map result;
    for (const auto* key : {"id", "geometryRef", "previewRef", "geometryVersion", "width", "height", "holeCount", "quantity"})
      if (value.contains(key)) result[key] = value.at(key);
    return result;
  };
  const auto componentProperties = [&](const Text& id, const Text& type) {
    auto& scene = project->GetMainScene();scene.Stop();
    Map properties;
    try {
      const auto entity = scene.Database().GetEntity(*iCAX::Data::uuid::from_string(id));
      Check(entity && entity->GetComponent(type), "colored layer component is missing");
      properties = entity->GetComponent(type)->GetProperties();
    } catch (...) { scene.Start();throw; }
    scene.Start();return properties;
  };
  const auto display = [&](const Text& id) { return componentProperties(id, "CRenderInstanceComponent"); };
  const auto displayUses = [&](const Text& object, const Text& layer) {
    const auto properties = display(object), material = row("layers", layer).at("materialRef").To<Map>();
    return properties.at("MaterialResourceID") == material.at("url") &&
      std::to_string(properties.at("MaterialResourceVersion").To<UInt>()) == material.at("version").To<Text>();
  };
  struct Snapshot {
    std::map<Text, std::map<Text, std::pair<Map, std::uint64_t>>> Components;
    std::vector<std::tuple<iCAX::Data::uuid, Text>> Undo, Redo;
    bool operator==(const Snapshot&) const = default;
  };
  const auto snapshot = [&]() {
    auto& scene = project->GetMainScene();scene.Stop();Snapshot result;
    try {
      auto& repository = scene.Database();
      for (const auto& id : repository.GetEntityIDs()) {
        const auto entity = repository.GetEntity(id);
        for (const auto& type : entity->GetComponentClasses())
          result.Components[iCAX::Data::to_string(id)][type] = {entity->GetComponent(type)->GetProperties(), repository.GetComponentVersion(id, type)};
      }
      result.Undo = repository.GetUndoArray();result.Redo = repository.GetRedoArray();
    } catch (...) { scene.Start();throw; }
    scene.Start();return result;
  };
  const auto reject = [&](const Text& method, const Map& request, const Text& code) {
    const auto before = Call(endpoint, "PlaneScene", "ReadState");
    const auto components = snapshot();const auto resources = project->GetMainScene().Resources().GetManifest().size();
    bool rejected = false;
    try { Call(endpoint, "PlaneLayer", method, request); }
    catch (const std::exception& error) { rejected = Text(error.what()).find(code) != Text::npos; }
    Check(rejected, "invalid layer command must preserve its specific reason");
    Check(Call(endpoint, "PlaneScene", "ReadState") == before && snapshot() == components &&
      project->GetMainScene().Resources().GetManifest().size() == resources,
      "invalid layer command must preserve EC, geometry, derived resources and undo/redo atomically");
  };
  const std::vector<Text> palette{"#22C55E", "#06B6D4", "#3B82F6", "#F59E0B", "#14B8A6", "#84CC16", "#8B5CF6", "#EAB308"};
  auto model = Model(endpoint);const auto initial = model.at("layers").To<Array>();
  Check(initial.size() == palette.size(), "new projects must initialize eight colored layers");
  for (std::size_t index = 0; index < palette.size(); ++index) {
    const auto layer = initial[index].To<Map>();
    Check(layer.at("color").To<Text>() == palette[index] && layer.at("order").To<UInt>() == index &&
      !layer.at("materialRef").To<Map>().at("url").To<Text>().empty(),
      "initial layers must use the actual Laser3D palette, stable order and native material references");
  }
  const auto first = initial.front().To<Map>().at("id").To<Text>();
  Check(model.at("currentLayerId").To<Text>() == first && initial.front().To<Map>().at("isCurrent").To<bool>(),
    "the first layer must be the current creation layer");
  reject("Create", {{"name", Text("Invalid color")}, {"color", Text("#12bad")}}, "PLANE_INVALID_LAYER_COLOR");
  const auto custom = Call(endpoint, "PlaneLayer", "Create", {{"name", Text("Custom ninth")}, {"color", Text("#123abc")}}).at("id").To<Text>();
  Check(Model(endpoint).at("layers").To<Array>().size() == 9 && row("layers", custom).at("color").To<Text>() == "#123ABC",
    "eight defaults must not impose a layer limit, and accepted hex colors normalize");
  Call(endpoint, "PlaneLayer", "SetCurrent", {{"id", custom}});
  const auto drawing = Call(endpoint, "PlaneDrawing", "Create", {{"primitive", Text("circle")}, {"center", Array{4., 6.}}, {"radius", 2.}}).at("id").To<Text>();
  const auto imported = Call(endpoint, "PlaneDrawing", "Import", {{"sourcePath", UTF8(fixtures / "part_with_hole.dxf")}}).at("id").To<Text>();
  const auto copy = Call(endpoint, "PlaneDrawing", "Edit", {{"id", drawing}, {"operation", Text("copy")}, {"dx", 10.}}).at("id").To<Text>();
  const auto originalGeometry = geometryIdentity(row("drawings", drawing));
  Check(row("drawings", drawing).at("layerId").To<Text>() == custom && row("drawings", imported).at("layerId").To<Text>() == custom &&
    row("drawings", copy).at("layerId").To<Text>() == custom && displayUses(drawing, custom) && displayUses(imported, custom) && displayUses(copy, custom),
    "new/imported/copied objects must persist current/source layer and display its material");
  const auto originalMaterial = row("layers", custom).at("materialRef");
  Call(endpoint, "PlaneLayer", "Update", {{"id", custom}, {"name", Text("Renamed ninth")}, {"color", Text("#ABCDEF")}});
  Check(row("drawings", drawing).at("layerColor").To<Text>() == "#ABCDEF" && row("drawings", drawing).at("layerName").To<Text>() == "Renamed ninth" &&
    row("layers", custom).at("materialRef") != originalMaterial && displayUses(drawing, custom) && geometryIdentity(row("drawings", drawing)) == originalGeometry,
    "recolor must update typed layer and native material without rebuilding source geometry");
  reject("Update", {{"id", custom}, {"color", Text("red")}}, "PLANE_INVALID_LAYER_COLOR");
  const auto second = initial[1].To<Map>().at("id").To<Text>();
  Call(endpoint, "PlaneLayer", "Update", {{"id", second}, {"locked", true}});
  reject("SetCurrent", {{"id", second}}, "PLANE_LAYER_LOCKED");
  reject("Assign", {{"ids", Array{drawing}}, {"layerId", second}}, "PLANE_LAYER_LOCKED");
  reject("Remove", {{"id", custom}, {"targetLayerId", second}}, "PLANE_LAYER_LOCKED");
  Call(endpoint, "PlaneLayer", "Update", {{"id", second}, {"locked", false}});
  Call(endpoint, "PlaneLayer", "Update", {{"id", custom}, {"visible", false}, {"locked", true}});
  Check(!display(drawing).at("Visible").To<bool>() && !display(drawing).at("Selectable").To<bool>(),
    "layer hidden/locked state must reach native RenderInstance projection");
  reject("Remove", {{"id", custom}, {"targetLayerId", first}}, "PLANE_LAYER_LOCKED");
  Call(endpoint, "PlaneLayer", "Update", {{"id", custom}, {"visible", true}, {"locked", false}});

  const auto part = Call(endpoint, "PlanePart", "Import", {{"sourcePath", UTF8(fixtures / "part_with_hole.dxf")}}).at("id").To<Text>();
  const auto partGeometry = geometryIdentity(row("parts", part));
  const auto sketch = Call(endpoint, "PlaneSketch", "Open", {{"ownerId", part}}).at("id").To<Text>();
  const auto curveIDs = row("sketches", sketch).at("curveIds").To<Array>();
  Check(curveIDs.size() == 2, "layer sketch fixture must preserve outer contour and hole");
  const auto deleted = curveIDs.back().To<Text>();
  Call(endpoint, "PlaneSketch", "Remove", {{"id", deleted}});
  Call(endpoint, "PlaneLayer", "Update", {{"id", custom}, {"color", Text("#654321")}});
  Check(!display(deleted).at("Visible").To<bool>() && !display(deleted).at("Selectable").To<bool>(),
    "a layer recolor must not resurrect a soft-deleted sketch curve");
  reject("Remove", {{"id", custom}}, "PLANE_LAYER_IN_USE");
  Call(endpoint, "PlaneLayer", "Remove", {{"id", custom}, {"targetLayerId", first}});
  Check(Model(endpoint).at("currentLayerId").To<Text>() == first && row("drawings", drawing).at("layerId").To<Text>() == first &&
    row("parts", part).at("layerId").To<Text>() == first && displayUses(drawing, first) && !display(deleted).at("Visible").To<bool>() &&
    geometryIdentity(row("drawings", drawing)) == originalGeometry && geometryIdentity(row("parts", part)) == partGeometry,
    "deletion migration must atomically preserve source geometry, move current layer and update visible material");
  Call(endpoint, "Project", "Undo");
  Check(Model(endpoint).at("currentLayerId").To<Text>() == custom && row("drawings", drawing).at("layerId").To<Text>() == custom && displayUses(drawing, custom),
    "one undo must restore deleted layer, assignments and material projection");
  Call(endpoint, "Project", "Redo");
  Call(endpoint, "PlaneSketch", "Cancel", {{"id", sketch}});
  for (const auto& value : curveIDs) {
    const auto id = value.To<Text>();
    Check(row("sketchCurves", id).at("layerId").To<Text>() == first && displayUses(id, first) && display(id).at("Visible").To<bool>(),
      "Cancel must restore exact curves under the migrated backup layer and reveal tombstones again");
  }
  Check(geometryIdentity(row("parts", part)) == partGeometry, "layer migration and sketch cancellation must preserve owner geometry");

  const auto remaining = Model(endpoint).at("layers").To<Array>();
  const auto deleteUnused = remaining.back().To<Map>().at("id").To<Text>();
  Call(endpoint, "PlaneLayer", "Remove", {{"id", deleteUnused}});
  const auto savedModel = Model(endpoint);const auto savedLayers = savedModel.at("layers");
  const auto resources = project->GetMainScene().Resources().GetManifest().size();
  const auto destination = directory / "colored-layers.ipcam";
  runtime->SaveProjectFile(project->GetProjectID(), UTF8(destination));
  Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()), "colored layer catalog close failed");
  auto reopened = runtime->OpenProjectFile(UTF8(destination));
  project = reopened->GetMainProject();diagnosticScene = &project->GetMainScene();
  endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  const auto restored = Model(endpoint);
  Check(restored.at("layers") == savedLayers && restored.at("layers").To<Array>().size() == 7 &&
    restored.at("currentLayerId").To<Text>() == first && geometryIdentity(row("drawings", drawing)) == originalGeometry &&
    displayUses(drawing, first) && project->GetMainScene().Resources().GetManifest().size() == resources,
    "save/reopen must retain palette IDs/assignments/material, and intentionally removed default layers must not regrow");
  const auto stableResources = project->GetMainScene().Resources().GetManifest().size();
  const auto stable = snapshot();
  (void)Model(endpoint);(void)Model(endpoint);
  Check(snapshot() == stable && project->GetMainScene().Resources().GetManifest().size() == stableResources,
    "read-only layer queries and scene Stop/Start must not rebuild materials or append history");
  auto terminalLayers = restored.at("layers").To<Array>();
  for (const auto& value : terminalLayers) {
    const auto id = value.To<Map>().at("id").To<Text>();if (id == first) continue;
    Call(endpoint, "PlaneLayer", "Remove", {{"id", id}, {"targetLayerId", first}});
  }
  reject("Remove", {{"id", first}}, "PLANE_LAST_LAYER_REQUIRED");
  Check(runtime->CloseProjectCatalog(reopened->GetCatalogID()), "reopened colored layer catalog close failed");
  diagnosticScene = previousDiagnosticScene;
  std::cerr << "[phase] native colored layers/current/material/atomic migration/tombstones/history/persistence passed\n" << std::flush;
}
