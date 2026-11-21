// Native-only product acceptance. Production SDO calls keep their real scope;
// inspection adds persistence and solid checks only to this isolated test host.
#define main batchExcelScopeBridgeUnusedMain
#include "BatchExcelAutomationScopeBridge.cpp"
#undef main
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <map>

namespace {
ObjectMap InspectFinalSolids(punch_persistence_acceptance::Scene& scene, const ObjectMap& payload) {
    using namespace punch_persistence_acceptance;
    const auto snapshot = Invoke(scene, "scene", "List", {{"includeManufacturingGeometry", true}});
    auto rows = snapshot.at("tubeDesigner").To<ObjectMap>().at("parts").To<VariantArray>();
    if (payload.contains("entityIds")) {
        rows.clear();
        for (const auto& id : payload.at("entityIds").To<VariantArray>()) rows.emplace_back(ObjectMap{{"entityId", id}});
    }
    VariantArray checks;
    const auto points = payload.contains("points") ? payload.at("points").To<VariantArray>() : VariantArray{};
    for (const auto& value : rows) {
        const auto id = value.To<ObjectMap>().at("entityId").To<std::string>();
        const auto component = part(scene, id);
        const auto solid = shape(scene, component);
        const auto bytes = persistedShapeBytes(scene, component);
        unsigned long long solids = 0;
        for (TopExp_Explorer e(solid, TopAbs_SOLID); e.More(); e.Next()) ++solids;
        Bnd_Box bounds; BRepBndLib::AddOptimal(solid, bounds, false, false); bounds.SetGap(0);
        double x0, y0, z0, x1, y1, z1; bounds.Get(x0, y0, z0, x1, y1, z1);
        VariantArray pointChecks;
        BRepClass3d_SolidClassifier classifier(solid);
        for (const auto& pointValue : points) {
            const auto point = pointValue.To<ObjectMap>();
            if (point.at("entityId").To<std::string>() != id) continue;
            const auto xyz = point.at("point").To<VariantArray>();
            if (xyz.size() != 3) throw std::invalid_argument("Expected three point coordinates");
            classifier.Perform(gp_Pnt(finishedProductNumber(xyz[0]), finishedProductNumber(xyz[1]), finishedProductNumber(xyz[2])), 1e-6);
            pointChecks.emplace_back(ObjectMap{{"point", xyz}, {"inside", classifier.State() == TopAbs_IN},
                {"outside", classifier.State() == TopAbs_OUT}, {"boundary", classifier.State() == TopAbs_ON}});
        }
        checks.emplace_back(ObjectMap{{"entityId", id}, {"key", component->GetStableKey()},
            {"valid", BRepCheck_Analyzer(solid).IsValid()}, {"solids", solids}, {"volume", volume(solid)},
            {"bounds", VariantArray{x0, x1, y0, y1, z0, z1}},
            {"resourceBytes", static_cast<unsigned long long>(bytes.size())},
            {"resourceHash", std::to_string(std::hash<std::string>{}(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())))},
            {"pointChecks", pointChecks}});
    }
    return {{"geometryChecks", checks}};
}

struct FinishedStock { std::string key; TopoDS_Shape shape; Bnd_Box bounds; };

std::vector<FinishedStock> ReadFinishedStocks(punch_persistence_acceptance::Scene& scene, bool tubesOnly = false) {
    using namespace punch_persistence_acceptance;
    std::vector<FinishedStock> stocks;
    const auto snapshot = Invoke(scene, "scene", "List", {{"includeManufacturingGeometry", true}});
    const auto rows = snapshot.at("tubeDesigner").To<ObjectMap>().at("parts").To<VariantArray>();
    for (const auto& value : rows) {
        const auto row = value.To<ObjectMap>();
        const auto id = row.at("entityId").To<std::string>();
        const auto properties = row.at("properties").To<ObjectMap>();
        if (tubesOnly && properties.at("manufacturing.partKind").To<std::string>() != "tube") continue;
        const auto solid = shape(scene, part(scene, id));
        if (solid.IsNull() || !BRepCheck_Analyzer(solid).IsValid())
            throw std::runtime_error("Final stock is null or invalid: " + id);
        if (properties.at("manufacturing.sourceMembers").To<VariantArray>().empty())
            throw std::runtime_error("Final stock has no physical source members: " + id);
        for (const auto& sourceValue : properties.at("manufacturing.sourceMembers").To<VariantArray>()) {
            const auto source = sourceValue.To<ObjectMap>();
            const auto spans = source.at("spans").To<VariantArray>();
            if (spans.size() != 1) throw std::runtime_error("Assembly inspection expects one whole physical stock");
            const auto placement = spans[0].To<ObjectMap>().at("placement").To<ObjectMap>();
            const auto x = placement.at("xAxis").To<VariantArray>(), y = placement.at("yAxis").To<VariantArray>();
            const auto z = placement.at("zAxis").To<VariantArray>(), origin = placement.at("origin").To<VariantArray>();
            const auto n = [](const VariantArray& a, std::size_t i) { return finishedProductNumber(a[i]); };
            gp_Trsf worldToPart;
            worldToPart.SetValues(n(x,0),n(y,0),n(z,0),n(origin,0),
                n(x,1),n(y,1),n(z,1),n(origin,1),n(x,2),n(y,2),n(z,2),n(origin,2));
            FinishedStock stock{source.at("itemKey").To<std::string>(),
                BRepBuilderAPI_Transform(solid, worldToPart.Inverted(), true).Shape(), {}};
            BRepBndLib::AddOptimal(stock.shape, stock.bounds, false, false); stock.bounds.SetGap(0);
            stocks.push_back(std::move(stock));
        }
    }
    return stocks;
}

using TubeGeometryReference = std::map<std::string, TopoDS_Shape>;

TubeGeometryReference ReadTubeGeometry(punch_persistence_acceptance::Scene& scene) {
    TubeGeometryReference tubes;
    for (auto& stock : ReadFinishedStocks(scene, true)) {
        if (stock.key.empty() || !tubes.emplace(stock.key, stock.shape).second)
            throw std::runtime_error("Empty or duplicate physical tube source key: " + stock.key);
    }
    if (tubes.empty()) throw std::runtime_error("No finished physical tubes to inspect");
    return tubes;
}

double ExactDifferenceVolume(const TopoDS_Shape& first, const TopoDS_Shape& second, const std::string& key) {
    // Keep stored baseline topology immutable across both directions and later
    // scenes; no tessellation, fuzzy tolerance or serialized-byte comparison.
    NCollection_List<TopoDS_Shape> arguments, tools;
    arguments.Append(first); tools.Append(second);
    BRepAlgoAPI_Cut cut;
    cut.SetArguments(arguments); cut.SetTools(tools);
    cut.SetNonDestructive(true); cut.SetToFillHistory(false); cut.SetRunParallel(false); cut.Build();
    if (!cut.IsDone() || cut.HasErrors())
        throw std::runtime_error("Native tube geometry difference failed: " + key);
    const auto result = cut.Shape();
    if (result.IsNull()) return 0.;
    if (!BRepCheck_Analyzer(result).IsValid())
        throw std::runtime_error("Native tube geometry difference is invalid: " + key);
    double difference = 0.;
    for (TopExp_Explorer e(result, TopAbs_SOLID); e.More(); e.Next())
        difference += std::abs(punch_persistence_acceptance::volume(e.Current()));
    if (!std::isfinite(difference)) throw std::runtime_error("Nonfinite tube geometry difference: " + key);
    return difference;
}

ObjectMap CompareTubeGeometry(const std::string& reference, const TubeGeometryReference& baseline,
    const TubeGeometryReference& current) {
    using namespace punch_persistence_acceptance;
    for (const auto& [key, solid] : baseline)
        if (!current.contains(key)) throw std::runtime_error("Missing physical tube source key: " + key);
    for (const auto& [key, solid] : current)
        if (!baseline.contains(key)) throw std::runtime_error("Extra physical tube source key: " + key);
    VariantArray checks;
    double maximum = 0.;
    for (const auto& [key, before] : baseline) {
        const auto& after = current.at(key);
        const auto removed = ExactDifferenceVolume(before, after, key + " baseline-current");
        const auto added = ExactDifferenceVolume(after, before, key + " current-baseline");
        maximum = std::max(maximum, removed + added);
        checks.emplace_back(ObjectMap{{"key", key}, {"removedVolumeMm3", removed}, {"addedVolumeMm3", added}});
    }
    return {{"reference", reference}, {"equivalent", maximum <= 1.e-6},
        {"physicalTubeCount", static_cast<unsigned long long>(current.size())},
        {"maximumSymmetricDifferenceMm3", maximum}, {"positiveVolumeToleranceMm3", 1.e-6}, {"checks", checks}};
}

ObjectMap InspectAssemblyIntersections(punch_persistence_acceptance::Scene& scene, const ObjectMap& payload) {
    using namespace punch_persistence_acceptance;
    const auto tubeOnly = payload.contains("tubeOnly") && payload.at("tubeOnly").To<bool>();
    const auto stocks = ReadFinishedStocks(scene, tubeOnly);
    VariantArray candidates, collisions, stockGeometry;
    for (const auto& stock : stocks) {
        double x0, y0, z0, x1, y1, z1;
        stock.bounds.Get(x0, y0, z0, x1, y1, z1);
        unsigned long long solids = 0;
        for (TopExp_Explorer e(stock.shape, TopAbs_SOLID); e.More(); e.Next()) ++solids;
        stockGeometry.emplace_back(ObjectMap{{"key", stock.key}, {"valid", BRepCheck_Analyzer(stock.shape).IsValid()},
            {"solids", solids}, {"volume", volume(stock.shape)},
            {"bounds", VariantArray{x0, x1, y0, y1, z0, z1}}});
    }
    unsigned long long rejectedByBounds = 0;
    for (std::size_t i = 0; i < stocks.size(); ++i) for (std::size_t j = i + 1; j < stocks.size(); ++j) {
        const auto& a = stocks[i]; const auto& b = stocks[j];
        if (a.bounds.IsOut(b.bounds)) { ++rejectedByBounds; continue; }
        BRepAlgoAPI_Common common(a.shape, b.shape);
        if (!common.IsDone()) throw std::runtime_error("Native assembly intersection failed: " + a.key + " / " + b.key);
        const auto overlap = common.Shape().IsNull() ? 0. : std::abs(volume(common.Shape()));
        ObjectMap check{{"first", a.key}, {"second", b.key}, {"intersectionVolume", overlap}};
        candidates.emplace_back(check);
        if (overlap > 1e-6) collisions.emplace_back(check);
    }
    VariantArray contacts;
    if (payload.contains("contactPairs")) for (const auto& value : payload.at("contactPairs").To<VariantArray>()) {
        const auto pair = value.To<ObjectMap>();
        const auto first = pair.at("first").To<std::string>(), second = pair.at("second").To<std::string>();
        const auto find = [&](const std::string& key) -> const FinishedStock& {
            for (const auto& stock : stocks) if (stock.key == key) return stock;
            throw std::runtime_error("Missing contact stock: " + key);
        };
        BRepExtrema_DistShapeShape distance(find(first).shape, find(second).shape);
        if (!distance.IsDone() || distance.NbSolution() < 1) throw std::runtime_error("Native stock contact distance failed");
        contacts.emplace_back(ObjectMap{{"first", first}, {"second", second}, {"distanceMm", distance.Value()}});
    }
    return {{"scope", std::string(tubeOnly ? "actual-finished-tubes" : "actual-finished-stocks")},
        {"physicalStocks", static_cast<unsigned long long>(stocks.size())},
        {"totalPairs", static_cast<unsigned long long>(stocks.size()*(stocks.size()-1)/2)},
        {"rejectedByExactBounds", rejectedByBounds}, {"booleanCandidates", candidates},
        {"collisions", collisions}, {"positiveVolumeTolerance", 1e-6},
        {"stockGeometry", stockGeometry}, {"contacts", contacts}};
}
}

int main() {
    using namespace punch_persistence_acceptance;
    using iCAX::Data::Variant;
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    logInvocations = false;
    auto scene = std::make_unique<Scene>(iCAX::Data::GenerateNewUUID(), iCAX::Data::GenerateNewUUID(), true, true);
    // Own transformed BReps independently of the scene's resource lifetime.
    std::map<std::string, TubeGeometryReference> tubeGeometryReferences;
    std::string line;
    while (std::getline(std::cin, line)) {
        Variant id; ObjectMap response;
        try {
            const auto input = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(line).To<ObjectMap>();
            id = input.at("id");
            const auto method = input.at("method").To<std::string>();
            const auto scope = input.at("scope").To<std::string>();
            const auto payload = input.at("payload").To<ObjectMap>();
            ObjectMap result;
            if (scope == "inspection" && method == "GetRuntimeModules") {
                ObjectMap modules;
                for (const auto* name : {L"TubeDesigner.dll", L"TemplateRuntime.dll", L"Data.dll", L"OpenCascadeResourceImport.dll", L"ProjectFile.dll"}) {
                    wchar_t path[32768]{};
                    if (!GetModuleFileNameW(GetModuleHandleW(name), path, static_cast<DWORD>(std::size(path))))
                        throw std::runtime_error("Required module is not loaded");
                    const auto bytes = std::filesystem::path(path).u8string();
                    modules[std::filesystem::path(name).string()] = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                }
                result = {{"modules", modules}};
            } else if (scope == "inspection" && method == "ResetScene") {
                scene = std::make_unique<Scene>(iCAX::Data::GenerateNewUUID(), iCAX::Data::GenerateNewUUID(), true, true);
                result = {{"reset", true}};
            } else if (scope == "inspection" && method == "SaveAndReopen") {
                const auto target = payload.at("targetPath").To<std::string>();
                const auto path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(target.data()), target.size()));
                if (!path.is_absolute()) throw std::invalid_argument("Project path must be absolute");
                std::filesystem::create_directories(path.parent_path());
                iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
                    .ProductID="icax.tube-designer", .CurrentFormatVersion="0.1", .nCurrentFormatRevision=1});
                iCAX::ProjectFile::CProjectDocumentInfo info;
                info.ProjectID = scene->project; info.MainSceneID = scene->GetSceneID(); info.ProjectName = "five-face-native-acceptance";
                file.Save(path, info, scene->Database(), scene->Resources());
                auto reopened = std::make_unique<Scene>(scene->project, scene->GetSceneID(), false, true);
                file.Open(path, reopened->Database(), reopened->Resources());
                scene = std::move(reopened);
                result = Invoke(*scene, "scene", "List", {{"includeManufacturingGeometry", true}});
                result["savedAndReopened"] = true;
            } else if (scope == "inspection" && method == "InspectNativeGeometry") {
                result = InspectFinalSolids(*scene, payload);
            } else if (scope == "inspection" && method == "InspectAssemblyIntersections") {
                result = InspectAssemblyIntersections(*scene, payload);
            } else if (scope == "inspection" && method == "CaptureTubeGeometry") {
                const auto reference = payload.at("reference").To<std::string>();
                if (reference.empty() || tubeGeometryReferences.contains(reference))
                    throw std::invalid_argument("Tube geometry reference is empty or already captured: " + reference);
                auto tubes = ReadTubeGeometry(*scene);
                result = {{"reference", reference}, {"physicalTubeCount", static_cast<unsigned long long>(tubes.size())}};
                tubeGeometryReferences.emplace(reference, std::move(tubes));
            } else if (scope == "inspection" && method == "CompareTubeGeometry") {
                const auto reference = payload.at("reference").To<std::string>();
                const auto found = tubeGeometryReferences.find(reference);
                if (found == tubeGeometryReferences.end())
                    throw std::invalid_argument("Tube geometry reference was not captured: " + reference);
                const auto current = ReadTubeGeometry(*scene);
                if (payload.contains("sourceKeys")) {
                    TubeGeometryReference selectedBefore, selectedAfter;
                    const auto keys = payload.at("sourceKeys").To<VariantArray>();
                    if (keys.empty()) throw std::invalid_argument("Geometry comparison subset must not be empty");
                    for (const auto& value : keys) {
                        const auto key = value.To<std::string>();
                        if (!found->second.contains(key) || !current.contains(key))
                            throw std::runtime_error("Selected physical source is missing: " + key);
                        if (!selectedBefore.emplace(key, found->second.at(key)).second)
                            throw std::invalid_argument("Duplicate comparison source key: " + key);
                        selectedAfter.emplace(key, current.at(key));
                    }
                    result = CompareTubeGeometry(reference, selectedBefore, selectedAfter);
                    result["subset"] = true;
                } else result = CompareTubeGeometry(reference, found->second, current);
            } else if (scope == "inspection" && method == "InspectScriptResources") {
                const auto snapshot = Invoke(*scene, "scene", "List", {}).at("tubeDesigner").To<ObjectMap>();
                const auto runId = snapshot.at("product").To<ObjectMap>().at("activeGenerationRunId").To<std::string>();
                const auto entity = scene->Database().GetEntity(iCAX::Data::uuid::from_string(runId).value());
                const auto run = entity ? std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
                    entity->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName)) : nullptr;
                if (!run) throw std::runtime_error("No generated product");
                result = {{"generatedParameters", run->GetGeneratedParameters()}, {"manufacturingParameters", run->GetManufacturingParameters()},
                    {"display", run->GetNeutralModel()}, {"manufacturing", run->GetManufacturingModel()}};
            } else result = Invoke(*scene, scope, method, payload);
            response = {{"id", id}, {"ok", true}, {"result", result}};
        } catch (const std::exception& error) {
            response = {{"id", id}, {"ok", false}, {"error", std::string(error.what())}};
        }
        std::cout << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(response) << '\n' << std::flush;
    }
}
