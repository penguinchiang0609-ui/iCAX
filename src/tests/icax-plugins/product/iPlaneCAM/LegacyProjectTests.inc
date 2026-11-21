void CheckLegacyPlaneProject(
    const std::shared_ptr<iCAX::Product::CProductRuntime> &runtime,
    const std::filesystem::path &directory,
    const std::filesystem::path &source) {
  Check(source.extension() == ".ipcam" && source.generic_string().find("/output/tests/") != Text::npos,
        "legacy acceptance only reads a prior self-owned output/tests project");
  const auto bytes = [](const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);Check(static_cast<bool>(stream), "legacy acceptance source missing");
    return Text(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  };
  const auto original = bytes(source);
  const auto caseID = iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
  const auto copy = directory / ("legacy-copy-" + caseID + ".ipcam"), resaved = directory / ("legacy-resaved-" + caseID + ".ipcam");
  std::filesystem::copy_file(source, copy);
  auto catalog = runtime->OpenProjectFile(UTF8(copy));
  auto project = catalog->GetMainProject();
  auto endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  const auto previousDiagnosticScene = diagnosticScene;
  diagnosticScene = &project->GetMainScene();
  const auto identities = [&](const Map &model) {
    Map result;
    for (const auto *collection : {"drawings", "parts", "stocks"}) {
      for (const auto &value : model.at(collection).To<Array>()) {
        const auto row = value.To<Map>();
        Check(!row.at("geometryRef").To<Map>().at("url").To<Text>().empty(), "legacy geometry reference missing");
        result[row.at("id").To<Text>()] = Map{{"geometryRef", row.at("geometryRef")},
            {"previewRef", row.at("previewRef")}, {"geometryVersion", row.at("geometryVersion")}};
      }
    }
    return result;
  };
  const auto model = Model(endpoint), before = identities(model);
  const auto layers = model.at("layers");
  const auto current = model.at("currentLayerId");
  Check(layers.To<Array>().size() >= 8, "legacy layer migration must provide the initial color palette without replacing existing layers");
  Check(!before.empty(), "legacy project must contain genuine saved objects");
  for (const auto &value : model.at("drawings").To<Array>()) {
    const auto id = value.To<Map>().at("id").To<Text>();
    Check(!Call(endpoint, "PlaneDrawing", "ReadGeometry", {{"id", id}}).at("curves").To<Array>().empty(),
          "prior saved drawing must load through the new geometry service");
  }
  runtime->SaveProjectFile(project->GetProjectID(), UTF8(resaved));
  Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()), "legacy project close failed");
  auto reopened = runtime->OpenProjectFile(UTF8(resaved));
  project = reopened->GetMainProject();diagnosticScene = &project->GetMainScene();
  endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  const auto restored = Model(endpoint);
  Check(identities(restored) == before && restored.at("layers") == layers && restored.at("currentLayerId") == current && bytes(source) == original,
        "legacy self-owned project must roundtrip without changing original identities, geometry or source file");
  Check(runtime->CloseProjectCatalog(reopened->GetCatalogID()), "resaved legacy project close failed");
  diagnosticScene = previousDiagnosticScene;
  std::cerr << "[phase] prior self-owned project copied/read/resaved/reopened with stable layer IDs and without modifying source passed: " << UTF8(source) << '\n' << std::flush;
}
