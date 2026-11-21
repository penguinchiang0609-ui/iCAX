void CheckArtworkBusiness(
    const std::shared_ptr<iCAX::Product::CProductRuntime> &runtime,
    const std::filesystem::path &directory, const std::filesystem::path &fixtures) {
  auto catalog = runtime->OpenProjectCatalog("Artwork and machining copies", {}, "Artwork and machining copies", {});
  auto project = catalog->GetMainProject();
  auto endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  const auto previousDiagnosticScene = diagnosticScene;diagnosticScene = &project->GetMainScene();
  const auto row = [&](const Text &kind, const Text &id) {
    for (const auto &value : Model(endpoint).at(kind).To<Array>()) { const auto valueRow = value.To<Map>();if (valueRow.at("id").To<Text>() == id) return valueRow; }
    throw std::runtime_error("artwork acceptance object missing: " + kind + "/" + id);
  };
  const auto query = [&](const Text &id) { return Call(endpoint, "PlaneDrawing", "ReadGeometry", {{"id", id}}); };
  const auto nearby = [](double a, double b) { return std::abs(a - b) < 1e-7; };
  const auto waitTask = [&](const Text &id) {
    for (int attempt = 0; attempt < 3000; ++attempt) {
      const auto task = Call(endpoint, "PlaneTask", "Read", {{"taskId", id}});
      if (task.at("status").To<Text>() != "Running") return task;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("artwork native task timed out");
  };
  const auto complete = [&](const Text &group, const Text &method, const Map &request) {
    const auto accepted = Call(endpoint, group, method, request);
    Check(accepted.at("status").To<Text>() == "Running" && accepted.contains("taskId"), "artwork/load operations must return a real running native task");
    const auto task = waitTask(accepted.at("taskId").To<Text>());
    if (task.at("status").To<Text>() != "Completed") std::cerr << "[artwork task] " << group << '.' << method << ' ' << iCAX::Data::VariantSerializer::Serialize(task) << '\n' << std::flush;
    Check(task.at("status").To<Text>() == "Completed", "artwork/load task did not complete");return task;
  };
  struct Snapshot {
    std::map<Text, std::map<Text, std::pair<Map, std::uint64_t>>> Components;
    std::vector<std::tuple<iCAX::Data::uuid, Text>> Undo, Redo;
    bool operator==(const Snapshot &) const = default;
  };
  const auto snapshot = [&](bool includeAudit = false) {
    auto &scene = project->GetMainScene();scene.Stop();Snapshot result;
    try {
      auto &repository = scene.Database();
      for (const auto &id : repository.GetEntityIDs()) {
        const auto entity = repository.GetEntity(id);
        if (!includeAudit && (entity->GetComponent("CPlaneEditTaskComponent") || entity->GetComponent("CPlaneTaskInputComponent"))) continue;
        for (const auto &type : entity->GetComponentClasses()) result.Components[iCAX::Data::to_string(id)][type] = {
            entity->GetComponent(type)->GetProperties(), repository.GetComponentVersion(id, type)};
      }
      result.Undo = repository.GetUndoArray();result.Redo = repository.GetRedoArray();
    } catch (...) { scene.Start();throw; }
    scene.Start();return result;
  };
  const auto component = [&](const Text &id, const Text &name) {
    const auto saved = snapshot(true);const auto found = saved.Components.find(id);Check(found != saved.Components.end() && found->second.contains(name), "artwork data is missing its formal typed EC component");
    return found->second.at(name).first;
  };
  std::vector<Text> materialCopies;
  const auto materialDocument = [&](const Map &drawing) {
    const auto reference = drawing.at("geometryRef").To<Map>();
    const auto bytes = project->GetMainScene().Resources().Get<iCAX::Resource::CBinaryResource>(
        reference.at("url").To<Text>(), std::stoull(reference.at("version").To<Text>()));
    Check(static_cast<bool>(bytes), "material-copy geometry resource is missing");
    return iCAX::Data::VariantSerializer::Deserialize(Text(bytes->Content.begin(), bytes->Content.end())).To<Map>();
  };
  const auto assertMaterialCopy = [&](const Text &id) {
    const auto drawing = row("drawings", id), document = materialDocument(drawing);
    const auto actual = project->GetMainScene().Services().Resolve<iCAX::GeometryServices::IPlaneGeometryService>()->BuildGeometry(document.at("curves").To<Array>(), true);
    Check(drawing.at("holeCount").To<UInt>() == 1 && component(id, "CPlaneObjectComponent").at("HoleCount").To<UInt>() == 1 &&
              document.at("holeCount").To<UInt>() == 1 && document.at("holes").To<Array>().size() == 1 && actual.at("holeCount").To<UInt>() == 1,
          "a material machining copy must retain the same real exact-curve hole, geometry topology and typed EC hole count");
    if (std::find(materialCopies.begin(), materialCopies.end(), id) == materialCopies.end()) materialCopies.push_back(id);
  };
  Text traceSourceImage;
  const auto assertTraceBound = [&](const Text &id, double minimum) {
    const auto drawing = row("drawings", id), ref = drawing.at("geometryRef").To<Map>();
    const auto bytes = project->GetMainScene().Resources().Get<iCAX::Resource::CBinaryResource>(ref.at("url").To<Text>(), std::stoull(ref.at("version").To<Text>()));
    Check(static_cast<bool>(bytes), "trace geometry resource is unavailable");
    const auto document = iCAX::Data::VariantSerializer::Deserialize(Text(bytes->Content.begin(), bytes->Content.end())).To<Map>();
    Check(drawing.at("approximationBoundMm").To<double>() + 1e-9 >= minimum && nearby(drawing.at("approximationBoundMm").To<double>(), document.at("approximationBoundMm").To<double>()) &&
              drawing.at("sourceKind").To<Text>() == "image-vector" && drawing.at("sourceObjectId").To<Text>() == traceSourceImage,
          "trace transforms/arrays must retain the conservative bound in both typed EC and immutable geometry documents with image provenance");
  };
  const auto reject = [&](const Text &group, const Text &method, const Map &request, const Text &code) {
    const auto before = snapshot(true);const auto resources = project->GetMainScene().Resources().GetManifest().size();bool correct = false;
    try { Call(endpoint, group, method, request); } catch (const std::exception &error) { correct = Text(error.what()).find(code) != Text::npos;if (!correct) std::cerr << "[artwork reject] " << error.what() << '\n'; }
    Check(correct, "artwork command must reject with the declared reason");
    Check(snapshot(true) == before && project->GetMainScene().Resources().GetManifest().size() == resources,
          "a rejected artwork/load command must leave every EC property/version, resource and undo/redo entry unchanged");
  };
  const auto edit = [&](const Text &id, Map request) { request["id"] = id;request["expectedGeometryRef"] = query(id).at("geometryRef");return Call(endpoint, "PlaneDrawing", "Edit", request); };
  const auto queuedTogether = [&](const std::vector<std::tuple<Text, Text, Map>> &requests) {
    auto &scene = project->GetMainScene();scene.Stop();std::vector<std::uint64_t> ids;
    try {
      for (const auto &[group, method, payload] : requests) {
        iCAX::Interaction::CSDOFrame frame;frame.nCallID = ++callSequence;frame.nKind = iCAX::Interaction::ESDOFrameKind::Request;
        frame.nMethodCode = iCAX::Interaction::MakeSDOMethodCode(group.c_str(), method.c_str());const auto bytes = iCAX::Data::VariantSerializer::Serialize(payload);
        frame.Payload.assign(bytes.begin(), bytes.end());ids.push_back(frame.nCallID);endpoint.Send(frame);
        std::cerr << "[call " << frame.nCallID << "] " << group << '.' << method << " queued before same frame\n" << std::flush;
      }
    } catch (...) { scene.Start();throw; }
    scene.Start();std::map<std::uint64_t, Map> replies;
    for (int attempt = 0; attempt < 2000 && replies.size() < ids.size(); ++attempt) {
      for (const auto &frame : endpoint.Receive()) if (std::find(ids.begin(), ids.end(), frame.nCallID) != ids.end() && frame.nKind == iCAX::Interaction::ESDOFrameKind::Response) {
        if (frame.nStatus != iCAX::Interaction::EInvocationStatus::Ok) throw std::runtime_error("artwork same-frame command rejected: " + Text(frame.Payload.begin(), frame.Payload.end()));
        replies[frame.nCallID] = iCAX::Data::VariantSerializer::Deserialize(Text(frame.Payload.begin(), frame.Payload.end())).To<Map>();
      }
      if (replies.size() < ids.size()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(replies.size() == ids.size(), "artwork same-frame responses timed out");return replies.at(ids.front()).at("taskId").To<Text>();
  };
  const auto vectorArea = [](const Map &geometry) {
    double sum = 0;for (const auto &curveValue : geometry.at("curves").To<Array>()) { const auto points = curveValue.To<Map>().at("points").To<Array>();
      for (std::size_t index = 0; index < points.size(); ++index) { const auto a = points[index].To<Array>(), b = points[(index + 1) % points.size()].To<Array>();sum += a[0].To<double>() * b[1].To<double>() - a[1].To<double>() * b[0].To<double>(); }
    }return std::abs(sum / 2.);
  };
  const auto artLayer = Call(endpoint, "PlaneLayer", "Create", {{"name", Text("Artwork and machining")}, {"color", Text("#C04020")}}).at("id").To<Text>();
  const auto sourceLayer = Call(endpoint, "PlaneLayer", "Create", {{"name", Text("Immutable CAD sources")}}).at("id").To<Text>();
  const auto text = complete("PlaneText", "Create", {{"text", Text("iPlane A8")}, {"fontFamily", Text("Arial")}, {"fontSizeMm", 8.}, {"x", 4.}, {"y", 7.}, {"layerId", artLayer}, {"async", false}}).at("resultId").To<Text>();
  const auto firstText = row("drawings", text), firstTextGeometry = query(text);
  CheckArtworkDisplayTexture(project->GetMainScene(), firstText);
  Check(firstText.at("contentType").To<Text>() == "text" && firstText.at("text").To<Text>() == "iPlane A8" && firstText.at("layerId").To<Text>() == artLayer &&
            firstText.at("pixelWidth").To<UInt>() > 0 && firstText.at("width").To<double>() > 0 && firstTextGeometry.at("curves").To<Array>().empty() && firstTextGeometry.at("snapPoints").To<Array>().empty(),
        "editable text must have typed text/artwork data and a real bitmap, without pretending its bounding rectangle is a cutting contour");
  Check(component(text, "CPlaneTextComponent").at("TextValue").To<Text>() == "iPlane A8" && component(text, "CPlaneArtworkComponent").contains("BitmapRef"), "text must be formal typed ECS data rather than a generic resource row");
  for (const auto &dimension : firstTextGeometry.at("dimensions").To<Array>()) Check(!dimension.To<Map>().at("editable").To<bool>(), "artwork inspection dimensions must not rewrite noncurve data through a curve parameter editor");
  const auto textClone = edit(text, {{"operation", Text("copy")}, {"dx", 100.}}).at("id").To<Text>();const auto cloneBefore = row("drawings", textClone);
  complete("PlaneText", "Update", {{"id", text}, {"text", Text("Editable A8 second")}, {"fontSizeMm", 12.}, {"expectedGeometryRef", firstText.at("geometryRef")}});
  const auto updatedText = row("drawings", text);
  CheckArtworkDisplayTexture(project->GetMainScene(), updatedText);
  Check(updatedText.at("id").To<Text>() == text && updatedText.at("text").To<Text>() == "Editable A8 second" && updatedText.at("imageRef") != firstText.at("imageRef") &&
            std::stoull(updatedText.at("geometryVersion").To<Text>()) == std::stoull(firstText.at("geometryVersion").To<Text>()) + 1 &&
            row("drawings", textClone) == cloneBefore,
        "text editing must update the same entity and its real raster once while its independent typed copy preserves the original text and bitmap");
  Call(endpoint, "Project", "Undo");Check(row("drawings", text) == firstText, "text update undo must restore exact typed content, raster and geometry versions");
  Call(endpoint, "Project", "Redo");Check(row("drawings", text) == updatedText, "text update redo must restore its exact committed typed content");
  reject("PlaneText", "Update", {{"id", text}, {"text", Text("stale")}, {"expectedGeometryRef", firstText.at("geometryRef")}}, "PLANE_STALE_GEOMETRY");
  reject("PlaneText", "Create", {{"text", Text("")}}, "PLANE_TEXT_CONTENT_REQUIRED");
  reject("PlaneText", "Update", {{"id", text}, {"fontSizeMm", 0.}}, "PLANE_TEXT_INVALID_FONT");

  const auto imagePath = directory / "artwork-hole-image.png", badPath = directory / "bad-artwork-image.png", partPath = directory / "artwork-source-part.dxf";
  std::filesystem::copy_file(fixtures / "image_rect_with_hole.png", imagePath);std::filesystem::copy_file(fixtures / "part_with_hole.dxf", partPath);Write(badPath, "bad pixels");
  const auto image = complete("PlaneImage", "Import", {{"sourcePath", UTF8(imagePath)}, {"widthMm", 16.}, {"heightMm", 12.}, {"x", 10.}, {"y", 20.}, {"layerId", artLayer}}).at("resultId").To<Text>();
  traceSourceImage = image;
  const auto imageBefore = row("drawings", image);
  CheckArtworkDisplayTexture(project->GetMainScene(), imageBefore, directory / "artwork-display-texture.icrt");
  Check(component(image, "CPlaneArtworkComponent").at("TextureRef").To<Map>().at("url") == imageBefore.at("textureRef").To<Map>().at("url"),
        "display TextureRef must be an explicit typed artwork EC field, separate from algorithm BitmapRef");
  Check(imageBefore.at("contentType").To<Text>() == "image" && imageBefore.at("reference").To<bool>() && imageBefore.at("pixelWidth").To<UInt>() == 16 && imageBefore.at("pixelHeight").To<UInt>() == 12 &&
            nearby(imageBefore.at("width").To<double>(), 16.) && nearby(imageBefore.at("height").To<double>(), 12.) && component(image, "CPlaneImageComponent").contains("SourceName") && query(image).at("curves").To<Array>().empty(),
        "imported reference images must retain true pixels, physical size and typed image identity without automatically generating a toolpath");
  const auto vector = complete("PlaneImage", "Vectorize", {{"id", image}, {"threshold", 128.}, {"simplifyMm", .1}}).at("resultId").To<Text>();
  const auto traced = query(vector), vectorRow = row("drawings", vector);
  Check(row("drawings", image) == imageBefore && traced.at("curves").To<Array>().size() == 2 && nearby(vectorArea(traced), 88.) &&
            vectorRow.at("contentType").To<Text>() == "curve" && vectorRow.at("sourceKind").To<Text>() == "image-vector" && vectorRow.at("sourceObjectId").To<Text>() == image &&
            vectorRow.at("machiningSource").To<Map>().at("geometryRef") == imageBefore.at("geometryRef") && component(vector, "CPlaneMachiningSourceComponent").contains("SourceGeometryRef"),
        "only an explicit vectorize action may create one independent real two-contour processing entity; its original image and captured typed provenance must remain intact");
  assertTraceBound(vector, .1);edit(vector, {{"operation", Text("move")}, {"dx", 2.}, {"dy", 3.}});assertTraceBound(vector, .1);Check(row("drawings", image) == imageBefore, "editing traced toolpaths must never alter the source image");
  edit(vector, {{"operation", Text("mirror")}, {"axis", Text("x")}});assertTraceBound(vector, .1);
  edit(vector, {{"operation", Text("scale")}, {"factor", 2.}});assertTraceBound(vector, .2);
  edit(vector, {{"operation", Text("scale")}, {"factor", .5}});assertTraceBound(vector, .1);
  const auto copiedTrace = edit(vector, {{"operation", Text("copy")}, {"dx", 40.}}).at("id").To<Text>();assertTraceBound(copiedTrace, .1);
  for (const auto &value : edit(vector, {{"operation", Text("array-rectangular")}, {"rows", 1}, {"columns", 2}, {"spacingX", 80.}}).at("ids").To<Array>()) assertTraceBound(value.To<Text>(), .1);
  for (const auto &value : waitTask(edit(vector, {{"operation", Text("array-rectangular")}, {"rows", 1}, {"columns", 2}, {"spacingX", 120.}, {"async", true}}).at("taskId").To<Text>()).at("resultIds").To<Array>()) assertTraceBound(value.To<Text>(), .1);
  const auto vectorMoved = query(vector);
  edit(image, {{"operation", Text("move")}, {"dx", 3.}, {"dy", 4.}});edit(image, {{"operation", Text("rotate")}, {"angleDeg", 90.}, {"cx", 13.}, {"cy", 24.}});edit(image, {{"operation", Text("scale")}, {"factor", 2.}, {"cx", 13.}, {"cy", 24.}});
  const auto imageScaled = row("drawings", image);
  Check(nearby(imageScaled.at("x").To<double>(), 13.) && nearby(imageScaled.at("y").To<double>(), 24.) && nearby(imageScaled.at("angleDeg").To<double>(), 90.) &&
            nearby(imageScaled.at("width").To<double>(), 32.) && nearby(imageScaled.at("height").To<double>(), 24.) && imageScaled.at("imageRef") == imageBefore.at("imageRef") && query(vector) == vectorMoved,
        "image move/rotation/scale must change its actual physical artwork pose while keeping its pixels and prior processing copy independent");
  edit(image, {{"operation", Text("mirror")}, {"axis", Text("x")}});Check(row("drawings", image).at("scaleY").To<double>() < 0., "reference image reflection must be represented by the typed physical transform");
  Call(endpoint, "Project", "Undo");Check(row("drawings", image) == imageScaled, "image mirror undo must restore the same exact artwork placement");
  reject("PlaneImage", "Update", {{"id", image}, {"widthMm", 0.}}, "PLANE_ARTWORK_INVALID_SIZE");
  reject("PlaneDrawing", "Edit", {{"id", image}, {"operation", Text("close")}}, "PLANE_ARTWORK_EDIT_UNSUPPORTED");

  const auto drawingCount = Model(endpoint).at("drawings").To<Array>().size();const auto beforeFailure = snapshot();const auto failureResources = project->GetMainScene().Resources().GetManifest().size();
  const auto failed = waitTask(Call(endpoint, "PlaneImage", "Import", {{"sourcePath", UTF8(badPath)}, {"layerId", artLayer}}).at("taskId").To<Text>());
  Check(failed.at("status").To<Text>() == "Failed" && failed.at("resultIds").To<Array>().empty() && snapshot() == beforeFailure && project->GetMainScene().Resources().GetManifest().size() == failureResources,
        "failed image decoding may retain task audit records but must publish no business EC, resources or undo/redo change");
  auto imageRef = query(image).at("geometryRef");const auto staleVector = queuedTogether({
      {"PlaneImage", "Vectorize", {{"id", image}, {"simplifyMm", 0.}}},
      {"PlaneImage", "Update", {{"id", image}, {"x", 19.}, {"expectedGeometryRef", imageRef}}}});
  const auto changedImage = row("drawings", image);const auto staleResources = project->GetMainScene().Resources().GetManifest().size();
  Check(waitTask(staleVector).at("status").To<Text>() == "Stale" && Model(endpoint).at("drawings").To<Array>().size() == drawingCount && row("drawings", image) == changedImage &&
            project->GetMainScene().Resources().GetManifest().size() == staleResources,
        "an actual asynchronous trace based on an older image pose/version must not publish stale processing geometry");
  const auto staleText = queuedTogether({{"PlaneText", "Update", {{"id", text}, {"text", Text("Must not publish")}}},
      {"PlaneDrawing", "Edit", {{"id", text}, {"operation", Text("move")}, {"dx", 1.}}}});
  Check(waitTask(staleText).at("status").To<Text>() == "Stale" && row("drawings", text).at("text").To<Text>() == "Editable A8 second", "an obsolete text raster task must not overwrite a newer artwork edit");
  Call(endpoint, "PlaneLayer", "Update", {{"id", artLayer}, {"locked", true}});
  reject("PlaneText", "Update", {{"id", text}, {"text", Text("locked")}}, "PLANE_OBJECT_LOCKED");
  reject("PlaneImage", "Update", {{"id", image}, {"x", 1.}}, "PLANE_OBJECT_LOCKED");
  reject("PlaneImage", "Vectorize", {{"id", image}}, "PLANE_OBJECT_LOCKED");
  reject("PlaneText", "Create", {{"text", Text("locked layer")}, {"layerId", artLayer}}, "PLANE_LAYER_LOCKED");
  Call(endpoint, "PlaneLayer", "Update", {{"id", artLayer}, {"locked", false}});
  const auto countBeforeLockedTask = Model(endpoint).at("drawings").To<Array>().size();
  const auto lockedTask = queuedTogether({{"PlaneText", "Create", {{"text", Text("Layer changed during work")}, {"layerId", artLayer}}},
      {"PlaneLayer", "Update", {{"id", artLayer}, {"locked", true}}}});
  Check(waitTask(lockedTask).at("status").To<Text>() == "Stale" && Model(endpoint).at("drawings").To<Array>().size() == countBeforeLockedTask,
        "a destination-layer lock/version change must prevent asynchronous text publication");
  Call(endpoint, "PlaneLayer", "Update", {{"id", artLayer}, {"locked", false}});

  const auto part = Call(endpoint, "PlanePart", "Import", {{"sourcePath", UTF8(partPath)}, {"quantity", 2}}).at("id").To<Text>();
  Call(endpoint, "PlaneLayer", "Assign", {{"ids", Array{part}}, {"layerId", sourceLayer}});const auto partBefore = row("parts", part);
  const auto copy = complete("PlaneMachining", "LoadPart", {{"partId", part}, {"x", 10.}, {"y", 20.}, {"layerId", artLayer}}).at("resultId").To<Text>();
  const auto copyRow = row("drawings", copy), copyGeometry = query(copy);
  Check(copy != part && copyRow.at("sourceKind").To<Text>() == "part" && copyRow.at("sourceObjectId").To<Text>() == part && copyRow.at("holeCount").To<UInt>() == 1 &&
            copyRow.at("machiningSource").To<Map>().at("partId").To<Text>() == part && copyRow.at("machiningSource").To<Map>().at("geometryRef") == partBefore.at("geometryRef") && row("parts", part) == partBefore,
        "LoadPart must create one separate typed machining copy with captured source identity/ref and leave its original CAD unchanged");
  bool translatedHole = false;for (const auto &value : copyGeometry.at("curves").To<Array>()) { const auto curve = value.To<Map>();if (curve.at("type").To<Text>() == "circle") {
      const auto p = curve.at("center").To<Array>();translatedHole = nearby(p[0].To<double>(), 30.) && nearby(p[1].To<double>(), 40.); } }
  Check(translatedHole, "LoadPart must apply the requested physical translation to exact source geometry");
  assertMaterialCopy(copy);
  edit(copy, {{"operation", Text("move")}, {"dx", 5.}});Check(row("parts", part) == partBefore, "machining-copy edits must never rewrite the source CAD");const auto movedCopy = query(copy);assertMaterialCopy(copy);
  Call(endpoint, "Project", "Undo");Check(query(copy) == copyGeometry && row("parts", part) == partBefore, "undo must restore only the independent processing copy");assertMaterialCopy(copy);
  Call(endpoint, "Project", "Redo");Check(query(copy) == movedCopy, "redo must restore the independent processing edit");assertMaterialCopy(copy);
  for (const Map &transform : std::vector<Map>{{{"operation", Text("rotate")}, {"angleDeg", 37.}, {"cx", 12.}, {"cy", 23.}},
           {{"operation", Text("mirror")}, {"axis", Text("x")}, {"cy", 7.}},
           {{"operation", Text("mirror")}, {"axis", Text("y")}, {"cx", 9.}},
           {{"operation", Text("scale")}, {"factor", 1.25}, {"cx", 12.}, {"cy", 23.}}}) {
    const auto before = query(copy);edit(copy, transform);const auto after = query(copy);assertMaterialCopy(copy);
    Call(endpoint, "Project", "Undo");Check(query(copy) == before, "undo must restore exact material transform geometry");assertMaterialCopy(copy);
    Call(endpoint, "Project", "Redo");Check(query(copy) == after && row("parts", part) == partBefore, "redo must retain the transformed holes without changing the source CAD");assertMaterialCopy(copy);
  }
  const auto duplicateMaterial = [&](const Text &id, const Map &request) {
    const auto before = Model(endpoint).at("drawings");auto reply = edit(id, request);
    const bool asynchronous = reply.contains("taskId");if (asynchronous) reply = waitTask(reply.at("taskId").To<Text>());
    const auto ids = reply.at(asynchronous ? "resultIds" : "ids").To<Array>();
    for (const auto &value : ids) assertMaterialCopy(value.To<Text>());
    const auto after = Model(endpoint).at("drawings");
    Call(endpoint, "Project", "Undo");Check(Model(endpoint).at("drawings") == before, "material copy/array undo must remove exactly its destinations in one step");assertMaterialCopy(id);
    Call(endpoint, "Project", "Redo");Check(Model(endpoint).at("drawings") == after, "material copy/array redo must restore the same typed destinations and hole metadata");
    for (const auto &value : ids) assertMaterialCopy(value.To<Text>());
  };
  duplicateMaterial(copy, {{"operation", Text("copy")}, {"dx", 250.}});
  duplicateMaterial(copy, {{"operation", Text("array-rectangular")}, {"rows", 1}, {"columns", 3}, {"spacingX", 350.}});
  duplicateMaterial(copy, {{"operation", Text("array-polar")}, {"count", 3}, {"angleDeg", 120.}, {"async", true}});
  Check(row("parts", part) == partBefore, "material copy and both arrays must preserve the original CAD source");
  const auto independentCopy = query(copy);
  const auto beforeStaleCopy = Model(endpoint).at("drawings").To<Array>().size();const auto staleCopy = queuedTogether({
      {"PlaneMachining", "LoadPart", {{"partId", part}, {"layerId", artLayer}}},
      {"PlaneLayer", "Update", {{"id", sourceLayer}, {"color", Text("#305070")}}}});
  Check(waitTask(staleCopy).at("status").To<Text>() == "Stale" && Model(endpoint).at("drawings").To<Array>().size() == beforeStaleCopy, "a changed source-layer snapshot must prevent stale LoadPart publication");
  Call(endpoint, "PlaneLayer", "Update", {{"id", sourceLayer}, {"locked", true}});reject("PlaneMachining", "LoadPart", {{"partId", part}}, "PLANE_OBJECT_LOCKED");Call(endpoint, "PlaneLayer", "Update", {{"id", sourceLayer}, {"locked", false}});
  Call(endpoint, "PlaneLayer", "Update", {{"id", artLayer}, {"locked", true}});reject("PlaneMachining", "LoadPart", {{"partId", part}, {"layerId", artLayer}}, "PLANE_LAYER_LOCKED");Call(endpoint, "PlaneLayer", "Update", {{"id", artLayer}, {"locked", false}});
  const auto sketch = Call(endpoint, "PlaneSketch", "Open", {{"ownerId", part}}).at("sketchId").To<Text>();
  for (const auto &id : row("sketches", sketch).at("curveIds").To<Array>()) {
    const auto exact = Call(endpoint, "PlaneSketch", "ReadGeometry", {{"id", id.To<Text>()}});
    if (exact.at("curves").To<Array>()[0].To<Map>().at("type").To<Text>() == "polyline") Call(endpoint, "PlaneSketch", "Edit", {{"id", id.To<Text>()}, {"operation", Text("move")}, {"dx", 1.}, {"dy", 1.}});
  }
  Call(endpoint, "PlaneSketch", "Complete", {{"id", sketch}});
  Check(row("parts", part).at("geometryRef") != partBefore.at("geometryRef") && query(copy) == independentCopy && row("drawings", copy).at("machiningSource").To<Map>().at("geometryRef") == partBefore.at("geometryRef"),
        "later CAD edits must leave the machining copy and its captured historical source ref unchanged");
  Call(endpoint, "PlaneStock", "CreateRectangle", {{"width", 200.}, {"height", 120.}});Call(endpoint, "PlaneNesting", "SetParameters", {{"partGap", 1.}, {"stockMargin", 2.}, {"timeLimitSeconds", .1}});
  const auto nesting = Call(endpoint, "PlaneNesting", "Run").at("taskId").To<Text>();Map nestingState;
  for (int attempt = 0; attempt < 2000; ++attempt) { nestingState = Call(endpoint, "PlaneNesting", "ReadTask", {{"taskId", nesting}});if (nestingState.at("status").To<Text>() != "Running") break;std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  Check(nestingState.at("status").To<Text>() == "Completed", "machining result acceptance requires a real completed nesting task");
  const auto reportId = nestingState.at("resultId").To<Text>();const auto reportBefore = Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", reportId}});
  const auto nestedPartBefore = row("parts", part);
  Check(reportBefore.at("placed").To<UInt>() == 2 && reportBefore.at("boards").To<Array>().size() == 1, "fixture demand must produce one real board with two placements");
  struct ReferenceCommitObserver final : iCAX::Database::IRepositoryEventListener {
    std::map<Text, bool> SelectableAtFirstCommittedBatch;
    void OnRepositoryChanging(void *, const iCAX::Database::RepositoryEventArgs &) override {}
    void OnRepositoryChanged(void *, const iCAX::Database::RepositoryEventArgs &args) override {
      if (!args.pBatch || !args.pRepository) return;
      for (const auto &record : args.pBatch->Records) {
        const auto entity = args.pRepository->GetEntity(record.EntityID);if (!entity) continue;
        const auto source = entity->GetComponent("CPlaneMachiningSourceComponent"), render = entity->GetComponent("CRenderInstanceComponent");
        if (!source || !render) continue;const auto fields = source->GetProperties();
        if (fields.contains("ReferenceOnly") && fields.at("ReferenceOnly").To<bool>()) SelectableAtFirstCommittedBatch.try_emplace(iCAX::Data::to_string(record.EntityID), render->GetProperties().at("Selectable").To<bool>());
      }
    }
  };
  const auto observer = std::make_shared<ReferenceCommitObserver>();project->GetMainScene().Stop();project->GetMainScene().Database().AddObserver(observer);project->GetMainScene().Start();
  const auto board = reportBefore.at("boards").To<Array>()[0].To<Map>();const auto loadedBoard = complete("PlaneMachining", "LoadResult", {{"boardId", board.at("id")}, {"layerId", artLayer}});
  const auto boardCopies = loadedBoard.at("resultIds").To<Array>();Check(boardCopies.size() == 3 && loadedBoard.at("resultId") == boardCopies[0], "LoadResult must return one board reference and one independent EC per placed instance");
  const auto background = row("drawings", boardCopies[0].To<Text>());
  Check(background.at("reference").To<bool>() && background.at("sourceKind").To<Text>() == "result-board" && !component(boardCopies[0].To<Text>(), "CRenderInstanceComponent").at("Selectable").To<bool>(),
        "the board boundary must remain an unselectable typed reference rather than a processing part");
  Array referenceCopies{boardCopies[0]};referenceCopies.emplace_back(edit(boardCopies[0].To<Text>(), {{"operation", Text("copy")}, {"dx", 220.}}).at("id"));
  for (const auto &id : edit(boardCopies[0].To<Text>(), {{"operation", Text("array-rectangular")}, {"rows", 1}, {"columns", 2}, {"spacingX", 440.}}).at("ids").To<Array>()) referenceCopies.emplace_back(id);
  for (const auto &id : waitTask(edit(boardCopies[0].To<Text>(), {{"operation", Text("array-rectangular")}, {"rows", 1}, {"columns", 2}, {"spacingX", 660.}, {"async", true}}).at("taskId").To<Text>()).at("resultIds").To<Array>()) referenceCopies.emplace_back(id);
  project->GetMainScene().Stop();const auto referenceAtCommit = observer->SelectableAtFirstCommittedBatch;project->GetMainScene().Database().RemoveObserver(observer);project->GetMainScene().Start();
  for (const auto &value : referenceCopies) { const auto id = value.To<Text>();Check(referenceAtCommit.contains(id) && !referenceAtCommit.at(id) && !component(id, "CRenderInstanceComponent").at("Selectable").To<bool>(),
      "reference-board copy and sync/async array destinations must already be unselectable at their first committed repository batch, before any later scene pump repair"); }
  for (std::size_t index = 1; index < boardCopies.size(); ++index) {
    const auto id = boardCopies[index].To<Text>();const auto loaded = row("drawings", id), link = loaded.at("machiningSource").To<Map>();
    assertMaterialCopy(id);
    Check(link.at("kind").To<Text>() == "result-placement" && link.at("partId").To<Text>() == part && link.at("boardId") == board.at("id") && link.at("resultId").To<Text>() == reportId && !link.at("reference").To<bool>(), "each loaded instance must retain typed part/result/board/placement provenance");
    const double angle = link.at("angleDeg").To<double>() * 3.14159265358979323846 / 180.;double x = 20. * std::cos(angle) - 20. * std::sin(angle), y = 20. * std::sin(angle) + 20. * std::cos(angle);if (link.at("mirror").To<bool>()) y = -y;x += link.at("x").To<double>();y += link.at("y").To<double>();
    bool correctHole = false;for (const auto &value : query(id).at("curves").To<Array>()) { const auto curve = value.To<Map>();if (curve.at("type").To<Text>() == "circle") { const auto p = curve.at("center").To<Array>();correctHole = nearby(p[0].To<double>(), x) && nearby(p[1].To<double>(), y); } }
    Check(correctHole, "loaded exact geometry must follow the actual native placement rotation/mirror/translation");
  }
  edit(boardCopies[1].To<Text>(), {{"operation", Text("move")}, {"dx", 7.}});
  assertMaterialCopy(boardCopies[1].To<Text>());
  duplicateMaterial(boardCopies[1].To<Text>(), {{"operation", Text("copy")}, {"dx", 250.}});
  duplicateMaterial(boardCopies[1].To<Text>(), {{"operation", Text("array-rectangular")}, {"rows", 1}, {"columns", 2}, {"spacingX", 350.}, {"async", true}});
  duplicateMaterial(boardCopies[1].To<Text>(), {{"operation", Text("array-polar")}, {"count", 3}, {"angleDeg", 120.}});
  Check(row("parts", part) == nestedPartBefore && Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", reportId}}) == reportBefore, "editing a loaded result copy must not mutate the nesting layout, report or its CAD source");
  reject("PlaneMachining", "LoadResult", {{"boardId", part}}, "PLANE_");

  const auto saved = Model(endpoint);std::map<Text, Map> savedGeometry;
  for (const auto &value : saved.at("drawings").To<Array>()) { const auto id = value.To<Map>().at("id").To<Text>();savedGeometry[id] = query(id); }
  const auto savePath = directory / "artwork-and-machining.ipcam";runtime->SaveProjectFile(project->GetProjectID(), UTF8(savePath));Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()), "artwork catalog close failed");
  std::filesystem::remove(imagePath);std::filesystem::remove(partPath);std::filesystem::remove(badPath);
  auto reopened = runtime->OpenProjectFile(UTF8(savePath));project = reopened->GetMainProject();endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());diagnosticScene = &project->GetMainScene();
  Check(Model(endpoint).at("drawings") == saved.at("drawings") && Model(endpoint).at("parts") == saved.at("parts") && Model(endpoint).at("layers") == saved.at("layers") &&
            Call(endpoint, "PlaneNesting", "ReadReport", {{"resultId", reportId}}) == reportBefore,
        "typed text/image placements, independent machining copies/provenance/layers and unchanged result must survive save/reopen after deleting all source files");
  for (const auto &[id, expected] : savedGeometry) Check(query(id) == expected, "saved exact curve or typed artwork geometry changed after source-free reopening");
  const auto savedMaterialCopies = materialCopies;
  for (const auto &id : savedMaterialCopies) assertMaterialCopy(id);
  for (const auto &id : {text, textClone, image}) CheckArtworkDisplayTexture(project->GetMainScene(), row("drawings", id));
  Check(component(text, "CPlaneTextComponent").at("TextValue").To<Text>() == "Editable A8 second" && component(image, "CPlaneArtworkComponent").contains("BitmapRef"), "reopened text/image must retain typed editable data and embedded bitmap references");
  Check(runtime->CloseProjectCatalog(reopened->GetCatalogID()), "reopened artwork catalog close failed");diagnosticScene = previousDiagnosticScene;
  std::cerr << "[phase] artwork typed EC/text/image/vector pixels/locks/async stale/independent machining copies/history/source-free persistence passed\n" << std::flush;
}
