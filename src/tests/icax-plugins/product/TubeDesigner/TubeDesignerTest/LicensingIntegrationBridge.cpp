// Isolated negative-authorization acceptance host. It exposes no enrollment,
// activation, trust mutation, or TPM operation. Production SDOs perform checks.
#include "pch.h"
#include "PunchPersistenceSDOTests.cpp"
#include <Product/ProductManifest.h>
#include <crtdbg.h>
#include <knownfolders.h>
#include <objbase.h>
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Uuid.lib")
#pragma comment(lib, "Product.lib")

namespace {
using namespace punch_persistence_acceptance;
using iCAX::Data::Variant;
bool configuredUnactivated = false;

class LicensingApplication final : public iCAX::Application::IApplicationContext {
public:
    LicensingApplication() {
        paths.InstallDirectory = std::filesystem::current_path().string();
        paths.UserDataDirectory = (std::filesystem::temp_directory_path()
            / ("icax-licensing-readonly-" + std::to_string(GetCurrentProcessId()))).string();
    }
    const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override { return descriptor; }
    const iCAX::Application::CApplicationPaths& GetPaths() const override { return paths; }
    iCAX::Data::PropertyBag GetSettings() const override { return {}; }
    const iCAX::Services::CServiceProvider& Services() const override { throw std::logic_error("No fixture application services"); }
private:
    iCAX::Application::CApplicationDescriptor descriptor;
    iCAX::Application::CApplicationPaths paths;
};

class LicensingProduct final : public iCAX::Product::IProductContext {
public:
    LicensingProduct() {
        definition = iCAX::Product::LoadProductManifest(
            (std::filesystem::current_path() / "apps/tube-designer/product.manifest.json").string()).Definition;
        std::vector<iCAX::Application::CUserDataFeatureDescriptor> features;
        for (const auto& sourceFeature : definition.UserDataFeatures) {
            iCAX::Application::CUserDataFeatureDescriptor feature;
            feature.FeatureID = sourceFeature.FeatureID;
            for (const auto& sourceType : sourceFeature.RecordTypes) {
                iCAX::Application::CUserDataRecordTypeDescriptor type;
                type.RecordType = sourceType.RecordType;
                type.SubjectTypes = sourceType.SubjectTypes;
                type.SchemaVersion = sourceType.SchemaVersion;
                type.AllowMultiple = sourceType.AllowMultiple;
                for (const auto& sourceRelation : sourceType.Relations)
                    type.Relations.push_back({sourceRelation.RelationType, sourceRelation.TargetKind,
                        sourceRelation.TargetFeatureID, sourceRelation.TargetType});
                feature.RecordTypes.push_back(std::move(type));
            }
            features.push_back(std::move(feature));
        }
        store = std::make_shared<iCAX::Application::CProductUserDataStore>(
            std::make_shared<iCAX::Application::CSqliteUserDataStore>(":memory:"), definition.ProductID, std::move(features));
    }
    const iCAX::Product::CProductDefinition& GetDefinition() const override { return definition; }
    const std::string& GetProductID() const override { return definition.ProductID; }
    std::shared_ptr<iCAX::Application::IProductUserDataStore> GetUserDataStore() const override { return store; }
    iCAX::Product::CProductData GetProductData() const override { return {}; }
    iCAX::Services::CServiceProvider& GetServiceProvider() const override { throw std::logic_error("No product services in authorization fixture"); }
    iCAX::Database::IMetaRegistry& GetMetaRegistry() const override { throw std::logic_error("No product registry in authorization fixture"); }
    iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override { throw std::logic_error("No behavior registry in authorization fixture"); }
    iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override { throw std::logic_error("No product loaders in authorization fixture"); }
    iCAX::Interaction::CSDORegistry& GetSDORegistry() const override { throw std::logic_error("Use DLL-registered SDOs"); }
private:
    iCAX::Product::CProductDefinition definition;
    std::shared_ptr<iCAX::Application::IProductUserDataStore> store;
};

std::string ModulePath(const wchar_t* name) {
    const auto module = GetModuleHandleW(name);
    if (!module) throw std::runtime_error("Required production DLL is not loaded");
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))))
        throw std::runtime_error("Cannot inspect production DLL path");
    return std::filesystem::path(path).string();
}

ObjectMap BuildPolicy() {
    const auto fn = reinterpret_cast<const char* (*)() noexcept>(
        GetProcAddress(GetModuleHandleW(L"TubeDesigner.dll"), "TubeDesignerLicenseBuildPolicy"));
    if (!fn || !fn()) throw std::runtime_error("Production licensing build policy export missing");
    return iCAX::TemplateRuntime::CStandardJsonCodec::Parse(std::string(fn())).To<ObjectMap>();
}

ObjectMap CheckExportLicenseDispatchOnly() {
    const auto binary = std::filesystem::current_path() / "TubeDesigner.dll";
    if (!LoadLibraryExW(binary.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
        throw std::runtime_error("Cannot load isolated Release TubeDesigner.dll (Windows error " + std::to_string(GetLastError()) + ")");
    const auto loaded = ModulePath(L"TubeDesigner.dll");
    if (!std::filesystem::equivalent(binary, loaded))
        throw std::runtime_error("Authorization dispatch loaded another TubeDesigner.dll");
    const auto policy = BuildPolicy();
    if (!policy.at("configured").To<bool>() || policy.at("developmentBypass").To<bool>()
        || !policy.at("authorizationEnforced").To<bool>())
        throw std::runtime_error("Export dispatch acceptance requires the configured enforced Release module");
    LicensingApplication application;
    iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayByModulePaths(registry, {loaded});
    const auto method = iCAX::Interaction::MakeSDOMethod("TubeDesignerLicensing", "ExportLicenseFile");
    const auto sdo = registry.Find(method.nSDOCode);
    if (!sdo || !sdo->HasMethod(method.nMethodCode))
        throw std::runtime_error("Missing production TubeDesignerLicensing.ExportLicenseFile registration");
    VariantArray results;
    const std::vector<std::pair<ObjectMap, std::string>> cases{
        {ObjectMap{}, "Missing path"}, {ObjectMap{{"path", std::string()}}, "Invalid path"}};
    for (const auto& [payload, expected] : cases) {
        iCAX::Interaction::CInvocation request;
        request.nCallID = results.size() + 1;
        request.Method = method;
        const auto bytes = iCAX::Data::VariantSerializer::Serialize(payload);
        request.Payload.assign(bytes.begin(), bytes.end());
        // Both requests must fail in InputPath, before license-store or TPM access.
        std::string actual;
        try {
            const auto response = sdo->Invoke(request, application, nullptr, nullptr, nullptr);
            if (!response.IsOK()) actual = response.strError;
        } catch (const std::exception& e) { actual = e.what(); }
        if (actual.find(expected) == std::string::npos)
            throw std::runtime_error("Unexpected ExportLicenseFile argument result: " + actual);
        results.emplace_back(ObjectMap{{"expected", expected}, {"actual", actual}, {"passed", true}});
    }
    return {{"module", loaded}, {"method", std::string("TubeDesignerLicensing.ExportLicenseFile")},
        {"registered", true}, {"cases", results}, {"licenseStoreUntouched", true}, {"hardwareUntouched", true}};
}

ObjectMap LicenseLocation() {
    const auto shell = LoadLibraryW(L"shell32.dll");
    if (!shell) throw std::runtime_error("Cannot load the system known-folder API");
    const auto knownFolder = reinterpret_cast<HRESULT(WINAPI*)(const GUID&, DWORD, HANDLE, PWSTR*)>(
        GetProcAddress(shell, "SHGetKnownFolderPath"));
    if (!knownFolder) throw std::runtime_error("Cannot resolve the system known-folder API");
    PWSTR raw = nullptr;
    const auto locationResult = knownFolder(FOLDERID_ProgramData, 0, nullptr, &raw);
    FreeLibrary(shell);
    if (FAILED(locationResult))
        throw std::runtime_error("Cannot locate actual ProgramData licensing directory");
    const auto directory = std::filesystem::path(raw) / L"TubeDesigner" / L"Licensing";
    CoTaskMemFree(raw);
    const auto certificate = directory / L"license.tdlic";
    return {{"directory", directory.string()}, {"certificate", certificate.string()},
        {"directoryExists", std::filesystem::exists(directory)},
        {"certificateExists", std::filesystem::exists(certificate)}};
}

void RequireExpectedRelease() {
    const auto policy = BuildPolicy();
    if (policy.at("configured").To<bool>() != configuredUnactivated || policy.at("developmentBypass").To<bool>()
        || !policy.at("authorizationEnforced").To<bool>())
        throw std::runtime_error("Acceptance requires an enforced Release build matching the explicitly selected trust mode; refusing to invoke licensing");
    if (configuredUnactivated && LicenseLocation().at("certificateExists").To<bool>())
        throw std::runtime_error("Configured-unactivated acceptance requires NO license.tdlic; refusing to invoke licensing or TPM");
}

ObjectMap InvokeReal(Scene& scene, LicensingProduct& product, const std::string& sdoName,
    const std::string& method, const ObjectMap& payload) {
    RequireExpectedRelease();
    static const std::set<std::pair<std::string, std::string>> allowed{
        {"TubeDesignerLicensing", "Status"}, {"TubeDesignerLicensing", "CheckAccess"},
        {"TubeDesigner", "List"}, {"TubeDesigner", "GetTemplateDescriptor"},
        {"TubeDesigner", "ListUserData"}, {"TubeDesigner", "ListSystemProfiles"},
        {"TubeDesigner", "ListComponentModels"}, {"TubeDesigner", "GetPunchTools"},
        {"TubeDesigner", "GetAssemblyTemplates"}, {"TubeDesigner", "GetAssemblyProcessPlans"},
        {"TubeDesigner", "GetPartDrawingTools"}, {"TubeDesigner", "GetBatchExcelAutomationSettings"},
        {"TubeDesigner", "GetProductAssemblyConnections"}, {"TubeDesigner", "GetProductAssemblyBindings"},
        {"TubeDesigner", "ReadNestingResult"}, {"TubeDesigner", "ListNestingPartFiles"},
        {"TubeDesigner", "GetPartDrawing"},
        {"TubeDesigner", "GeneratePreview"}, {"TubeDesigner", "Disassemble"},
        {"TubeDesigner", "AddNestingStandardPart"}, {"TubeDesigner", "AddNestingPunchPart"},
        {"TubeDesigner", "PreviewPunchWizard"}, {"TubeDesigner", "ApplyPunchWizard"},
        {"TubeDesigner", "Nest"}, {"TubeDesigner", "ExportNesting"},
        {"TubeDesigner", "ExportSelected"}, {"TubeDesigner", "ExportAll"},
        {"TubeDesigner", "ExportBatchExcelTemplate"}, {"TubeDesigner", "ExportComponentModel"},
        {"TubeDesigner", "ExportProductTemplatePackage"}, {"TubeDesigner", "ExportProfile"},
        {"TubeDesigner", "MachiningData"},
        {"Toolpath", "List"}, {"Toolpath", "RecognizeLoops"}, {"Toolpath", "AddSelectionPath"},
        {"Toolpath", "SetPoseField"}, {"Toolpath", "ClearProgram"},
        {"CADIntent", "Recognize"}, {"IntentToolpath", "CreateFromSelection"},
        {"Job", "Get"}, {"Job", "SetMachine"}, {"Machine", "List"},
        {"Workpiece", "Instantiate"}, {"WorkpieceEdit", "Commit"}, {"Selection", "Get"}
    };
    if (!allowed.contains({sdoName, method})) throw std::invalid_argument("Method is not allowed by isolated authorization fixture");
    LicensingApplication application;
    iCAX::Interaction::CSDORegistry registry;
    // Replay ONLY the real production DLLs, never an EXE replacement SDO.
    iCAX::Interaction::CSDORegistrationCatalog::ReplayByModulePaths(registry,
        {ModulePath(L"TubeDesigner.dll"), ModulePath(L"CamRuntime.dll")});
    iCAX::Interaction::CInvocation request;
    request.nCallID = 1;
    request.Method = iCAX::Interaction::MakeSDOMethod(sdoName, method);
    const auto bytes = iCAX::Data::VariantSerializer::Serialize(payload);
    request.Payload.assign(bytes.begin(), bytes.end());
    const auto sdo = registry.Find(request.Method.nSDOCode);
    if (!sdo || !sdo->HasMethod(request.Method.nMethodCode))
        throw std::runtime_error("Missing production SDO: " + sdoName + "." + method);
    const auto response = sdo->Invoke(request, application, &product, nullptr, &scene);
    if (!response.IsOK()) throw std::runtime_error(response.strError);
    return iCAX::Data::VariantSerializer::Deserialize(
        std::string(response.Payload.begin(), response.Payload.end())).To<ObjectMap>();
}

ObjectMap Snapshot(const Scene& scene) {
    VariantArray entities, resources;
    auto all = scene.Database().FilterEntities([](auto) { return true; });
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a->GetID() < b->GetID(); });
    for (const auto& entity : all) {
        ObjectMap components;
        for (const auto& name : entity->GetComponentClasses())
            components[name] = iCAX::Data::VariantSerializer::Serialize(
                entity->GetComponent(name)->GetProperties());
        entities.emplace_back(ObjectMap{{"id", iCAX::Data::to_string(entity->GetID())}, {"components", components}});
    }
    auto infos = scene.Resources().GetInfos();
    std::sort(infos.begin(), infos.end(), [](const auto& a, const auto& b) { return a.Key.Source < b.Key.Source; });
    for (const auto& info : infos)
        resources.emplace_back(ObjectMap{{"url", info.Key.Source}, {"version", info.nVersion},
            {"type", info.ResourceTypeID}, {"contentHash", info.ContentHash}});
    return {{"entityCount", static_cast<unsigned long long>(entities.size())},
        {"resourceCount", static_cast<unsigned long long>(resources.size())},
        {"entities", entities}, {"resources", resources},
        {"productId", std::string("icax.tube-designer")},
        {"projectId", iCAX::Data::to_string(scene.project)},
        {"sceneId", iCAX::Data::to_string(scene.GetSceneID())}};
}

void SetFixtureProperty(const std::shared_ptr<iCAX::Database::CComponentBase>& component,
    const std::string& name, const Variant& value) {
    std::string error;
    if (!component->SetProperty(name, value, error))
        throw std::runtime_error("Cannot seed export fixture field " + name + ": " + error);
}

ObjectMap SeedExportSources(Scene& scene) {
    if (scene.Database().FilterEntities([](auto e) {
        return e->HasComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName);
    }).size()) throw std::logic_error("Export sources may be seeded only once");
    const auto shape = BRepPrimAPI_MakeBox(120.0, 40.0, 20.0).Shape();
    auto geometry = std::make_shared<BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
        shape, "Authorization export fixture", "isolated-licensing-source", 0.001));
    if (!iCAX::OpenCascade::BuildOpenCascadeShape(*geometry).bOK)
        throw std::runtime_error("Export fixture BRep does not rebuild");
    const auto url = scene.Resources().MakeNamedResourceURL("licensing-acceptance/source");
    iCAX::Resource::CResourceInfo info;
    info.Name = "Authorization export fixture";
    scene.Resources().PutVersioned(url, geometry, info);
    const auto stored = scene.Resources().GetInfo(url);
    if (!stored || stored->nVersion == 0) throw std::runtime_error("Export fixture source was not registered with an immutable version");
    const auto productId = iCAX::Data::GenerateNewUUID();
    const auto generationId = iCAX::Data::GenerateNewUUID();
    auto product = scene.Database().CreateEntity(productId)->AddComponent<iCAX::TubeDesigner::CProductInstanceComponent>();
    SetFixtureProperty(product, "Name", std::string("Isolated authorization product"));
    SetFixtureProperty(product, "ActiveGenerationRunID", generationId);
    auto generation = scene.Database().CreateEntity(generationId)->AddComponent<iCAX::TubeDesigner::CGenerationRunComponent>();
    SetFixtureProperty(generation, "ProductID", productId);
    SetFixtureProperty(generation, "PartCount", 1ull);
    auto seed = [&](bool independent) {
        const auto id = iCAX::Data::GenerateNewUUID();
        auto part = scene.Database().CreateEntity(id)->AddComponent<CManufacturingPartComponent>();
        SetFixtureProperty(part, "Name", std::string(independent ? "Independent fixture" : "Product fixture"));
        SetFixtureProperty(part, "PartIndex", 1ull);
        SetFixtureProperty(part, "Length", 120.0);
        SetFixtureProperty(part, "ManufacturingGeometryResourceID", url);
        SetFixtureProperty(part, "ManufacturingGeometryResourceVersion", stored->nVersion);
        if (independent)
            SetFixtureProperty(part, "ItemProperties", ObjectMap{{"nesting.snapshot", ObjectMap{{"kind", std::string("standard")}}}});
        else {
            SetFixtureProperty(part, "ProductID", productId);
            SetFixtureProperty(part, "GenerationRunID", generationId);
        }
        return iCAX::Data::to_string(id);
    };
    const auto independentId = seed(true), productPartId = seed(false);
    return {{"independentPartId", independentId}, {"productPartId", productPartId},
        {"productId", iCAX::Data::to_string(productId)}, {"generationRunId", iCAX::Data::to_string(generationId)},
        {"sourceUrl", url}, {"sourceVersion", stored->nVersion},
        {"brepRebuildOK", true}, {"brepBytes", static_cast<unsigned long long>(iCAX::GeometryData::Persistence::Serialize(*geometry).size())}};
}

ObjectMap SeedLinkedProduct(Scene& scene, const ObjectMap& payload) {
    const auto productId = iCAX::Data::GenerateNewUUID();
    const auto generationId = iCAX::Data::GenerateNewUUID();
    auto product = scene.Database().CreateEntity(productId)->AddComponent<iCAX::TubeDesigner::CProductInstanceComponent>();
    SetFixtureProperty(product, "Name", std::string("Saved product browse fixture"));
    SetFixtureProperty(product, "TemplateID", payload.at("templateId"));
    SetFixtureProperty(product, "TemplateVersion", payload.at("templateVersion"));
    SetFixtureProperty(product, "Parameters", payload.at("parameters"));
    SetFixtureProperty(product, "ActiveGenerationRunID", generationId);
    auto generation = scene.Database().CreateEntity(generationId)->AddComponent<iCAX::TubeDesigner::CGenerationRunComponent>();
    SetFixtureProperty(generation, "ProductID", productId);
    // A linked entry whose transient manufacturing cache is unavailable must
    // never trigger PrepareProductDisassembly while browsing without a license.
    const ObjectMap reference{{"source", std::string("product")},
        {"productEntityId", iCAX::Data::to_string(productId)},
        {"generationRunId", iCAX::Data::to_string(generationId)},
        {"partEntityId", iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
        {"stableKey", std::string("missing-manufacturing-cache")}};
    auto root = scene.Database().GetMetaEntity()->GetComponent(iCAX::TubeDesigner::CTubeDesignerRootComponent::S_ClassName);
    SetFixtureProperty(root, "NestingTask", ObjectMap{{"parts", VariantArray{reference}}});
    return {{"productId", iCAX::Data::to_string(productId)}, {"generationRunId", iCAX::Data::to_string(generationId)}};
}
}

int main(int argc, char** argv) {
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    logInvocations = false;
    try {
        if (argc == 2 && std::string(argv[1]) == "--export-license-dispatch-only") {
            std::cout << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(CheckExportLicenseDispatchOnly()) << '\n';
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--configured-unactivated") configuredUnactivated = true;
        else if (argc != 1) throw std::invalid_argument("Only the explicit --configured-unactivated mode is supported");
        const auto cam = std::filesystem::current_path() / "CamRuntime.dll";
        if (!LoadLibraryExW(cam.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
            throw std::runtime_error("Cannot load current Release CamRuntime.dll (Windows error " + std::to_string(GetLastError()) + ")");
        Scene scene;
        LicensingProduct product;
        RequireExpectedRelease();
        for (std::string line; std::getline(std::cin, line);) {
            ObjectMap result;
            try {
                const auto request = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(line).To<ObjectMap>();
                result["id"] = request.at("id");
                const auto method = request.at("method").To<std::string>();
                const auto payload = request.contains("payload") ? request.at("payload").To<ObjectMap>() : ObjectMap{};
                if (method == "Inspect.Policy") result["result"] = BuildPolicy();
                else if (method == "Inspect.LicenseLocation") result["result"] = LicenseLocation();
                else if (method == "Inspect.Modules") result["result"] = ObjectMap{
                    {"TubeDesigner.dll", ModulePath(L"TubeDesigner.dll")}, {"CamRuntime.dll", ModulePath(L"CamRuntime.dll")},
                    {"TemplateRuntime.dll", ModulePath(L"TemplateRuntime.dll")}, {"Data.dll", ModulePath(L"Data.dll")}};
                else if (method == "Inspect.Scene") result["result"] = Snapshot(scene);
                else if (method == "Inspect.SeedExportSources") result["result"] = SeedExportSources(scene);
                else if (method == "Inspect.SeedLinkedProduct") result["result"] = SeedLinkedProduct(scene, payload);
                else {
                    const auto dot = method.find('.');
                    if (dot == std::string::npos) throw std::invalid_argument("An SDO-qualified method is required");
                    result["result"] = InvokeReal(scene, product, method.substr(0, dot), method.substr(dot + 1), payload);
                }
                result["ok"] = true;
            } catch (const std::exception& e) {
                result["ok"] = false;
                result["error"] = std::string(e.what());
            }
            std::cout << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(result) << '\n' << std::flush;
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
    return 0;
}
