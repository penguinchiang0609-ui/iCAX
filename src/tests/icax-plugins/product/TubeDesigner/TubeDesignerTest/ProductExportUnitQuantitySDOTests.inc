// Real product generation, disassembly, resource export and STEP readback.
namespace product_export_unit_quantity_tests {
struct SExportEvidence final {
    std::uint64_t UnitCount = 0;
    std::uint64_t ProductionCount = 0;
    VariantArray Files;
};

std::filesystem::path testDirectory(const std::string& label) {
    char* requested = nullptr; std::size_t length = 0;
    (void)_dupenv_s(&requested, &length, "ICAX_PRODUCT_EXPORT_TEST_OUTPUT");
    const auto root = requested && length > 1 ? assemblyBindingUtf8Path(requested)
        : std::filesystem::current_path() / "output/tests/product-export-unit-quantity";
    std::free(requested);
    const auto directory = root / (label + "-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    return directory;
}

std::string pathText(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

ObjectMap generateProduct(Scene& scene, const std::string& name = "export-unit-fixture") {
    return invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"instanceName", name}, {"instanceQuantity", 1ull},
        {"width", 1200.0}, {"height", 1800.0}, {"accessDoorEnabled", false},
        {"infillPattern", std::string("horizontal")}, {"horizontalMaximumCenterSpacing", 200.0},
        {"frameManufacturingMode", std::string("segment_weld")},
        {"frameJoinType", std::string("miter_45")},
        {"horizontalEndConnection", std::string("tabs")},
        {"verticalEndConnection", std::string("tabs")}
    }).at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
}

VariantArray currentParts(Scene& scene) {
    return invoke(scene, "List", ObjectMap{}).at("tubeDesigner").To<ObjectMap>().at("parts").To<VariantArray>();
}

VariantArray selectedParts(const VariantArray& parts) {
    VariantArray selected;
    ObjectMap repeated;
    for (const auto& value : parts) {
        const auto row = value.To<ObjectMap>();
        const auto count = row.at("unitQuantity").To<std::uint64_t>();
        if (count == 8) { repeated = row; break; }
        if (count == 4) repeated = row;
        if (repeated.empty() && count > 1) repeated = row;
    }
    if (repeated.empty()) throw std::runtime_error("Native product fixture must aggregate identical parts");
    selected.emplace_back(repeated);
    return selected;
}

std::string cellValue(const std::string& bytes, const std::string& reference, const bool numeric) {
    const auto cell = bytes.find("<c r=\"" + reference + "\"");
    if (cell == std::string::npos) throw std::runtime_error("Missing exported workbook cell: " + reference);
    const auto tag = numeric ? "<v>" : "<t";
    const auto startTag = bytes.find(tag, cell);
    const auto cellEnd = bytes.find("</c>", cell);
    if (startTag == std::string::npos || startTag >= cellEnd)
        throw std::runtime_error("Missing exported workbook cell value: " + reference);
    const auto start = bytes.find('>', startTag) + 1;
    const auto end = bytes.find(numeric ? "</v>" : "</t>", start);
    return bytes.substr(start, end - start);
}

SExportEvidence checkExport(Scene& scene, const ObjectMap& exported, const VariantArray& parts,
    const std::vector<ObjectMap>& reports, const std::filesystem::path& root,
    const bool checkGeometryIdentity = true) {
    SExportEvidence evidence;
    for (const auto& value : parts) {
        const auto row = value.To<ObjectMap>();
        evidence.UnitCount += row.at("unitQuantity").To<std::uint64_t>();
        evidence.ProductionCount += row.at("quantity").To<std::uint64_t>();
    }
    evidence.Files = exported.at("exportedFiles").To<VariantArray>();
    EXPECT_EQ(exported.at("exportedCount").To<std::uint64_t>(), evidence.UnitCount);
    EXPECT_EQ(evidence.Files.size(), evidence.UnitCount);
    std::set<std::string> uniquePaths;
    std::size_t physicalFiles = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.is_regular_file() && entry.path().extension() == ".step") ++physicalFiles;
    EXPECT_EQ(physicalFiles, evidence.UnitCount);
    for (const auto& file : evidence.Files) {
        const auto text = file.To<std::string>();
        EXPECT_TRUE(uniquePaths.insert(text).second);
        const auto readback = assemblyBindingReadStep(text);
        EXPECT_TRUE(BRepCheck_Analyzer(readback).IsValid());
        std::size_t solids = 0;
        for (TopExp_Explorer explorer(readback, TopAbs_SOLID); explorer.More(); explorer.Next()) ++solids;
        EXPECT_EQ(solids, 1u);
        if (checkGeometryIdentity) {
            const auto name = pathText(assemblyBindingUtf8Path(text).filename());
            bool matched = false;
            for (const auto& value : parts) {
                const auto row = value.To<ObjectMap>();
                const auto original = assemblyBindingUtf8Path(row.at("fileName").To<std::string>());
                const auto count = row.at("unitQuantity").To<std::uint64_t>();
                if ((count == 1 && name != pathText(original.filename()))
                    || (count > 1 && !name.starts_with(pathText(original.stem()) + "-件"))) continue;
                checkSameShape(assemblyBindingRowShape(scene, row), readback);
                matched = true; break;
            }
            EXPECT_TRUE(matched) << name;
        }
    }
    const auto workbook = assemblyBindingUtf8Path(exported.at("partListFile").To<std::string>());
    std::ifstream stream(workbook, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    EXPECT_FALSE(bytes.empty());
    std::uint64_t listedQuantity = 0;
    std::set<std::string> listedNames;
    std::size_t rowCount = 0;
    for (std::size_t row = 3; bytes.find("<c r=\"D" + std::to_string(row) + "\"") != std::string::npos; ++row) {
        const auto quantity = std::stoull(cellValue(bytes, "J" + std::to_string(row), true));
        listedQuantity += quantity;
        const auto partIndex = std::stoull(cellValue(bytes, "D" + std::to_string(row), true));
        const auto partNumber = cellValue(bytes, "E" + std::to_string(row), false);
        bool matched = false;
        for (const auto& value : parts) {
            const auto partRow = value.To<ObjectMap>();
            if (partRow.at("index").To<std::uint64_t>() != partIndex
                || partRow.at("partNumber").To<std::string>() != partNumber) continue;
            EXPECT_EQ(quantity, partRow.at("quantity").To<std::uint64_t>());
            matched = true; break;
        }
        EXPECT_TRUE(matched);
        std::stringstream names(cellValue(bytes, "K" + std::to_string(row), false));
        std::stringstream paths(cellValue(bytes, "L" + std::to_string(row), false));
        std::string name, path;
        std::size_t filesInRow = 0;
        while (std::getline(names, name)) {
            EXPECT_TRUE(listedNames.insert(name).second);
            EXPECT_TRUE(static_cast<bool>(std::getline(paths, path)));
            EXPECT_TRUE(std::filesystem::is_regular_file(root / assemblyBindingUtf8Path(path)));
            EXPECT_EQ(pathText(assemblyBindingUtf8Path(path).filename()), name);
            ++filesInRow;
        }
        for (const auto& value : parts) {
            const auto partRow = value.To<ObjectMap>();
            if (partRow.at("index").To<std::uint64_t>() == partIndex
                && partRow.at("partNumber").To<std::string>() == partNumber)
                EXPECT_EQ(filesInRow, partRow.at("unitQuantity").To<std::uint64_t>());
        }
        ++rowCount;
    }
    EXPECT_EQ(rowCount, parts.size());
    EXPECT_EQ(listedQuantity, evidence.ProductionCount);
    for (const auto& file : evidence.Files)
        EXPECT_TRUE(listedNames.contains(pathText(assemblyBindingUtf8Path(file.To<std::string>()).filename())));
    for (const auto& groupValue : exported.at("exportedGroups").To<VariantArray>()) {
        const auto group = groupValue.To<ObjectMap>();
        const auto directoryName = pathText(assemblyBindingUtf8Path(group.at("directory").To<std::string>()).filename());
        EXPECT_TRUE(directoryName.ends_with("×" + std::to_string(group.at("quantity").To<std::uint64_t>())));
    }
    EXPECT_FALSE(reports.empty());
    if (!reports.empty()) {
        EXPECT_EQ(reports.front().at("phase").To<std::string>(), "preparing");
        EXPECT_EQ(reports.front().at("total").To<std::uint64_t>(), evidence.UnitCount);
        EXPECT_EQ(reports.back().at("phase").To<std::string>(), "completed");
        EXPECT_EQ(reports.back().at("completed").To<std::uint64_t>(), evidence.UnitCount);
        EXPECT_EQ(reports.back().at("total").To<std::uint64_t>(), evidence.UnitCount);
    }
    VariantArray reportValues;
    for (const auto& report : reports) reportValues.emplace_back(report);
    std::ofstream(root / "evidence.json") << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
        {"unitQuantity", evidence.UnitCount}, {"productionQuantity", evidence.ProductionCount},
        {"exportedCount", exported.at("exportedCount")}, {"files", evidence.Files},
        {"partListFile", exported.at("partListFile")}, {"excelRows", static_cast<unsigned long long>(rowCount)},
        {"excelTotalQuantity", listedQuantity}, {"reports", reportValues}});
    return evidence;
}

ObjectMap exportParts(Scene& scene, const VariantArray& parts, const std::filesystem::path& directory,
    std::vector<ObjectMap>& reports, const std::string& method = "ExportSelected") {
    VariantArray ids;
    for (const auto& value : parts) ids.emplace_back(value.To<ObjectMap>().at("entityId"));
    return invoke(scene, method, ObjectMap{{"targetDirectory", pathText(directory)}, {"partEntityIds", ids}}, nullptr,
        [&](const auto& bytes) {
            reports.push_back(iCAX::Data::VariantSerializer::Deserialize(
                std::string(bytes.begin(), bytes.end())).To<ObjectMap>());
        });
}
}

TEST(ProductExportUnitQuantitySDO, PersistedSelectionKeepsSingleProductFileCountForOneAndThreeInstancesAfterReopen) {
    using namespace product_export_unit_quantity_tests;
    Scene scene(iCAX::Data::GenerateNewUUID(), iCAX::Data::GenerateNewUUID(), true, true);
    const auto product = generateProduct(scene);
    invoke(scene, "DisassembleSelected", disassemblyCountRequest(product));
    const auto selected = selectedParts(currentParts(scene));
    ASSERT_EQ(selected.size(), 1u);
    ASSERT_EQ(selected.front().To<ObjectMap>().at("unitQuantity").To<std::uint64_t>(), 8u);
    const auto root = testDirectory("persisted");
    std::vector<ObjectMap> firstReports;
    const auto first = exportParts(scene, selected, root / "quantity-one", firstReports);
    ASSERT_EQ(first.at("exportedCount").To<std::uint64_t>(), 8u);
    const auto firstEvidence = checkExport(scene, first, selected, firstReports, root / "quantity-one");
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project; info.MainSceneID=scene.GetSceneID(); info.ProjectName="Export unit count";
    const auto projectPath = root / "product.ictd";
    file.Save(projectPath, info, scene.Database(), scene.Resources());
    Scene reopened(scene.project, scene.GetSceneID(), false, true);
    file.Open(projectPath, reopened.Database(), reopened.Resources());
    invoke(reopened, "SetInstanceQuantity", ObjectMap{{"productEntityId", product.at("entityId")}, {"quantity", 3ull}});
    const auto selectedAfterReopen = selectedParts(currentParts(reopened));
    ASSERT_EQ(selectedAfterReopen.size(), 1u);
    std::vector<ObjectMap> secondReports;
    const auto second = exportParts(reopened, selectedAfterReopen, root / "quantity-three", secondReports, "ExportAll");
    ASSERT_EQ(second.at("exportedCount").To<std::uint64_t>(), 8u);
    const auto secondEvidence = checkExport(reopened, second, selectedAfterReopen, secondReports, root / "quantity-three");
    EXPECT_EQ(firstEvidence.UnitCount, secondEvidence.UnitCount);
    EXPECT_EQ(secondEvidence.ProductionCount, 3 * firstEvidence.ProductionCount);
    EXPECT_EQ(second.at("exportedGroups").To<VariantArray>().front().To<ObjectMap>().at("quantity").To<std::uint64_t>(), 3u);
}

TEST(ProductExportUnitQuantitySDO, LinkedTransientSelectionUsesTheSameCopyAndWorkbookContract) {
    using namespace product_export_unit_quantity_tests;
    Scene scene(iCAX::Data::GenerateNewUUID(), iCAX::Data::GenerateNewUUID(), true, true);
    const auto product = generateProduct(scene);
    invoke(scene, "DisassembleSelected", disassemblyCountRequest(product));
    invoke(scene, "SetInstanceQuantity", ObjectMap{{"productEntityId", product.at("entityId")}, {"quantity", 3ull}});
    const auto originalParts = currentParts(scene);
    VariantArray references;
    for (const auto& value : originalParts) {
        const auto row = value.To<ObjectMap>();
        references.emplace_back(ObjectMap{{"partEntityId", row.at("entityId")},
            {"productEntityId", product.at("entityId")}, {"generationRunId", product.at("activeGenerationRunId")},
            {"stableKey", row.at("stableKey")}, {"source", std::string("product")}});
    }
    // Exercise the existing runtime-only restoration path with real native
    // generation recipes and linked product references, without mocking SDOs.
    const auto rootComponent = std::dynamic_pointer_cast<iCAX::TubeDesigner::CTubeDesignerRootComponent>(
        scene.Database().GetMetaEntity()->GetComponent(iCAX::TubeDesigner::CTubeDesignerRootComponent::S_ClassName));
    ASSERT_TRUE(rootComponent);
    ASSERT_TRUE(rootComponent->SetNestingTask(ObjectMap{{"parts", references}}));
    auto& transaction = scene.Database().BeginTransaction("Keep linked product parts runtime-only for export acceptance");
    for (const auto& value : originalParts)
        transaction.DisposeEntity(iCAX::Data::uuid::from_string(value.To<ObjectMap>().at("entityId").To<std::string>()).value());
    std::string error;
    ASSERT_TRUE(scene.Database().CommitTransaction(transaction, error)) << error;
    const auto restored = invoke(scene, "List", ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
    VariantArray transientParts;
    for (const auto& value : restored.at("manufacturingGroups").To<VariantArray>()) {
        const auto group = value.To<ObjectMap>();
        if (group.at("productEntityId") == product.at("entityId")) {
            ASSERT_TRUE(group.at("transient").To<bool>());
            transientParts = group.at("parts").To<VariantArray>();
        }
    }
    ASSERT_FALSE(transientParts.empty());
    const auto selected = selectedParts(transientParts);
    ASSERT_EQ(selected.size(), 1u);
    ASSERT_EQ(selected.front().To<ObjectMap>().at("unitQuantity").To<std::uint64_t>(), 8u);
    const auto root = testDirectory("transient");
    std::vector<ObjectMap> reports;
    const auto exported = exportParts(scene, selected, root, reports);
    ASSERT_EQ(exported.at("exportedCount").To<std::uint64_t>(), 8u);
    checkExport(scene, exported, selected, reports, root);
}

TEST(ProductExportUnitQuantitySDO, OverridesAndCollidingTypeFileNamesPreserveTotalQuantityAndPhysicalFiles) {
    using namespace product_export_unit_quantity_tests;
    Scene scene(iCAX::Data::GenerateNewUUID(), iCAX::Data::GenerateNewUUID(), true, true);
    const auto product = generateProduct(scene);
    invoke(scene, "DisassembleSelected", disassemblyCountRequest(product));
    invoke(scene, "SetInstanceQuantity", ObjectMap{{"productEntityId", product.at("entityId")}, {"quantity", 3ull}});
    const auto allParts = currentParts(scene);
    auto selected = selectedParts(allParts);
    if (selected.size() == 1)
        for (const auto& value : allParts)
            if (value.To<ObjectMap>().at("entityId") != selected.front().To<ObjectMap>().at("entityId")) {
                selected.emplace_back(value); break;
            }
    for (const auto& value : selected) {
        const auto row = value.To<ObjectMap>();
        const auto component = part(scene, row.at("entityId").To<std::string>());
        ASSERT_TRUE(component);
        ASSERT_TRUE(component->SetFileName(row.at("unitQuantity").To<std::uint64_t>() == 1
            ? "same-type-name-件01.step" : "same-type-name.step"));
    }
    const auto repeated = part(scene, selected.front().To<ObjectMap>().at("entityId").To<std::string>());
    const auto count = repeated->GetQuantity();
    ASSERT_GT(count, 1u);
    ASSERT_TRUE(repeated->SetQuantityOverride(count * 3 - 1));
    VariantArray updated;
    for (const auto& value : currentParts(scene))
        for (const auto& chosen : selected)
            if (value.To<ObjectMap>().at("entityId") == chosen.To<ObjectMap>().at("entityId")) updated.emplace_back(value);
    const auto root = testDirectory("override-and-collision");
    std::vector<ObjectMap> reports;
    const auto exported = exportParts(scene, updated, root, reports);
    const auto evidence = checkExport(scene, exported, updated, reports, root, false);
    EXPECT_GT(evidence.UnitCount, 1u);
    const auto files = evidence.Files;
    EXPECT_TRUE(std::any_of(files.begin(), files.end(), [](const iCAX::Data::Variant& file) {
        return file.To<std::string>().find("-类型") != std::string::npos;
    }));
}

TEST(ProductExportUnitQuantitySDO, ProductNamesDifferingOnlyInCaseUseDistinctDirectories) {
    using namespace product_export_unit_quantity_tests;
    Scene scene(iCAX::Data::GenerateNewUUID(), iCAX::Data::GenerateNewUUID(), true, true);
    VariantArray selected;
    for (const auto* name : {"Export-Case", "export-case"}) {
        const auto product = generateProduct(scene, name);
        invoke(scene, "DisassembleSelected", disassemblyCountRequest(product));
        invoke(scene, "SetInstanceQuantity", ObjectMap{{"productEntityId", product.at("entityId")}, {"quantity", 3ull}});
        const auto rows = selectedParts(currentParts(scene));
        selected.emplace_back(rows.front());
    }
    const auto root = testDirectory("folder-case");
    std::vector<ObjectMap> reports;
    const auto exported = exportParts(scene, selected, root, reports);
    checkExport(scene, exported, selected, reports, root);
    const auto groups = exported.at("exportedGroups").To<VariantArray>();
    ASSERT_EQ(groups.size(), 2u);
    auto first = groups.front().To<ObjectMap>().at("directory").To<std::string>();
    auto second = groups.back().To<ObjectMap>().at("directory").To<std::string>();
    const auto lower = [](unsigned char character) { return static_cast<char>(std::tolower(character)); };
    std::transform(first.begin(), first.end(), first.begin(), lower);
    std::transform(second.begin(), second.end(), second.begin(), lower);
    EXPECT_NE(first, second);
}
