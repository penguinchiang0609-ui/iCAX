// Real product/scene SDO bridge, with an isolated persistent user-data directory.
#include "pch.h"
#include "PunchPersistenceSDOTests.cpp"
#include <Product/ProductManifest.h>
#include <crtdbg.h>
#pragma comment(lib, "Product.lib")

namespace {
using iCAX::Data::ObjectMap;
std::string ResourceBase64(std::span<const uint8_t> bytes) {
    constexpr char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result; result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned n = (unsigned(bytes[i]) << 16) | (i + 1 < bytes.size() ? unsigned(bytes[i + 1]) << 8 : 0)
            | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        result += chars[(n >> 18) & 63]; result += chars[(n >> 12) & 63];
        result += i + 1 < bytes.size() ? chars[(n >> 6) & 63] : '=';
        result += i + 2 < bytes.size() ? chars[n & 63] : '=';
    }
    return result;
}
unsigned long long ResourceVersion(const ObjectMap& payload) {
    if (!payload.contains("version")) return 0;
    const auto& value = payload.at("version");
    if (value.Is<unsigned long long>()) return value.To<unsigned long long>();
    if (value.Is<long long>()) return static_cast<unsigned long long>(value.To<long long>());
    if (value.Is<int>()) return static_cast<unsigned long long>(value.To<int>());
    if (value.Is<unsigned int>()) return value.To<unsigned int>();
    return static_cast<unsigned long long>(value.To<double>());
}
ObjectMap ResourceInfo(const iCAX::Resource::CResourceLibrary& resources, const std::string& url) {
    const auto info = resources.GetInfo(url);
    if (!info) return {{"url", url}, {"exists", false}};
    iCAX::Data::VariantArray dependencies;
    for (const auto& dependency : info->Dependencies)
        dependencies.push_back(ObjectMap{{"url", dependency.URL}, {"version", dependency.nVersion}});
    return {{"url", url}, {"exists", true}, {"version", info->nVersion},
        {"type", info->ResourceTypeID}, {"identifier", info->FlatBufferIdentifier},
        {"runtimeOnly", info->IsRuntimeOnly()}, {"dependencies", dependencies}};
}
class AutomationApplication final : public iCAX::Application::IApplicationContext {
public:
    AutomationApplication() {
        paths.InstallDirectory = std::filesystem::current_path().string();
        wchar_t root[32768]{};
        if (!GetEnvironmentVariableW(L"ICAX_AUTOMATION_USER_DATA", root, static_cast<DWORD>(std::size(root))))
            throw std::runtime_error("Set ICAX_AUTOMATION_USER_DATA for isolated settings");
        const auto bytes = std::filesystem::path(root).u8string();
        paths.UserDataDirectory = {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
    const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override { return descriptor; }
    const iCAX::Application::CApplicationPaths& GetPaths() const override { return paths; }
    iCAX::Data::PropertyBag GetSettings() const override { return {}; }
    const iCAX::Services::CServiceProvider& Services() const override { throw std::logic_error("Fixture has no application services"); }
private:
    iCAX::Application::CApplicationDescriptor descriptor;
    iCAX::Application::CApplicationPaths paths;
};
class AutomationProduct final : public iCAX::Product::IProductContext {
public:
    AutomationProduct() {
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
    iCAX::Services::CServiceProvider& GetServiceProvider() const override { throw std::logic_error("Fixture has no product services"); }
    iCAX::Database::IMetaRegistry& GetMetaRegistry() const override { throw std::logic_error("Fixture has no product registry"); }
    iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override { throw std::logic_error("Fixture has no behaviors"); }
    iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override { throw std::logic_error("Fixture has no product loaders"); }
    iCAX::Interaction::CSDORegistry& GetSDORegistry() const override { throw std::logic_error("Use registered SDO"); }
private:
    iCAX::Product::CProductDefinition definition;
    std::shared_ptr<iCAX::Application::IProductUserDataStore> store;
};
ObjectMap Invoke(punch_persistence_acceptance::Scene& scene, const std::string& scope,
    const std::string& method, const ObjectMap& payload) {
    if (scope != "product" && scope != "scene") throw std::invalid_argument("Invalid SDO scope");
    AutomationApplication application;
    iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(registry);
    iCAX::Interaction::CInvocation request; request.nCallID = 1;
    request.Method = iCAX::Interaction::MakeSDOMethod("TubeDesigner", method);
    const auto bytes = iCAX::Data::VariantSerializer::Serialize(payload);
    request.Payload.assign(bytes.begin(), bytes.end());
    const auto sdo = registry.Find(request.Method.nSDOCode);
    if (!sdo || !sdo->HasMethod(request.Method.nMethodCode)) throw std::invalid_argument("Unknown SDO method");
    // The real desktop scene carries its owning product and user-data store.
    // Keep that same context here without granting a product-only call a scene.
    static AutomationProduct product;
    const auto response = sdo->Invoke(request, application, &product, nullptr, scope == "scene" ? &scene : nullptr);
    if (!response.IsOK()) throw std::runtime_error(response.strError);
    return iCAX::Data::VariantSerializer::Deserialize(std::string(response.Payload.begin(), response.Payload.end())).To<ObjectMap>();
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
                for (const auto* name : {L"TubeDesigner.dll", L"TemplateRuntime.dll", L"Data.dll", L"OpenCascadeResourceImport.dll"}) {
                    wchar_t path[32768]{};
                    const auto module = GetModuleHandleW(name);
                    if (!module || !GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)))) throw std::runtime_error("Native module is not loaded");
                    const auto bytes = std::filesystem::path(path).u8string();
                    modules[std::filesystem::path(name).string()] = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                }
                result = {{"modules", modules}};
            } else if (scope == "inspection" && method == "ResetScene") {
                scene = std::make_unique<Scene>(iCAX::Data::GenerateNewUUID(), iCAX::Data::GenerateNewUUID(), true, true);
                result = {{"reset", true}};
            } else if (scope == "inspection" && method == "ReadResource") {
                const auto url = payload.at("url").To<std::string>();
                const auto version = ResourceVersion(payload);
                const auto resource = scene->Resources().Get<iCAX::Resource::CFlatBufferResource>(url, version);
                if (!resource) throw std::runtime_error("Resource is not present in this isolated scene");
                result = {{"url", url}, {"version", version}, {"bytes", static_cast<unsigned long long>(resource->Size())},
                    {"base64", ResourceBase64(resource->Bytes())}};
            } else if (scope == "inspection" && method == "GetResourceInfo") {
                const auto url = payload.contains("url") ? payload.at("url").To<std::string>()
                    : scene->Resources().MakeNamedResourceURL(payload.at("namedKey").To<std::string>());
                result = ResourceInfo(scene->Resources(), url);
            } else if (scope == "inspection" && method == "DiscardResource") {
                // Only this isolated host exposes mutation probes. Production
                // product/scene SDOs retain their real scope and validation.
                const auto url = payload.at("url").To<std::string>();
                const auto mutation = scene->Resources().DiscardRuntimeResource(url);
                result = ResourceInfo(scene->Resources(), url);
                result["mutation"] = static_cast<int>(mutation);
            } else if (scope == "inspection" && method == "ClearResources") {
                // No persisted user scene is reachable from this process.
                // Preserve scene IDs while testing resource-pool recreation.
                scene->Resources().Clear();
                result = {{"cleared", true}};
            } else if (scope == "inspection" && method == "BumpResourceVersion") {
                const auto url = payload.at("url").To<std::string>();
                auto info = scene->Resources().GetInfo(url);
                if (!info || !info->IsRuntimeOnly()) throw std::runtime_error("Probe only replaces existing runtime resources");
                const auto previous = info->nVersion;
                info->ContentHash = "inspection-version-" + std::to_string(previous + 1);
                iCAX::Resource::CResourceInfo stored;
                iCAX::Resource::EResourceMutationResult mutation;
                if (const auto flatbuffer = scene->Resources().Get<iCAX::Resource::CFlatBufferResource>(url))
                    mutation = scene->Resources().PutVersioned(url,
                        std::make_shared<iCAX::Resource::CFlatBufferResource>(*flatbuffer), *info,
                        iCAX::Resource::EResourceVersionCondition::None, 0, &stored);
                else if (const auto model = scene->Resources().Get<BRepModel>(url))
                    mutation = scene->Resources().PutVersioned(url, std::make_shared<BRepModel>(*model), *info,
                        iCAX::Resource::EResourceVersionCondition::None, 0, &stored);
                else throw std::runtime_error("Probe resource is neither preview BRep nor flatbuffer");
                if (stored.nVersion <= previous) throw std::runtime_error("Probe did not allocate a newer immutable resource version");
                result = ResourceInfo(scene->Resources(), url);
                result["previousVersion"] = previous;
                result["mutation"] = static_cast<int>(mutation);
            } else result = Invoke(*scene, scope, method, payload);
            response = {{"id", id}, {"ok", true}, {"result", result}};
        } catch (const std::exception& error) {
            response = {{"id", id}, {"ok", false}, {"error", std::string(error.what())}};
        }
        std::cout << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(response) << '\n' << std::flush;
    }
}
