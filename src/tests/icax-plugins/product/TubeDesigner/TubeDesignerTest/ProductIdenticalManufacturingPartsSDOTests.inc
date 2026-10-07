TEST(ProductIdenticalManufacturingPartsSDO, WindowTemplatePreviewsAllDeclaredFacesWithoutChangingHostInputs)
{
    Scene scene;
    for (const auto* face : {"single", "two", "three", "five"}) {
        SCOPED_TRACE(face);
        const ObjectMap parameters{{"faceType", std::string(face)}, {"accessDoorEnabled", true},
            {"frameManufacturingMode", std::string("segment_weld")},
            {"horizontalEndConnection", std::string("tabs")}, {"verticalEndConnection", std::string("tabs")}};
        const auto unchanged = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters);
        const auto preview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("single-face-security-window")}, {"parameters", parameters}});
        ASSERT_FALSE(preview.at("items").To<VariantArray>().empty());
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters), unchanged);
        const auto normalized = preview.at("parameters").To<ObjectMap>();
        EXPECT_EQ(normalized.at("faceType").To<std::string>(), face);
        EXPECT_FALSE(normalized.contains("mergeIdenticalParts"));
        EXPECT_FALSE(normalized.contains("manufacturing.sourceStockIds"));
        auto request = normalized; request["templateId"] = std::string("single-face-security-window");
        const auto product = invoke(scene, "GeneratePreview", request).at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
        EXPECT_EQ(product.at("parameters"), preview.at("parameters"));
    }
}

TEST(ProductIdenticalManufacturingPartsSDO, RealStocksAggregateQuantitiesSourcesGeometryAndNestingAcrossReopen)
{
    Scene scene;
    const auto preview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"parameters", ObjectMap{{"faceType", std::string("single")}, {"width", 1200.0}, {"height", 1800.0},
            {"accessDoorEnabled", true}, {"frameManufacturingMode", std::string("segment_weld")},
            {"frameJoinType", std::string("miter_45")}, {"horizontalEndConnection", std::string("tabs")},
            {"verticalEndConnection", std::string("tabs")}}}});
    auto request = preview.at("parameters").To<ObjectMap>(); request["templateId"] = std::string("single-face-security-window");
    auto product = invoke(scene, "GeneratePreview", request).at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
    const auto run = disassemblyCountRun(scene, product);
    const auto displayDocument = run->GetNeutralModel();
    std::set<std::string> displayed;
    for (const auto& value : preview.at("items").To<VariantArray>()) displayed.insert(value.To<ObjectMap>().at("key").To<std::string>());
    const auto result = invoke(scene, "DisassembleSelected", disassemblyCountRequest(product)).at("tubeDesigner").To<ObjectMap>();
    product = result.at("product").To<ObjectMap>();
    const auto model = run->GetManufacturingModel();
    const auto extensions = model.at("extensions").To<ObjectMap>();
    const auto native = extensions.at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    const auto frozen = native.at("stocks").To<ObjectMap>();
    const auto stockCount = frozen.size();
    const auto rows = result.at("parts").To<VariantArray>();
    const auto sectionIdentities = [](const VariantArray& parts) {
        std::set<std::string> keys;
        for (const auto& value : parts) {
            const auto profile = value.To<ObjectMap>().at("profile").To<ObjectMap>();
            const auto key = profile.at("sectionIdentity").To<std::string>();
            EXPECT_FALSE(key.empty());
            EXPECT_NE(key.find("icax.nesting-section.v1"), std::string::npos);
            EXPECT_FALSE(profile.at("contours").To<VariantArray>().empty());
            keys.insert(key);
        }
        return keys;
    };
    ASSERT_EQ(stockCount, 41u);
    ASSERT_EQ(rows.size(), 20u);
    const auto originalSections = sectionIdentities(rows);
    EXPECT_EQ(originalSections.size(), 7u);
    ASSERT_EQ(run->GetPartCount(), rows.size());
    EXPECT_EQ(run->GetManufacturingParameters(), preview.at("parameters").To<ObjectMap>());
    EXPECT_EQ(run->GetNeutralModel(), displayDocument);
    EXPECT_EQ(native.at("plan").To<ObjectMap>().at("manufacturingParts").To<VariantArray>().size(), stockCount);
    std::set<std::string> stockCoverage, memberCoverage;
    std::size_t totalQuantity = 0, repeatedTypes = 0;
    VariantArray evidence;
    for (const auto& value : rows) {
        const auto row = value.To<ObjectMap>();
        const auto component = part(scene, row.at("entityId").To<std::string>());
        const auto properties = component->GetItemProperties();
        const auto sourceIDs = properties.at("manufacturing.sourceStockIds").To<VariantArray>();
        const auto sources = properties.at("manufacturing.sourceMembers").To<VariantArray>();
        EXPECT_EQ(component->GetQuantity(), sourceIDs.size());
        totalQuantity += component->GetQuantity();
        if (component->GetQuantity() > 1) ++repeatedTypes;
        const auto actual = shape(scene, component);
        ASSERT_TRUE(BRepCheck_Analyzer(actual).IsValid());
        std::size_t solidCount = 0; for (TopExp_Explorer e(actual, TopAbs_SOLID); e.More(); e.Next()) ++solidCount;
        EXPECT_EQ(solidCount, 1u);
        std::string identity;
        for (const auto& source : sourceIDs) {
            const auto id = source.To<std::string>(); EXPECT_TRUE(stockCoverage.insert(id).second);
            const auto stock = frozen.at(id).To<ObjectMap>();
            EXPECT_EQ(stock.at("itemKey").To<std::string>(), component->GetStableKey());
            const auto current = stock.at("manufacturingIdentity").To<std::string>();
            if (identity.empty()) identity = current; else EXPECT_EQ(identity, current);
            const auto geometry = stock.at("processedGeometry").To<ObjectMap>();
            const auto resource = scene.Resources().Get<BRepModel>(geometry.at("url").To<std::string>(), geometry.at("version").To<unsigned long long>());
            ASSERT_TRUE(resource);
            checkSameShape(actual, iCAX::OpenCascade::BuildOpenCascadeShape(*resource).Shape);
        }
        for (const auto& source : sources) EXPECT_TRUE(memberCoverage.insert(source.To<ObjectMap>().at("itemKey").To<std::string>()).second);
        evidence.emplace_back(ObjectMap{{"itemKey", component->GetStableKey()}, {"unitQuantity", component->GetQuantity()},
            {"sourceStockIds", sourceIDs}, {"sourceMembers", sources}});
    }
    EXPECT_EQ(totalQuantity, stockCount); EXPECT_EQ(stockCoverage.size(), stockCount);
    EXPECT_EQ(memberCoverage, displayed); EXPECT_GT(repeatedTypes, 0u);
    const auto bindings = invoke(scene, "GetProductAssemblyBindings", ObjectMap{
        {"productEntityId", product.at("entityId")}, {"generationRunId", product.at("activeGenerationRunId")}});
    std::set<std::string> shownOperations, declaredOperations;
    for (const auto& value : native.at("plan").To<ObjectMap>().at("operations").To<VariantArray>()) {
        const auto operation = value.To<ObjectMap>();
        if (frozen.contains(operation.at("stockId").To<std::string>())) declaredOperations.insert(operation.at("operationId").To<std::string>());
    }
    for (const auto& value : bindings.at("manufacturingPlan").To<ObjectMap>().at("stocks").To<VariantArray>()) {
        const auto stock = value.To<ObjectMap>(); std::set<std::string> ids;
        for (const auto& id : stock.at("sourceStockIds").To<VariantArray>()) ids.insert(id.To<std::string>());
        for (const auto& operation : stock.at("operations").To<VariantArray>()) {
            const auto row = operation.To<ObjectMap>(); EXPECT_TRUE(ids.contains(row.at("stockId").To<std::string>()));
            EXPECT_TRUE(shownOperations.insert(row.at("operationId").To<std::string>()).second);
        }
    }
    EXPECT_EQ(shownOperations, declaredOperations);
    disassemblyCountChecksMemberLinks(result);
    const auto originalIDs = disassemblyCountIdentities(result);
    const auto warm = invoke(scene, "DisassembleSelected", disassemblyCountRequest(product)).at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountIdentities(warm), originalIDs);
    EXPECT_EQ(sectionIdentities(warm.at("parts").To<VariantArray>()), originalSections);
    EXPECT_EQ(run->GetManufacturingModel(), model);
    auto wrongQuantity = model;
    auto& wrongItems = std::get<VariantArray>(wrongQuantity.at("items").m_Value);
    bool altered = false;
    for (auto& value : wrongItems) {
        auto& item = std::get<ObjectMap>(value.m_Value);
        auto& properties = std::get<ObjectMap>(item.at("properties").m_Value);
        const auto sources = properties.find("manufacturing.sourceStockIds");
        if (sources == properties.end() || sources->second.To<VariantArray>().size() <= 1) continue;
        properties["quantity"] = static_cast<unsigned long long>(sources->second.To<VariantArray>().size() + 1);
        altered = true; break;
    }
    ASSERT_TRUE(altered);
    ASSERT_TRUE(run->SetManufacturingModel(wrongQuantity));
    EXPECT_THROW(invoke(scene, "DisassembleSelected", disassemblyCountRequest(product)), std::exception);
    EXPECT_EQ(disassemblyCountIdentities(invoke(scene, "List", ObjectMap{}).at("tubeDesigner").To<ObjectMap>()), originalIDs);
    EXPECT_EQ(run->GetPartCount(), rows.size());
    EXPECT_EQ(run->GetManufacturingModel(), wrongQuantity);
    ASSERT_TRUE(run->SetManufacturingModel(model));
    const auto directory = std::filesystem::current_path() / "output/tests/identical-manufacturing-parts";
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "groups.json") << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
        {"types", static_cast<unsigned long long>(rows.size())}, {"totalQuantity", static_cast<unsigned long long>(totalQuantity)},
        {"stockCount", static_cast<unsigned long long>(stockCount)}, {"groups", evidence}});
    // The browser regression consumes the unmodified native profile snapshots,
    // rather than reconstructing section identities in its own fixture.
    std::ofstream(directory / "section-identities.json") << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
        {"parts", rows}, {"sectionCount", static_cast<unsigned long long>(originalSections.size())},
        {"totalQuantity", static_cast<unsigned long long>(totalQuantity)}});
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info; info.ProjectID=scene.project; info.MainSceneID=scene.GetSceneID(); info.ProjectName="identical-parts";
    const auto path=directory/"aggregated.ictd"; file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false); file.Open(path,reopened.Database(),reopened.Resources());
    const auto restored = invoke(reopened, "DisassembleSelected", disassemblyCountRequest(product)).at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountIdentities(restored), originalIDs);
    EXPECT_EQ(sectionIdentities(restored.at("parts").To<VariantArray>()), originalSections);
    invoke(reopened, "SetInstanceQuantity", ObjectMap{{"productEntityId", product.at("entityId")}, {"quantity", 3ull}});
    const auto multiplied = invoke(reopened, "List", ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(sectionIdentities(multiplied.at("parts").To<VariantArray>()), originalSections);
    VariantArray partIDs; unsigned long long productionTotal = 0;
    for (const auto& value : multiplied.at("parts").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        EXPECT_EQ(row.at("quantity").To<unsigned long long>(), 3 * row.at("unitQuantity").To<unsigned long long>());
        productionTotal += row.at("quantity").To<unsigned long long>(); partIDs.emplace_back(row.at("entityId"));
    }
    EXPECT_EQ(productionTotal, 3 * stockCount);
    const auto staged = invoke(reopened, "StageNestingParts", ObjectMap{{"partEntityIds", partIDs}});
    EXPECT_EQ(staged.at("partEntityIds").To<VariantArray>().size(), rows.size());
    // Stage returns identifiers/resource deltas, not the full profile catalog.
    // Read the actual saved nesting-owned rows for their section snapshots.
    const auto stagedSnapshot = invoke(reopened, "List", ObjectMap{});
    unsigned long long nestingQuantity = 0;
    VariantArray stagedRows;
    for (const auto& id : staged.at("partEntityIds").To<VariantArray>()) {
        const auto text = id.To<std::string>();
        nestingQuantity += part(reopened, text)->GetQuantity();
        stagedRows.emplace_back(nesting_profile_key_export_acceptance::partSnapshot(stagedSnapshot, text));
    }
    EXPECT_EQ(sectionIdentities(stagedRows), originalSections);
    EXPECT_EQ(nestingQuantity, productionTotal);
    std::cout << "[identical-manufacturing-parts] types=" << rows.size() << " quantity=" << totalQuantity
              << " stocks=" << stockCoverage.size() << " members=" << memberCoverage.size()
              << " sections=" << originalSections.size() << std::endl;
}
