namespace product_manufacturing_concurrency_tests {
struct SWorkerOverride final {
    std::string Previous;
    bool Present = false;
    SWorkerOverride() {
        const auto length = GetEnvironmentVariableA("ICAX_ASSEMBLY_GEOMETRY_WORKERS", nullptr, 0);
        if (length) {
            Previous.resize(length);
            GetEnvironmentVariableA("ICAX_ASSEMBLY_GEOMETRY_WORKERS", Previous.data(), length);
            Previous.resize(length - 1); Present = true;
        }
    }
    ~SWorkerOverride() {
        SetEnvironmentVariableA("ICAX_ASSEMBLY_GEOMETRY_WORKERS", Present ? Previous.c_str() : nullptr);
    }
};

struct SStockSnapshot final {
    ObjectMap Stock;
    TopoDS_Shape Base, Processed;
};
struct SProductSnapshot final {
    ObjectMap Parameters, Plan;
    std::map<std::string, SStockSnapshot> Stocks;
};

SProductSnapshot generateSnapshot(const ObjectMap& parameters, const char* workers)
{
    SetEnvironmentVariableA("ICAX_ASSEMBLY_GEOMETRY_WORKERS", workers);
    Scene scene;
    const std::string templateID = "single-face-security-window";
    const auto preview = invoke(scene, "GenerateProductTemplatePreview",
        ObjectMap{{"templateId", templateID}, {"parameters", parameters}});
    auto generation = preview.at("parameters").To<ObjectMap>(); generation["templateId"] = templateID;
    const auto product = invoke(scene, "GeneratePreview", generation).at("tubeDesigner").To<ObjectMap>()
        .at("product").To<ObjectMap>();
    EXPECT_EQ(product.at("parameters"), preview.at("parameters"));
    invoke(scene, "DisassembleSelected", ObjectMap{{"productEntityIds", VariantArray{product.at("entityId")}}});
    const auto runID = iCAX::Data::uuid::from_string(product.at("activeGenerationRunId").To<std::string>()).value();
    const auto run = std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
        scene.Database().GetEntity(runID)->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName));
    if (!run) throw std::runtime_error("Actual manufacturing run missing");
    EXPECT_EQ(run->GetManufacturingParameters(), preview.at("parameters").To<ObjectMap>());
    const auto native = run->GetManufacturingModel().at("extensions").To<ObjectMap>()
        .at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    SProductSnapshot result;
    result.Parameters = run->GetManufacturingParameters();
    result.Plan = native.at("plan").To<ObjectMap>();
    std::map<std::string, TopoDS_Shape> resources;
    const auto resourceShape = [&](const ObjectMap& reference) {
        const auto url = reference.at("url").To<std::string>();
        if (const auto found = resources.find(url); found != resources.end()) return found->second;
        const auto brep = scene.Resources().Get<BRepModel>(url, reference.at("version").To<unsigned long long>());
        if (!brep) throw std::runtime_error("Actual frozen stock resource missing");
        const auto rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*brep);
        if (!rebuilt.bOK || rebuilt.Shape.IsNull()) throw std::runtime_error("Actual frozen stock cannot rebuild");
        resources.emplace(url, rebuilt.Shape); return rebuilt.Shape;
    };
    for (const auto& [id, value] : native.at("stocks").To<ObjectMap>()) {
        const auto stock = value.To<ObjectMap>();
        result.Stocks.emplace(id, SStockSnapshot{stock,
            resourceShape(stock.at("baseGeometry").To<ObjectMap>()),
            resourceShape(stock.at("processedGeometry").To<ObjectMap>())});
    }
    return result;
}

ObjectMap comparableRequest(ObjectMap request)
{
    auto features = request.at("features").To<VariantArray>();
    for (auto& value : features) {
        auto feature = value.To<ObjectMap>(); feature.erase("frozenCut"); value = std::move(feature);
    }
    request["features"] = std::move(features);
    auto ends = request.at("ends").To<ObjectMap>();
    for (auto& [key, value] : ends) {
        auto end = value.To<ObjectMap>(); end.erase("frozenCut"); value = std::move(end);
    }
    request["ends"] = std::move(ends); return request;
}

std::string featureCoverageKey(const std::string& resource, ObjectMap feature)
{
    feature.erase("id");
    if (feature.contains("frozenTool")) {
        auto frozen = feature.at("frozenTool").To<ObjectMap>();
        if (frozen.contains("instance")) {
            auto instance = frozen.at("instance").To<ObjectMap>(); instance.erase("id");
            frozen["instance"] = std::move(instance);
        }
        feature["frozenTool"] = std::move(frozen);
    }
    return resource + iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(feature);
}

void expectSameMaterial(const TopoDS_Shape& actual, const TopoDS_Shape& expected)
{
    checkSameShape(expected, actual); // Single solid, volume, area, centroid and bounds.
    BRepAlgoAPI_Common common;
    NCollection_List<TopoDS_Shape> arguments, tools;
    arguments.Append(expected); tools.Append(actual);
    common.SetArguments(arguments); common.SetTools(tools);
    common.SetNonDestructive(true); common.SetFuzzyValue(1e-7); common.Build();
    ASSERT_TRUE(common.IsDone()); ASSERT_FALSE(common.Shape().IsNull());
    const auto expectedVolume = volume(expected);
    EXPECT_NEAR(volume(common.Shape()), expectedVolume, std::max(1., expectedVolume) * 1e-6);
}

void expectSameProduct(const SProductSnapshot& actual, const SProductSnapshot& serial)
{
    EXPECT_EQ(actual.Parameters, serial.Parameters);
    ASSERT_EQ(actual.Stocks.size(), serial.Stocks.size());
    EXPECT_EQ(actual.Plan.at("manufacturingParts"), serial.Plan.at("manufacturingParts"));
    const auto actualContacts = actual.Plan.at("nativeContactChecks").To<VariantArray>();
    const auto serialContacts = serial.Plan.at("nativeContactChecks").To<VariantArray>();
    ASSERT_EQ(actualContacts.size(), serialContacts.size());
    for (std::size_t index = 0; index < actualContacts.size(); ++index) {
        const auto a = actualContacts[index].To<ObjectMap>(), s = serialContacts[index].To<ObjectMap>();
        EXPECT_EQ(a.at("instanceId"), s.at("instanceId")); EXPECT_EQ(a.at("status"), s.at("status"));
        EXPECT_NEAR(a.at("contactAreaMm2").To<double>(), s.at("contactAreaMm2").To<double>(), 1e-5);
        EXPECT_NEAR(a.at("intersectionVolumeMm3").To<double>(), s.at("intersectionVolumeMm3").To<double>(), 1e-7);
    }
    std::set<std::pair<std::string, std::string>> checkedResources;
    std::set<std::string> declaredFeatures, checkedFeatures;
    for (const auto& [id, expected] : serial.Stocks) {
        SCOPED_TRACE(id);
        const auto& found = actual.Stocks.at(id);
        EXPECT_EQ(found.Stock.at("matrix"), expected.Stock.at("matrix"));
        EXPECT_EQ(found.Stock.at("profile"), expected.Stock.at("profile"));
        EXPECT_EQ(found.Stock.at("itemKey"), expected.Stock.at("itemKey"));
        const auto request = comparableRequest(expected.Stock.at("request").To<ObjectMap>());
        EXPECT_EQ(comparableRequest(found.Stock.at("request").To<ObjectMap>()), request);
        const auto resource = expected.Stock.at("processedGeometry").To<ObjectMap>().at("url").To<std::string>();
        for (const auto& value : request.at("features").To<VariantArray>())
            declaredFeatures.insert(featureCoverageKey(resource, value.To<ObjectMap>()));
        const auto actualResource = found.Stock.at("processedGeometry").To<ObjectMap>().at("url").To<std::string>();
        if (!checkedResources.emplace(resource, actualResource).second) continue;
        expectSameMaterial(found.Base, expected.Base);
        expectSameMaterial(found.Processed, expected.Processed);
        // Check material at every real groove in its rotated cutter frame,
        // including retained lower wall and neighboring uncut material.
        const auto profile = expected.Stock.at("profile").To<ObjectMap>();
        const auto width = profile.at("width").To<double>(), depth = profile.at("depth").To<double>();
        const auto wall = profile.at("wallThickness").To<double>();
        for (const auto& value : request.at("features").To<VariantArray>()) {
            const auto feature = value.To<ObjectMap>();
            const auto station = feature.at("station").To<double>(), rotation = feature.at("rotation").To<double>();
            const auto halfDepth = std::abs(std::sin(rotation * 3.14159265358979323846 / 180)) > 0.5 ? width / 2 : depth / 2;
            for (const auto point : {std::array<double, 2>{station, halfDepth - wall / 2},
                    std::array<double, 2>{station, -halfDepth + wall * 0.2},
                    std::array<double, 2>{station + 30, halfDepth - wall / 2}})
                EXPECT_EQ(assemblyProcessPlanMaterial(found.Processed, point[0], 0, point[1], rotation),
                    assemblyProcessPlanMaterial(expected.Processed, point[0], 0, point[1], rotation));
            checkedFeatures.insert(featureCoverageKey(resource, feature));
        }
    }
    EXPECT_GT(checkedResources.size(), 20u);
    EXPECT_FALSE(declaredFeatures.empty()); EXPECT_EQ(checkedFeatures, declaredFeatures);
}

void runOracle(const std::string& tool, const std::string& bottom)
{
    SWorkerOverride restore;
    const ObjectMap parameters{{"faceType", std::string("five")},
        {"frameManufacturingMode", std::string("plane_v_notch")},
        {"outerFrameGrooveTool", tool}, {"outerFrameVGrooveBottomStrategy", bottom},
        {"outerFrameVGrooveRoundRadius", 2.0}};
    const auto serial = generateSnapshot(parameters, "1");
    ASSERT_EQ(serial.Stocks.size(), 82u);
    for (const auto workers : {"4", "8"}) {
        SCOPED_TRACE(workers);
        expectSameProduct(generateSnapshot(parameters, workers), serial);
    }
}
}

TEST(ProductManufacturingConcurrencySDO, FiveFaceRoundedOneFourEightWorkersKeepActualMaterialAndContacts)
{
    product_manufacturing_concurrency_tests::runOracle("system:v-notch-sharp", "rounded");
}

TEST(ProductManufacturingConcurrencySDO, FiveFaceEdgeArcOneFourEightWorkersKeepActualMaterialAndContacts)
{
    product_manufacturing_concurrency_tests::runOracle("system:edge-arc-groove", "sharp");
}
