void CheckPlaneArtworkService(
    const std::shared_ptr<iCAX::Product::CProductRuntime> &runtime,
    const std::filesystem::path &fixtures) {
  auto catalog = runtime->OpenProjectCatalog("Neutral artwork service", {}, "Neutral artwork service", {});
  auto project = catalog->GetMainProject();
  auto endpoint = runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(), project->GetMainSceneID());
  const auto previousDiagnosticScene = diagnosticScene;diagnosticScene = &project->GetMainScene();
  Model(endpoint); // Let the independent scene finish normal initialization.
  const auto service = project->GetMainScene().Services().Resolve<iCAX::GeometryServices::IPlaneArtworkService>();
  const auto readBytes = [](const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);Check(static_cast<bool>(input), "artwork fixture unavailable");
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  };
  const auto nearArtwork = [](double a, double b) { return std::abs(a - b) <= 1e-9; };
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
        for (const auto &type : entity->GetComponentClasses()) result.Components[iCAX::Data::to_string(id)][type] = {
            entity->GetComponent(type)->GetProperties(), repository.GetComponentVersion(id, type)};
      }
      result.Undo = repository.GetUndoArray();result.Redo = repository.GetRedoArray();
    } catch (...) { scene.Start();throw; }
    scene.Start();return result;
  };
  const auto rejectsArtwork = [](auto action, const Text &code) {
    try { action(); } catch (const std::exception &error) {
      if (Text(error.what()).find(code) != Text::npos) return true;
      std::cerr << "[artwork rejection] expected " << code << ", actual " << error.what() << '\n' << std::flush;return false;
    }
    std::cerr << "[artwork rejection] expected " << code << ", actual no exception\n" << std::flush;return false;
  };
  const auto signedArea = [](const Array &points) {
    double area = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
      const auto a = points[i].To<Array>(), b = points[(i + 1) % points.size()].To<Array>();
      area += a[0].To<double>() * b[1].To<double>() - b[0].To<double>() * a[1].To<double>();
    }
    return area / 2.;
  };
  const auto netArea = [&](const Map &geometry) {
    double result = 0;for (const auto &value : geometry.at("curves").To<Array>()) result += signedArea(value.To<Map>().at("points").To<Array>());
    return std::abs(result);
  };
  const auto simpleRing = [](const Array &ring) {
    const auto cross = [](const Array &a, const Array &b, const Array &c) { return (b[0].To<double>() - a[0].To<double>()) * (c[1].To<double>() - a[1].To<double>()) - (b[1].To<double>() - a[1].To<double>()) * (c[0].To<double>() - a[0].To<double>()); };
    const auto contains = [](const Array &a, const Array &b, const Array &p) { return p[0].To<double>() >= std::min(a[0].To<double>(), b[0].To<double>()) - 1e-9 && p[0].To<double>() <= std::max(a[0].To<double>(), b[0].To<double>()) + 1e-9 && p[1].To<double>() >= std::min(a[1].To<double>(), b[1].To<double>()) - 1e-9 && p[1].To<double>() <= std::max(a[1].To<double>(), b[1].To<double>()) + 1e-9; };
    if (ring.size() < 3) return false;
    for (std::size_t i = 0; i < ring.size(); ++i) {
      const auto a = ring[i].To<Array>(), b = ring[(i + 1) % ring.size()].To<Array>();
      if (std::hypot(a[0].To<double>() - b[0].To<double>(), a[1].To<double>() - b[1].To<double>()) < 1e-9) return false;
      for (std::size_t j = i + 1; j < ring.size(); ++j) {
        if (j == i + 1 || (i == 0 && j == ring.size() - 1)) continue;
        const auto c = ring[j].To<Array>(), d = ring[(j + 1) % ring.size()].To<Array>();
        const double ac = cross(a, b, c), ad = cross(a, b, d), ca = cross(c, d, a), cb = cross(c, d, b);
        if ((ac * ad < 0 && ca * cb < 0) || (std::abs(ac) < 1e-9 && contains(a, b, c)) || (std::abs(ad) < 1e-9 && contains(a, b, d)) || (std::abs(ca) < 1e-9 && contains(c, d, a)) || (std::abs(cb) < 1e-9 && contains(c, d, b))) return false;
      }
    }
    return true;
  };
  const auto pngSignature = [](const std::vector<std::uint8_t> &bytes) {
    const std::vector<std::uint8_t> expected{137, 80, 78, 71, 13, 10, 26, 10};
    return bytes.size() > expected.size() && std::equal(expected.begin(), expected.end(), bytes.begin());
  };
  const auto before = snapshot();const auto resources = project->GetMainScene().Resources().GetManifest().size();
  const auto sourcePng = readBytes(fixtures / "image_rect_with_hole.png"), sourceBmp = readBytes(fixtures / "image_rect_with_hole.bmp");
  const Map traceOptions{{"widthMm", 16.}, {"heightMm", 12.}, {"threshold", 128.}, {"invert", false}, {"minAreaMm2", 0.}, {"simplifyMm", 0.},
                         {"id", Text("business-id")}, {"ownerId", Text("business-owner")}, {"async", true}};
  Map pngTrace;
  for (const auto *source : {&sourcePng, &sourceBmp}) {
    const auto normalized = service->NormalizeImage(*source);
    Check(pngSignature(normalized.EncodedPng) && normalized.PixelWidth == 16 && normalized.PixelHeight == 12 &&
              nearArtwork(normalized.WidthMm, 16. * 25.4 / 96.) && nearArtwork(normalized.HeightMm, 12. * 25.4 / 96.),
          "PNG/BMP image decoding must return actual pixel dimensions, encoded PNG and physical 96dpi dimensions");
    const auto traced = service->Vectorize(*source, traceOptions);const auto curves = traced.at("curves").To<Array>();
    Check(curves.size() == 2 && nearArtwork(traced.at("width").To<double>(), 12.) && nearArtwork(traced.at("height").To<double>(), 8.) &&
              nearArtwork(traced.at("approximationBoundMm").To<double>(), 0.) && nearArtwork(netArea(traced), 88.),
          "real image tracing must follow the black 12x8 body and 4x2 white hole, not its image rectangle or bounding box");
    std::vector<double> areas;std::vector<Array> rings;
    for (const auto &value : curves) {
      const auto curve = value.To<Map>();Check(curve.at("type").To<Text>() == "polyline" && curve.at("closed").To<bool>() && curve.at("points").To<Array>().size() == 4,
                                               "rectangular pixel boundaries must remain two closed exact four-corner contours");
      rings.push_back(curve.at("points").To<Array>());areas.push_back(signedArea(rings.back()));
    }
    Check(areas[0] * areas[1] < 0., "foreground outer and hole contours must have opposite directions");
    std::sort(areas.begin(), areas.end(), [](double a, double b) { return std::abs(a) < std::abs(b); });
    Check(nearArtwork(std::abs(areas[0]), 8.) && nearArtwork(std::abs(areas[1]), 96.), "image hole and body areas must match actual occupied pixels");
    const auto diagnostics = traced.at("traceDiagnostics").To<Map>();
    Check(diagnostics.at("pixelWidth").To<UInt>() == 16 && diagnostics.at("pixelHeight").To<UInt>() == 12 && diagnostics.at("contourCount").To<UInt>() == 2 &&
              !traced.contains("id") && !traced.contains("ownerId") && !traced.contains("async"),
          "trace diagnostics must describe decoded pixels while keeping business identity out of neutral output");
    Check(service->Vectorize(normalized.EncodedPng, traceOptions).at("curves") == traced.at("curves"), "normalized image pixels must preserve the same actual foreground boundaries");
    if (pngTrace.empty()) pngTrace = traced;else Check(traced.at("curves") == pngTrace.at("curves"), "top-down PNG and bottom-up BMP must decode into the same physical contours");
  }
  auto scaledOptions = traceOptions;scaledOptions["widthMm"] = 32.;scaledOptions["heightMm"] = 24.;
  const auto scaled = service->Vectorize(sourcePng, scaledOptions);
  Check(nearArtwork(scaled.at("width").To<double>(), 24.) && nearArtwork(scaled.at("height").To<double>(), 16.) && nearArtwork(netArea(scaled), 352.),
        "trace dimensions must scale real pixel-cell boundaries and their material area");
  auto invertedOptions = traceOptions;invertedOptions["invert"] = true;const auto inverted = service->Vectorize(sourcePng, invertedOptions);
  Check(inverted.at("curves").To<Array>().size() == 3 && nearArtwork(netArea(inverted), 104.), "invert must trace the actual 104 complementary white pixels, including its nested foreground island");
  const auto topologyPixels = readBytes(fixtures / "image_topology_regression.png");
  const auto originalTopology = service->Vectorize(topologyPixels, {{"widthMm", 16.}, {"heightMm", 16.}, {"simplifyMm", 0.}});
  const auto simplifiedTopology = service->Vectorize(topologyPixels, {{"widthMm", 16.}, {"heightMm", 16.}, {"simplifyMm", 2.}});
  Check(originalTopology.at("curves").To<Array>().size() == 1 && originalTopology.at("curves").To<Array>()[0].To<Map>().at("points").To<Array>().size() == 28 && nearArtwork(netArea(originalTopology), 68.),
        "the topology fixture must decode its actual 28-corner, 68-pixel foreground boundary");
  Check(simplifiedTopology.at("curves").To<Array>().size() == 1 && simpleRing(simplifiedTopology.at("curves").To<Array>()[0].To<Map>().at("points").To<Array>()) && nearArtwork(netArea(simplifiedTopology), 68.) &&
            simplifiedTopology.at("traceDiagnostics").To<Map>().at("topologyFallback").To<bool>(),
        "simplification must independently pass segment-intersection checks and preserve pixel area by falling back when RDP would create an internal crossing");
  const auto text = service->RasterizeText({{"text", Text("iPlaneCAM A8")}, {"fontFamily", Text("Arial")}, {"fontSizeMm", 8.}});
  Check(pngSignature(text.EncodedPng) && text.PixelWidth > 0 && text.PixelHeight > 0 && text.WidthMm > 0 && text.HeightMm > 0,
        "text rasterization must create nonempty actual glyph pixels and physical dimensions");
  const auto normalizedText = service->NormalizeImage(text.EncodedPng);
  Check(normalizedText.PixelWidth == text.PixelWidth && normalizedText.PixelHeight == text.PixelHeight, "rendered text must be a real decodable PNG, not an opaque placeholder");
  const auto textContour = service->Vectorize(text.EncodedPng, {{"widthMm", text.WidthMm}, {"heightMm", text.HeightMm}, {"threshold", 128.}, {"invert", true}, {"simplifyMm", 0.}});
  Check(!textContour.at("curves").To<Array>().empty() && textContour.at("width").To<double>() < text.WidthMm && textContour.at("height").To<double>() < text.HeightMm &&
            netArea(textContour) > 0. && netArea(textContour) < text.WidthMm * text.HeightMm,
        "glyph tracing must honor text alpha and ink boundaries rather than filling its raster rectangle");
  const auto largerText = service->RasterizeText({{"text", Text("iPlaneCAM A8")}, {"fontFamily", Text("Arial")}, {"fontSizeMm", 16.}});
  Check(largerText.WidthMm > text.WidthMm && largerText.HeightMm > text.HeightMm, "editable font size must affect actual text raster and physical dimensions");
  Check(rejectsArtwork([&] { service->NormalizeImage(std::vector<std::uint8_t>{1, 2, 3, 4}); }, "PLANE_ARTWORK_") &&
            rejectsArtwork([&] { service->RasterizeText({{"text", Text("")}, {"fontSizeMm", 8.}}); }, "PLANE_ARTWORK_INVALID_TEXT") &&
            rejectsArtwork([&] { auto options = traceOptions;options["threshold"] = -1.;service->Vectorize(sourcePng, options); }, "PLANE_ARTWORK_INVALID_TRACE_OPTIONS"),
        "invalid image/text/trace inputs must be explicitly rejected");
  int cancellationChecks = 0;
  Check(rejectsArtwork([&] { service->Vectorize(sourcePng, traceOptions, [&] { return ++cancellationChecks > 4; }); }, "PLANE_GEOMETRY_CANCELLED") && cancellationChecks > 4,
        "real image tracing must honor cancellation during pixel processing, not only before entering its algorithm");
  Check(snapshot() == before && project->GetMainScene().Resources().GetManifest().size() == resources,
        "neutral image/font/trace computations, failures and cancellation must leave all EC properties/versions, resources and undo/redo unchanged");
  Check(runtime->CloseProjectCatalog(catalog->GetCatalogID()), "neutral artwork test catalog close failed");diagnosticScene = previousDiagnosticScene;
  std::cerr << "[phase] neutral artwork PNG/BMP pixels/holes/invert/scale/font/alpha/cancellation/side-effects passed\n" << std::flush;
}
