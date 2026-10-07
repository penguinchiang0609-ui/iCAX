TEST(NestingPartFilePickerSDO, ListsOnlyCadFilesAndDirectoriesWithoutSceneChanges)
{
    Scene scene;
    const auto ids = scene.Database().GetEntityIDs();
    const auto undo = scene.Database().GetUndoArray();
    const auto root = std::filesystem::temp_directory_path()
        / ("icax-cad-picker-" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
    std::filesystem::create_directories(root / "z-folder");
    struct Cleanup { std::filesystem::path Path; ~Cleanup() { std::error_code error;
        std::filesystem::remove_all(Path, error); } } cleanup{root};
    for (const auto* name : {"b.STEP", "a.stp", "c.IGES", "d.igs", "e.txt", "f.brep"})
        std::ofstream(root / name) << "listing fixture";
    const auto result = invoke(scene, "ListNestingPartFiles", {{"directory", root.string()}});
    EXPECT_EQ(std::filesystem::canonical(root).string(), result.at("directory").To<std::string>());
    EXPECT_EQ(root.parent_path().string(), result.at("parentDirectory").To<std::string>());
    const auto entries = result.at("entries").To<VariantArray>();
    ASSERT_EQ(5u, entries.size());
    EXPECT_EQ("z-folder", entries.front().To<ObjectMap>().at("name").To<std::string>());
    EXPECT_TRUE(entries.front().To<ObjectMap>().at("directory").To<bool>());
    const std::array<std::string, 4> expected{"a.stp", "b.STEP", "c.IGES", "d.igs"};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto entry = entries.at(i + 1).To<ObjectMap>();
        EXPECT_EQ(expected[i], entry.at("name").To<std::string>());
        EXPECT_FALSE(entry.at("directory").To<bool>());
        EXPECT_EQ(std::filesystem::path(entry.at("path").To<std::string>()), root / expected[i]);
        EXPECT_GT(entry.at("sizeBytes").To<unsigned long long>(), 0u);
        EXPECT_FALSE(entry.at("lastModifiedNs").To<std::string>().empty());
    }
    const auto defaults = invoke(scene, "ListNestingPartFiles", {});
    EXPECT_EQ(std::filesystem::canonical(std::filesystem::current_path()).string(),
        defaults.at("directory").To<std::string>());
    EXPECT_THROW(invoke(scene, "ListNestingPartFiles", {{"directory", (root / "missing").string()}}),
        std::invalid_argument);
    EXPECT_EQ(ids, scene.Database().GetEntityIDs());
    EXPECT_EQ(undo, scene.Database().GetUndoArray());
    EXPECT_TRUE(scene.Resources().GetManifest(true).empty());
}

TEST(NestingPartFilePickerSDO, RealMachinedFilesPreviewWithBoundedResourcesAndNoProductWrites)
{
    Scene scene;
    auto& database = scene.Database();
    database.BeginLoadBaseline();
    const auto product = database.CreateEntity(iCAX::Data::GenerateNewUUID());
    product->AddComponent<iCAX::TubeDesigner::CProductInstanceComponent>();
    const auto part = database.CreateEntity(iCAX::Data::GenerateNewUUID());
    part->AddComponent<CManufacturingPartComponent>();
    database.EndLoadBaseline();
    const auto ids = database.GetEntityIDs();
    const auto undo = database.GetUndoArray();
    const auto countComponents = [&](const std::string& name) {
        std::size_t count = 0;
        for (const auto& id : database.GetEntityIDs())
            if (database.GetEntity(id)->HasComponent(name)) ++count;
        return count;
    };
    const auto products = countComponents(iCAX::TubeDesigner::CProductInstanceComponent::S_ClassName);
    const auto parts = countComponents(CManufacturingPartComponent::S_ClassName);
    const auto directory = std::filesystem::current_path() / "samples/manual-acceptance/machined-step";
    ASSERT_TRUE(std::filesystem::is_directory(directory));
    const std::array<std::string, 3> files{"01_am-2200.step", "04_REV-21-3558.step", "06_unistrut-P3100T10.step"};
    const std::string key = "native-file-picker";
    const auto brepURL = scene.Resources().MakeNamedResourceURL("tube-designer/nesting-file-preview/" + key);
    std::string geometryURL;
    unsigned long long previousVersion = 0;
    for (int round = 0; round < 2; ++round) {
        for (const auto& file : files) {
            const auto path = directory / file;
            const auto result = invoke(scene, "PreviewNestingPartFile", {{"sourcePath", path.string()}, {"previewKey", key}});
            EXPECT_EQ(file, result.at("sourceFileName").To<std::string>());
            const auto geometry = result.at("geometry").To<ObjectMap>();
            const auto url = geometry.at("url").To<std::string>();
            const auto version = geometry.at("version").To<unsigned long long>();
            EXPECT_GT(version, previousVersion);
            previousVersion = version;
            if (geometryURL.empty()) geometryURL = url;
            EXPECT_EQ(geometryURL, url);
            const auto mesh = scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(url, version);
            ASSERT_NE(nullptr, mesh);
            EXPECT_GT(mesh->Size(), 1000u);
            EXPECT_EQ("ICRG", std::string(reinterpret_cast<const char*>(mesh->Data()) + 4, 4));
            EXPECT_EQ(std::filesystem::canonical(path).string(), result.at("sourcePath").To<std::string>());
            EXPECT_EQ(std::filesystem::file_size(path), result.at("sizeBytes").To<unsigned long long>());
            EXPECT_FALSE(result.at("lastModifiedNs").To<std::string>().empty());
            const auto sourceMesh = scene.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(brepURL);
            ASSERT_NE(nullptr, sourceMesh);
            EXPECT_FALSE(sourceMesh->Mesh.Triangles.empty());
            EXPECT_FALSE(sourceMesh->Mesh.Vertices.empty());
            EXPECT_EQ(nullptr, scene.Resources().Get<BRepModel>(brepURL));
            Bnd_Box box;
            BRepBndLib::Add(iCAX::TubeDesigner::ReadManufacturingPartPreviewFile(path), box, false);
            box.SetGap(0);
            double x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            const auto bounds = result.at("bounds").To<ObjectMap>();
            const auto minimum = bounds.at("min").To<VariantArray>();
            const auto maximum = bounds.at("max").To<VariantArray>();
            const std::array<double, 3> expectedMin{x0, y0, z0}, expectedMax{x1, y1, z1};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                EXPECT_NEAR(expectedMin[axis], minimum.at(axis).To<double>(), 1e-6);
                EXPECT_NEAR(expectedMax[axis], maximum.at(axis).To<double>(), 1e-6);
            }
            EXPECT_EQ(2u, scene.Resources().GetManifest(true).size());
            EXPECT_EQ(1u, scene.Resources().GetVersions(brepURL).size());
            EXPECT_EQ(1u, scene.Resources().GetVersions(url).size());
            EXPECT_EQ(0u, scene.Resources().GetVersionStorageStats().nArchivedVersionCount);
            EXPECT_EQ(ids, database.GetEntityIDs());
            EXPECT_EQ(products, countComponents(iCAX::TubeDesigner::CProductInstanceComponent::S_ClassName));
            EXPECT_EQ(parts, countComponents(CManufacturingPartComponent::S_ClassName));
            EXPECT_EQ(undo, database.GetUndoArray());
        }
        const auto released = invoke(scene, "ReleaseNestingPartFilePreview", {{"previewKey", key}});
        EXPECT_TRUE(released.at("released").To<bool>());
        EXPECT_TRUE(scene.Resources().GetManifest(true).empty());
        EXPECT_TRUE(scene.Resources().GetVersions(brepURL).empty());
        EXPECT_TRUE(scene.Resources().GetVersions(geometryURL).empty());
        EXPECT_EQ(0u, scene.Resources().GetVersionStorageStats().nArchivedVersionCount);
        EXPECT_TRUE(invoke(scene, "ReleaseNestingPartFilePreview", {{"previewKey", key}}).at("released").To<bool>());
    }
    EXPECT_EQ(ids, database.GetEntityIDs());
    EXPECT_EQ(undo, database.GetUndoArray());
}

TEST(NestingPartFilePickerSDO, RejectsInvalidPreviewRequestsBeforeAllocatingResources)
{
    Scene scene;
    const auto ids = scene.Database().GetEntityIDs();
    const auto path = (std::filesystem::current_path() / "samples/manual-acceptance/machined-step/01_am-2200.step").string();
    for (const auto& key : {std::string(), std::string("../escape"), std::string("bad/key"), std::string(97, 'a')}) {
        EXPECT_THROW(invoke(scene, "PreviewNestingPartFile", {{"sourcePath", path}, {"previewKey", key}}), std::invalid_argument);
        EXPECT_THROW(invoke(scene, "ReleaseNestingPartFilePreview", {{"previewKey", key}}), std::invalid_argument);
    }
    EXPECT_THROW(invoke(scene, "PreviewNestingPartFile", {{"sourcePath", path + ".missing"},
        {"previewKey", std::string("invalid-file")}}), std::invalid_argument);
    const auto root = std::filesystem::temp_directory_path()
        / ("icax-cad-picker-invalid-" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path Path; ~Cleanup() { std::error_code error;
        std::filesystem::remove_all(Path, error); } } cleanup{root};
    std::ofstream(root / "unsupported.txt") << "not CAD";
    std::ofstream(root / "malformed.step") << "not STEP";
    std::ofstream(root / "empty.igs");
    { std::ofstream large(root / "oversized.stp", std::ios::binary);
        large.seekp(32u * 1024u * 1024u); large.put('x'); }
    for (const auto* file : {"unsupported.txt", "malformed.step", "empty.igs", "oversized.stp"})
        EXPECT_THROW(invoke(scene, "PreviewNestingPartFile", {{"sourcePath", (root / file).string()},
            {"previewKey", std::string("invalid-file")}}), std::invalid_argument);
    EXPECT_TRUE(scene.Resources().GetManifest(true).empty());
    EXPECT_EQ(ids, scene.Database().GetEntityIDs());
    EXPECT_FALSE(scene.Database().CanUndo());
}

TEST(NestingPartFilePickerSDO, PreviewsMultipleSolidsAndIgesWithoutImportingThem)
{
    Scene scene;
    const auto ids = scene.Database().GetEntityIDs();
    const auto root = std::filesystem::temp_directory_path()
        / ("icax-cad-picker-geometry-" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path Path; ~Cleanup() { std::error_code error;
        std::filesystem::remove_all(Path, error); } } cleanup{root};
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(100, 20, 30), 30, 12, 8).Shape());
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(200, 20, 30), 30, 12, 8).Shape());
    const auto step = root / "two-solids.step";
    STEPControl_Writer stepWriter;
    ASSERT_EQ(IFSelect_RetDone, stepWriter.Transfer(compound, STEPControl_AsIs));
    ASSERT_EQ(IFSelect_RetDone, stepWriter.Write(step.string().c_str()));
    const std::string key = "compound-file-preview";
    const auto result = invoke(scene, "PreviewNestingPartFile", {{"sourcePath", step.string()}, {"previewKey", key}});
    const auto brepURL = scene.Resources().MakeNamedResourceURL("tube-designer/nesting-file-preview/" + key);
    const auto sourceMesh = scene.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(brepURL);
    ASSERT_NE(nullptr, sourceMesh);
    EXPECT_EQ(24u, sourceMesh->Mesh.Triangles.size());
    bool firstSolid = false, secondSolid = false;
    for (const auto& vertex : sourceMesh->Mesh.Vertices) {
        if (vertex.X <= 130) firstSolid = true;
        if (vertex.X >= 200) secondSolid = true;
    }
    EXPECT_TRUE(firstSolid);
    EXPECT_TRUE(secondSolid);
    const auto stepWireframe = invoke(scene, "PreviewNestingPartFile", {{"sourcePath", step.string()},
        {"previewKey", key}, {"representation", std::string("wireframe")}}).at("wireframe").To<ObjectMap>();
    EXPECT_EQ(24u, stepWireframe.at("edgeCount").To<unsigned long long>());
    firstSolid = secondSolid = false;
    for (const auto& line : stepWireframe.at("polylines").To<VariantArray>())
    {
        const auto coordinates = line.To<VariantArray>();
        for (std::size_t i = 0; i < coordinates.size(); i += 3)
        {
            if (coordinates[i].To<double>() <= 130) firstSolid = true;
            if (coordinates[i].To<double>() >= 200) secondSolid = true;
        }
    }
    EXPECT_TRUE(firstSolid);
    EXPECT_TRUE(secondSolid);
    EXPECT_NEAR(100, result.at("bounds").To<ObjectMap>().at("min").To<VariantArray>().at(0).To<double>(), 1e-6);
    const auto iges = root / "single-solid.igs";
    IGESControl_Writer igesWriter("MM", 1);
    ASSERT_TRUE(igesWriter.AddShape(BRepPrimAPI_MakeBox(gp_Pnt(100, 20, 30), 30, 12, 8).Shape()));
    ASSERT_TRUE(igesWriter.Write(iges.string().c_str()));
    const auto igesResult = invoke(scene, "PreviewNestingPartFile", {{"sourcePath", iges.string()}, {"previewKey", key}});
    EXPECT_EQ("single-solid.igs", igesResult.at("sourceFileName").To<std::string>());
    EXPECT_NEAR(100, igesResult.at("bounds").To<ObjectMap>().at("min").To<VariantArray>().at(0).To<double>(), 1e-6);
    EXPECT_EQ(2u, scene.Resources().GetManifest(true).size());
    const auto igesWireframe = invoke(scene, "PreviewNestingPartFile", {{"sourcePath", iges.string()},
        {"previewKey", key}, {"representation", std::string("wireframe")}}).at("wireframe").To<ObjectMap>();
    EXPECT_EQ(12u, igesWireframe.at("edgeCount").To<unsigned long long>());
    EXPECT_EQ(2u, scene.Resources().GetManifest(true).size());
    invoke(scene, "ReleaseNestingPartFilePreview", {{"previewKey", key}});
    EXPECT_TRUE(scene.Resources().GetManifest(true).empty());
    EXPECT_EQ(ids, scene.Database().GetEntityIDs());
    EXPECT_FALSE(scene.Database().CanUndo());
}

TEST(NestingPartFilePickerSDO, SamePathFileChangeRefreshesGeometryAndIdentity)
{
    Scene scene;
    const auto ids = scene.Database().GetEntityIDs();
    const auto undo = scene.Database().GetUndoArray();
    const auto root = std::filesystem::temp_directory_path()
        / ("icax-cad-picker-update-" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path Path; ~Cleanup() { std::error_code error;
        std::filesystem::remove_all(Path, error); } } cleanup{root};
    const auto path = root / "changed.step";
    const auto write = [&](double width) {
        STEPControl_Writer writer;
        ASSERT_EQ(IFSelect_RetDone, writer.Transfer(BRepPrimAPI_MakeBox(width, 12, 8).Shape(), STEPControl_AsIs));
        ASSERT_EQ(IFSelect_RetDone, writer.Write(path.string().c_str()));
    };
    write(30);
    const ObjectMap payload{{"sourcePath", path.string()}, {"previewKey", std::string("same-path")}};
    const auto first = invoke(scene, "PreviewNestingPartFile", payload);
    const auto firstModified = std::filesystem::last_write_time(path);
    write(60);
    std::filesystem::last_write_time(path, firstModified + std::chrono::seconds(1));
    const auto second = invoke(scene, "PreviewNestingPartFile", payload);
    EXPECT_NE(first.at("lastModifiedNs").To<std::string>(), second.at("lastModifiedNs").To<std::string>());
    EXPECT_NEAR(30, first.at("bounds").To<ObjectMap>().at("max").To<VariantArray>().at(0).To<double>(), 1e-6);
    EXPECT_NEAR(60, second.at("bounds").To<ObjectMap>().at("max").To<VariantArray>().at(0).To<double>(), 1e-6);
    const auto firstGeometry = first.at("geometry").To<ObjectMap>(), secondGeometry = second.at("geometry").To<ObjectMap>();
    EXPECT_EQ(firstGeometry.at("url").To<std::string>(), secondGeometry.at("url").To<std::string>());
    EXPECT_GT(secondGeometry.at("version").To<unsigned long long>(), firstGeometry.at("version").To<unsigned long long>());
    EXPECT_EQ(2u, scene.Resources().GetManifest(true).size());
    EXPECT_EQ(0u, scene.Resources().GetVersionStorageStats().nArchivedVersionCount);
    EXPECT_EQ(ids, scene.Database().GetEntityIDs());
    EXPECT_EQ(undo, scene.Database().GetUndoArray());
    invoke(scene, "ReleaseNestingPartFilePreview", {{"previewKey", std::string("same-path")}});
    EXPECT_TRUE(scene.Resources().GetManifest(true).empty());
}

TEST(SolidResourceShapeValidation, ExactAndSerializedPathsShareTheSameSolidContract)
{
    const auto check = [](const TopoDS_Shape& shape, bool valid) {
        const auto direct = [&] { iCAX::OpenCascade::ValidateSolidResourceShape(shape, "test solid"); };
        const auto serialized = [&] {
            std::ostringstream stream;
            BRepTools::Write(shape, stream);
            (void)iCAX::TubeDesigner::ComponentModelShape({{"brep", stream.str()}});
        };
        if (valid) {
            EXPECT_NO_THROW(direct());
            EXPECT_NO_THROW(serialized());
        } else {
            EXPECT_THROW(direct(), std::invalid_argument);
            EXPECT_THROW(serialized(), std::invalid_argument);
        }
    };
    EXPECT_THROW(iCAX::OpenCascade::ValidateSolidResourceShape(TopoDS_Shape{}, "null"), std::invalid_argument);
    const auto box = BRepPrimAPI_MakeBox(30, 12, 8).Shape();
    check(box, true);
    check(box.Reversed(), false); // finite, but negative signed solid volume
    BRep_Builder builder;
    TopoDS_Compound multiple;
    builder.MakeCompound(multiple);
    builder.Add(multiple, box);
    builder.Add(multiple, BRepPrimAPI_MakeBox(gp_Pnt(100, 0, 0), 30, 12, 8).Shape());
    check(multiple, true);
    TopoDS_Compound empty;
    builder.MakeCompound(empty);
    check(empty, false);
    TopExp_Explorer faces(box, TopAbs_FACE);
    ASSERT_TRUE(faces.More());
    check(faces.Current(), false); // valid loose face is not a solid
    builder.Add(multiple, faces.Current());
    check(multiple, false); // a compound must contain only solids
    TopoDS_Solid zeroVolume;
    builder.MakeSolid(zeroVolume);
    check(zeroVolume, false);
    TopoDS_Shell openShell;
    builder.MakeShell(openShell);
    faces.Next();
    for (; faces.More(); faces.Next()) builder.Add(openShell, faces.Current());
    TopoDS_Solid openSolid;
    builder.MakeSolid(openSolid);
    builder.Add(openSolid, openShell);
    check(openSolid, false);
    auto deep = box;
    for (int depth = 0; depth < 66; ++depth) {
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        builder.Add(compound, deep);
        deep = compound;
    }
    check(deep, false);
}

TEST(NestingPartFilePickerPerformance, SevenRealMachinedFiles)
{
    char* labelValue = nullptr;
    std::size_t labelLength = 0;
    (void)_dupenv_s(&labelValue, &labelLength, "ICAX_NESTING_PREVIEW_BENCHMARK_LABEL");
    const std::string label = labelValue ? labelValue : "";
    std::free(labelValue);
    if (label.empty()) { std::cout << "Opt-in native preview performance capture" << std::endl; return; }
    const auto output = std::filesystem::current_path() / "output/tests/nesting-preview-performance-20261006";
    std::filesystem::create_directories(output / "native-mesh");
    Scene scene;
    const auto ids = scene.Database().GetEntityIDs();
    const auto undo = scene.Database().GetUndoArray();
    const std::array<std::string, 7> files{"01_am-2200.step", "02_am-5180.step", "03_am-5001-4700.step",
        "04_REV-21-3558.step", "05_REV-21-3572.step", "06_unistrut-P3100T10.step", "07_unistrut-P4000T10.step"};
    VariantArray captures;
    const bool oldLog = logInvocations;
    logInvocations = false;
    struct Restore { bool Value; ~Restore() { logInvocations = Value; } } restore{oldLog};
    const int rounds = label == "baseline-counts" ? 1 : 2;
    for (int round = 0; round < rounds; ++round) {
        for (const auto& file : files) {
            const auto path = std::filesystem::current_path() / "samples/manual-acceptance/machined-step" / file;
            const auto start = std::chrono::steady_clock::now();
            auto result = invoke(scene, "PreviewNestingPartFile", {{"sourcePath", path.string()}, {"previewKey", std::string("performance-capture")}});
            const auto totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            const auto geometry = result.at("geometry").To<ObjectMap>();
            const auto mesh = scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(
                geometry.at("url").To<std::string>(), geometry.at("version").To<unsigned long long>());
            ASSERT_NE(nullptr, mesh);
            ASSERT_TRUE(result.contains("profile"));
            result["filename"] = file;
            result["sourcePath"] = path.string();
            result["round"] = round == 0 ? std::string("process-first") : std::string("repeat");
            result["invocationMs"] = totalMs;
            const auto meshDirectory = label == "final" ? output / "native-mesh" : output / (label + "-mesh");
            result["meshFile"] = (meshDirectory / (path.stem().string() + ".icrg")).string();
            // Also records the original source's mesh counts when replayed with
            // the retained baseline DLL, so display precision can be compared.
            const auto sourceURL = scene.Resources().MakeNamedResourceURL("tube-designer/nesting-file-preview/performance-capture");
            std::size_t vertices = 0, triangles = 0;
            if (const auto brep = scene.Resources().Get<BRepModel>(sourceURL)) {
                for (const auto& triangulation : brep->Triangulations3) {
                    vertices += triangulation.Geometry.Vertices.size();
                    triangles += triangulation.Geometry.Triangles.size();
                }
            } else if (const auto sourceMesh = scene.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(sourceURL)) {
                vertices = sourceMesh->Mesh.Vertices.size();
                triangles = sourceMesh->Mesh.Triangles.size();
            }
            result["vertexCount"] = static_cast<unsigned long long>(vertices);
            result["triangleCount"] = static_cast<unsigned long long>(triangles);
            captures.emplace_back(result);
            std::cout << "[preview-benchmark] " << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(result) << std::endl;
            if (round == 0) {
                std::filesystem::create_directories(meshDirectory);
                std::ofstream stream(meshDirectory / (path.stem().string() + ".icrg"), std::ios::binary);
                stream.write(reinterpret_cast<const char*>(mesh->Data()), static_cast<std::streamsize>(mesh->Size()));
            }
            EXPECT_EQ(ids, scene.Database().GetEntityIDs());
            EXPECT_EQ(undo, scene.Database().GetUndoArray());
            EXPECT_EQ(2u, scene.Resources().GetManifest(true).size());
        }
    }
    std::ofstream stream(output / (label + ".json"));
    stream << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{{"measurements", captures},
        {"coldDefinition", std::string("First read per file in a fresh test process; OS disk cache not flushed.")}});
    invoke(scene, "ReleaseNestingPartFilePreview", {{"previewKey", std::string("performance-capture")}});
    EXPECT_TRUE(scene.Resources().GetManifest(true).empty());
}

TEST(NestingPartFilePickerPerformance, ExactPrecisionConcurrencyComparison)
{
    char* profileValue = nullptr;
    std::size_t profileLength = 0;
    (void)_dupenv_s(&profileValue, &profileLength, "ICAX_NESTING_PREVIEW_BENCHMARK_LABEL");
    const bool requested = profileValue && std::string(profileValue) == "tuning";
    std::free(profileValue);
    if (!requested) return;
    VariantArray captures;
    const auto ms = [](auto start) { return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count(); };
    for (const auto* file : {"03_am-5001-4700.step", "02_am-5180.step"}) {
        const auto path = std::filesystem::current_path() / "samples/manual-acceptance/machined-step" / file;
        const auto shape = iCAX::TubeDesigner::ReadManufacturingPartPreviewFile(path);
        std::size_t triangleCount = 0, vertexCount = 0;
        for (int round = 0; round < 2; ++round) {
            for (bool parallel : {false, true}) {
                const auto start = std::chrono::steady_clock::now();
                ASSERT_TRUE(BRepCheck_Analyzer(shape, true, parallel).IsValid());
                const auto elapsed = ms(start);
                ObjectMap capture{{"filename", std::string(file)}, {"stage", std::string("BRepCheck")},
                    {"parallel", parallel}, {"round", round}, {"milliseconds", elapsed}};
                captures.emplace_back(capture);
                std::cout << "[preview-tuning] " << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(capture) << std::endl;
            }
            for (std::size_t concurrency : {std::size_t(0), std::size_t(1)}) {
                const auto start = std::chrono::steady_clock::now();
                const auto meshes = iCAX::OpenCascade::ConvertOpenCascadeShapesToTriangleMeshes(
                    {{shape, file, "tuning"}}, 0.025, concurrency);
                const auto elapsed = ms(start);
                const auto& mesh = meshes.front().Mesh;
                if (triangleCount == 0) { triangleCount = mesh.Triangles.size(); vertexCount = mesh.Vertices.size(); }
                EXPECT_EQ(triangleCount, mesh.Triangles.size());
                EXPECT_EQ(vertexCount, mesh.Vertices.size());
                ObjectMap capture{{"filename", std::string(file)}, {"stage", std::string("mesh")},
                    {"concurrency", static_cast<unsigned long long>(concurrency)}, {"round", round}, {"milliseconds", elapsed},
                    {"vertexCount", static_cast<unsigned long long>(mesh.Vertices.size())},
                    {"triangleCount", static_cast<unsigned long long>(mesh.Triangles.size())}};
                captures.emplace_back(capture);
                std::cout << "[preview-tuning] " << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(capture) << std::endl;
            }
        }
    }
    std::ofstream stream(std::filesystem::current_path() / "output/tests/nesting-preview-performance-20261006/tuning.json");
    stream << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{{"measurements", captures}});
}

TEST(NestingPartFilePickerSDO, VendorWireframesPreserveEdgesWithoutMeshesOrSceneWrites)
{
    Scene scene;
    const auto ids = scene.Database().GetEntityIDs();
    const auto undo = scene.Database().GetUndoArray();
    VariantArray captures;
    const auto directory = std::filesystem::current_path() / "samples/manual-acceptance/machined-step";
    const std::array<std::string, 7> files{"01_am-2200.step", "02_am-5180.step", "03_am-5001-4700.step",
        "04_REV-21-3558.step", "05_REV-21-3572.step", "06_unistrut-P3100T10.step", "07_unistrut-P4000T10.step"};
    for (const auto& file : files)
    {
        const auto start = std::chrono::steady_clock::now();
        auto result = invoke(scene, "PreviewNestingPartFile", {{"sourcePath", (directory / file).string()},
            {"previewKey", std::string("svg-vendor-preview")}, {"representation", std::string("wireframe")}});
        const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ASSERT_FALSE(result.contains("geometry"));
        const auto wireframe = result.at("wireframe").To<ObjectMap>();
        const auto polylines = wireframe.at("polylines").To<VariantArray>();
        ASSERT_GT(polylines.size(), 12u) << file;
        EXPECT_EQ(polylines.size(), wireframe.at("edgeCount").To<unsigned long long>());
        const auto bounds = result.at("bounds").To<ObjectMap>();
        const auto min = bounds.at("min").To<VariantArray>(), max = bounds.at("max").To<VariantArray>();
        std::size_t pointCount = 0;
        for (const auto& line : polylines)
        {
            const auto coordinates = line.To<VariantArray>();
            ASSERT_GE(coordinates.size(), 6u);
            EXPECT_EQ(0u, coordinates.size() % 3);
            pointCount += coordinates.size() / 3;
            for (std::size_t i = 0; i < coordinates.size(); ++i)
            {
                const auto value = coordinates[i].To<double>();
                ASSERT_TRUE(std::isfinite(value));
                EXPECT_GE(value, min[i % 3].To<double>() - .01) << file;
                EXPECT_LE(value, max[i % 3].To<double>() + .01) << file;
            }
        }
        EXPECT_EQ(pointCount, wireframe.at("pointCount").To<unsigned long long>());
        EXPECT_TRUE(scene.Resources().GetManifest(true).empty());
        EXPECT_EQ(ids, scene.Database().GetEntityIDs());
        EXPECT_EQ(undo, scene.Database().GetUndoArray());
        std::cout << "[wireframe-preview] " << file << " " << milliseconds << " ms, edges " << polylines.size() << std::endl;
        result["filename"] = file;
        result["invocationMs"] = milliseconds;
        captures.emplace_back(std::move(result));
    }
    char* label = nullptr;
    std::size_t length = 0;
    (void)_dupenv_s(&label, &length, "ICAX_NESTING_PREVIEW_BENCHMARK_LABEL");
    const bool capture = label && std::string(label) == "wireframe20261007";
    std::free(label);
    if (capture)
    {
        const auto output = std::filesystem::current_path() / "output/tests/nesting-preview-blank-20261007";
        std::filesystem::create_directories(output);
        std::ofstream(output / "wireframe-native.json")
            << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{{"measurements", captures}});
    }
}

TEST(NestingPartFilePickerPerformance, PreviewStageComparison)
{
    char* label = nullptr;
    std::size_t length = 0;
    (void)_dupenv_s(&label, &length, "ICAX_NESTING_PREVIEW_BENCHMARK_LABEL");
    const bool requested = label && std::string(label) == "stages20261007";
    std::free(label);
    if (!requested) return;
    VariantArray captures;
    const auto timed = [&](const std::string& name, const auto& action) {
        const auto start = std::chrono::steady_clock::now();
        action();
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        captures.emplace_back(ObjectMap{{"stage", name}, {"milliseconds", elapsed}});
        std::cout << "[preview-stage] " << name << " " << elapsed << " ms" << std::endl;
    };
    TopoDS_Shape shape, copy;
    timed("read-and-validate", [&] {
        shape = iCAX::TubeDesigner::ReadManufacturingPartPreviewFile(
            std::filesystem::current_path() / "samples/manual-acceptance/machined-step/02_am-5180.step");
    });
    timed("topology-only-validation", [&] { EXPECT_TRUE(BRepCheck_Analyzer(shape, false).IsValid()); });
    timed("copy", [&] { copy = BRepBuilderAPI_Copy(shape, true, false).Shape(); });
    timed("mesh-owned-shape", [&] { BRepMesh_IncrementalMesh mesh(shape, 0.25, false, 0.5, false); });
    timed("mesh-copy", [&] { BRepMesh_IncrementalMesh mesh(copy, 0.25, false, 0.5, false); });
    STEPControl_Reader reader;
    reader.SetShapeProcessFlags({});
    timed("preview-parse", [&] { ASSERT_EQ(IFSelect_RetDone, reader.ReadFile(
        (std::filesystem::current_path() / "samples/manual-acceptance/machined-step/02_am-5180.step").string().c_str())); });
    timed("preview-transfer-no-healing", [&] { ASSERT_GT(reader.TransferRoots(), 0); });
    shape = reader.OneShape();
    timed("preview-mesh-1mm", [&] { BRepMesh_IncrementalMesh mesh(shape, 1.0, false, 1.0, false); });
    std::ofstream stream(std::filesystem::current_path() / "output/tests/nesting-preview-blank-20261007/stages.json");
    stream << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{{"measurements", captures}});
}
