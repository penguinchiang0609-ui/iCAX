// Test-only stdio adapter: real SDO scopes, resources and project persistence.
#include "pch.h"
#include "PunchPersistenceSDOTests.cpp"
#include "OpenCascadeResourceImport/CadDocumentImportService.h"
#include "Services/ServiceRegistrationCatalog.h"
#include "Services/ServiceProvider.h"
#include "Product/ProductManifest.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <crtdbg.h>
#pragma comment(lib, "Product.lib")

namespace {
using namespace punch_persistence_acceptance;
using iCAX::Data::Variant;
class Product final : public iCAX::Product::IProductContext {
public:
    Product() {
        definition = iCAX::Product::LoadProductManifest((std::filesystem::current_path()
            / "src/apps/tube-designer/product.manifest.json").string()).Definition;
        std::vector<iCAX::Application::CUserDataFeatureDescriptor> descriptors;
        for (const auto& sourceFeature : definition.UserDataFeatures) {
            iCAX::Application::CUserDataFeatureDescriptor feature;
            feature.FeatureID = sourceFeature.FeatureID;
            for (const auto& sourceType : sourceFeature.RecordTypes) {
                iCAX::Application::CUserDataRecordTypeDescriptor type;
                type.RecordType = sourceType.RecordType;
                type.SubjectTypes = sourceType.SubjectTypes;
                type.SchemaVersion = sourceType.SchemaVersion;
                type.AllowMultiple = sourceType.AllowMultiple;
                for (const auto& relation : sourceType.Relations)
                    type.Relations.push_back({relation.RelationType, relation.TargetKind,
                        relation.TargetFeatureID, relation.TargetType});
                feature.RecordTypes.emplace_back(std::move(type));
            }
            descriptors.emplace_back(std::move(feature));
        }
        store = std::make_shared<iCAX::Application::CProductUserDataStore>(
            std::make_shared<iCAX::Application::CSqliteUserDataStore>(":memory:"), definition.ProductID,
            std::move(descriptors));
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
    }
    const iCAX::Product::CProductDefinition& GetDefinition() const override { return definition; }
    const std::string& GetProductID() const override { return definition.ProductID; }
    std::shared_ptr<iCAX::Application::IProductUserDataStore> GetUserDataStore() const override { return store; }
    iCAX::Product::CProductData GetProductData() const override { return {}; }
    iCAX::Services::CServiceProvider& GetServiceProvider() const override { return services; }
    iCAX::Database::IMetaRegistry& GetMetaRegistry() const override { throw std::logic_error("No test product registry"); }
    iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override { throw std::logic_error("No test behaviors"); }
    iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override { throw std::logic_error("No test resource loaders"); }
    iCAX::Interaction::CSDORegistry& GetSDORegistry() const override { throw std::logic_error("Use registered SDO"); }
private:
    iCAX::Product::CProductDefinition definition;
    std::shared_ptr<iCAX::Application::IProductUserDataStore> store;
    mutable iCAX::Services::CServiceProvider services;
};
ObjectMap cadDocument(const iCAX::CAD::SCadExchangeDocument& loaded) {
    const auto groups = [](const std::vector<iCAX::CAD::SCadGroup>& values) {
        iCAX::Data::VariantArray result;
        for (const auto& group : values) result.emplace_back(iCAX::Data::VariantArray{group.Code, group.Value});
        return result;
    };
    ObjectMap header, sections;
    for (const auto& [name, values] : loaded.Document.Header) header[name] = groups(values);
    for (const auto& [name, records] : loaded.Document.Sections) {
        iCAX::Data::VariantArray result;
        for (const auto& record : records) result.emplace_back(ObjectMap{{"type", record.Type}, {"values", groups(record.Values)}});
        sections[name] = std::move(result);
    }
    const auto path = loaded.SourcePath.u8string();
    return {{"sourcePath", std::string(path.begin(), path.end())}, {"format", loaded.SourceFormat},
        {"sourceVersion", loaded.SourceVersion}, {"converterVersion", loaded.ConverterVersion},
        {"document", ObjectMap{{"header", std::move(header)}, {"sections", std::move(sections)}, {"sourceFormat", loaded.Document.SourceFormat}}}};
}
std::string text(const ObjectMap& value, const std::string& key, const std::string& fallback = {}) {
    const auto it = value.find(key); return it == value.end() ? fallback : it->second.To<std::string>();
}
ObjectMap scopedInvoke(Scene& scene, Product& product, const std::string& scope,
    const std::string& method, const ObjectMap& payload) {
    if (scope != "scene" && scope != "product") throw std::invalid_argument("Invalid test scope");
    Application application; iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(registry);
    iCAX::Interaction::CInvocation request; request.nCallID = 1;
    request.Method = iCAX::Interaction::MakeSDOMethod("TubeDesigner", method);
    const auto bytes = iCAX::Data::VariantSerializer::Serialize(payload);
    request.Payload.assign(bytes.begin(), bytes.end());
    const auto sdo = registry.Find(request.Method.nSDOCode);
    if (!sdo || !sdo->HasMethod(request.Method.nMethodCode)) throw std::invalid_argument("Missing real SDO: " + method);
    const auto result = sdo->Invoke(request, application, &product, nullptr, scope == "scene" ? &scene : nullptr);
    if (!result.IsOK()) throw std::runtime_error(result.strError);
    return iCAX::Data::VariantSerializer::Deserialize(std::string(result.Payload.begin(), result.Payload.end())).To<ObjectMap>();
}
std::string base64(std::span<const uint8_t> bytes) {
    constexpr char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result; result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned n = (unsigned(bytes[i]) << 16) | (i + 1 < bytes.size() ? unsigned(bytes[i + 1]) << 8 : 0)
            | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        result += chars[(n >> 18) & 63]; result += chars[(n >> 12) & 63];
        result += i + 1 < bytes.size() ? chars[(n >> 6) & 63] : '='; result += i + 2 < bytes.size() ? chars[n & 63] : '=';
    }
    return result;
}
unsigned long long version(const ObjectMap& payload) {
    if (!payload.contains("version")) return 0;
    const auto& value = payload.at("version");
    if (value.Is<unsigned long long>()) return value.To<unsigned long long>();
    if (value.Is<long long>()) return static_cast<unsigned long long>(value.To<long long>());
    if (value.Is<int>()) return static_cast<unsigned long long>(value.To<int>());
    if (value.Is<unsigned int>()) return value.To<unsigned int>();
    return static_cast<unsigned long long>(value.To<double>());
}
std::filesystem::path projectPath(const ObjectMap& payload) {
    const auto file = text(payload, "file", "side-sketch-native.ictd");
    if (file.empty() || file.find_first_of("/\\:") != std::string::npos || file == "." || file == ".." || file.find('\0') != std::string::npos)
        throw std::invalid_argument("Project file must be a filename in the test artifact directory");
    const auto name = std::filesystem::path(std::u8string(file.begin(), file.end()));
    if (name.extension() != L".ictd") throw std::invalid_argument("Project extension must be .ictd");
    const auto dir = std::filesystem::current_path() / "output/tests/side-sketch-native-browser";
    std::filesystem::create_directories(dir); return dir / name;
}
}

int main() {
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::ios::sync_with_stdio(false); std::cin.tie(nullptr); logInvocations = false;
    std::unique_ptr<Scene> scene; Product product;
    iCAX::ProjectFile::CProjectFile file({.Magic = "ICAX_TUBE_DESIGNER", .ProductID = "icax.tube-designer", .CurrentFormatVersion = "0.1", .nCurrentFormatRevision = 1});
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        Variant id; ObjectMap response; bool exiting = false;
        try {
            const auto input = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(line).To<ObjectMap>();
            if (input.contains("id")) id = input.at("id");
            const auto action = text(input, "action");
            const auto payload = input.contains("payload") ? input.at("payload").To<ObjectMap>() : ObjectMap{};
            ObjectMap result;
            if (action == "exit") { result = {{"exited", true}}; exiting = true; }
            else if (action == "read-cad-document") {
                // A fresh generic service container, with no Product/Scene/SDO.
                iCAX::Services::CServiceProvider provider;
                iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
                const auto service = provider.Resolve<iCAX::CAD::ICadDocumentImportService>();
                const auto path = text(payload, "sourcePath");
                result = cadDocument(service->Load(std::filesystem::path(std::u8string(path.begin(), path.end()))));
            }
            else {
                if (!scene || action == "reset") scene = std::make_unique<Scene>();
                if (action == "reset") result = {{"reset", true}};
                else if (action == "invoke") result = scopedInvoke(*scene, product, text(input, "scope", "scene"), text(input, "method"), payload);
                else if (action == "resource") {
                    const auto url = text(payload, "url"); const auto v = version(payload);
                    const auto bytes = scene->Resources().Get<iCAX::Resource::CFlatBufferResource>(url, v);
                    if (!bytes) throw std::runtime_error("Real resource not present in test scene");
                    result = {{"base64", base64(bytes->Bytes())}, {"bytes", static_cast<unsigned long long>(bytes->Size())}, {"url", url}, {"version", v}};
                    if (const auto info = scene->Resources().GetInfo(url)) {
                        result["runtimeOnly"] = info->IsRuntimeOnly();
                        const auto owner = info->Metadata.find("cad-preview-owner");
                        if (owner != info->Metadata.end()) result["previewOwner"] = owner->second;
                        const auto tubeOwner = info->Metadata.find("tube-cad-preview-owner");
                        if (tubeOwner != info->Metadata.end()) result["previewOwner"] = tubeOwner->second;
                    }
                } else if (action == "geometry") {
                    const auto component = part(*scene, text(payload, "partEntityId"));
                    const auto brep = shape(*scene, component);
                    GProp_GProps properties;
                    // Adaptive integration is essential for spline boundaries;
                    // fixed quadrature can misreport a narrow curved kerf by
                    // several cubic millimetres despite an accurate BRep.
                    BRepGProp::VolumeProperties(brep, properties, 1.0e-9);
                    result = {{"volume", properties.Mass()}, {"valid", BRepCheck_Analyzer(brep).IsValid()},
                        {"resourceId", component->GetManufacturingGeometryResourceID()}, {"version", component->GetManufacturingGeometryResourceVersion()}};
                } else if (action == "cad-document-geometry") {
                    const auto component = part(*scene, text(payload, "partEntityId"));
                    const auto cad = std::dynamic_pointer_cast<iCAX::TubeDesigner::CTubePartCadComponent>(
                        component->GetEntity()->GetComponent(iCAX::TubeDesigner::CTubePartCadComponent::S_ClassName));
                    if (!cad) throw std::runtime_error("No authoritative CAD document on test part");
                    const auto document = scene->Resources().Get<iCAX::GeometryData::Tube::CTubeCadDocument>(
                        cad->GetDocumentResourceID(), cad->GetDocumentResourceVersion());
                    const auto service = scene->Services().Resolve<iCAX::CAD::ITubeCadDocumentService>();
                    if (!document || !service) throw std::runtime_error("CAD document or registered service unavailable");
                    const auto evaluated = service->Evaluate(*document);
                    const auto rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(evaluated.Shape);
                    if (!rebuilt.bOK || rebuilt.Shape.IsNull()) throw std::runtime_error("Neutral document cannot rebuild");
                    const auto published = shape(*scene, component);
                    const auto differenceVolume = [](const TopoDS_Shape& a, const TopoDS_Shape& b) {
                        BRepAlgoAPI_Cut cut;
                        NCollection_List<TopoDS_Shape> arguments, tools;
                        arguments.Append(BRepBuilderAPI_Copy(a, true, false).Shape());
                        tools.Append(BRepBuilderAPI_Copy(b, true, false).Shape());
                        cut.SetArguments(arguments); cut.SetTools(tools); cut.SetNonDestructive(true);
                        cut.SetToFillHistory(false); cut.Build();
                        if (!cut.IsDone() || cut.Shape().IsNull() || !BRepCheck_Analyzer(cut.Shape()).IsValid())
                            throw std::runtime_error("Independent CAD/document geometry comparison failed");
                        GProp_GProps properties; BRepGProp::VolumeProperties(cut.Shape(), properties, 1.0e-9);
                        return std::abs(properties.Mass());
                    };
                    GProp_GProps properties; BRepGProp::VolumeProperties(rebuilt.Shape, properties, 1.0e-9);
                    result = {{"volume", properties.Mass()}, {"valid", BRepCheck_Analyzer(rebuilt.Shape).IsValid()},
                        {"directedDifferenceVolume", differenceVolume(rebuilt.Shape, published) + differenceVolume(published, rebuilt.Shape)},
                        {"documentResourceId", cad->GetDocumentResourceID()},
                        {"documentResourceVersion", cad->GetDocumentResourceVersion()}, {"revision", std::to_string(document->Revision)}};
                } else if (action == "compare-geometry") {
                    const auto a = shape(*scene, part(*scene, text(payload, "firstPartEntityId")));
                    const auto b = shape(*scene, part(*scene, text(payload, "secondPartEntityId")));
                    BRepAlgoAPI_Common common(a, b); common.Build();
                    if (!common.IsDone()) throw std::runtime_error("Real BRep intersection failed");
                    result = {{"firstVolume", volume(a)}, {"secondVolume", volume(b)}, {"commonVolume", volume(common.Shape())}};
                } else if (action == "save") {
                    const auto path = projectPath(payload); iCAX::ProjectFile::CProjectDocumentInfo info;
                    info.ProjectID = scene->project; info.MainSceneID = scene->GetSceneID(); info.ProjectName = text(payload, "name", "二维绘制真实原生验收");
                    file.Save(path, info, scene->Database(), scene->Resources()); result = {{"saved", true}, {"file", path.string()}};
                } else if (action == "open") {
                    const auto path = projectPath(payload); auto prepared = file.PrepareOpen(path); const auto info = prepared.Result().Info;
                    auto reopened = std::make_unique<Scene>(info.ProjectID, info.MainSceneID, false);
                    file.Restore(prepared, reopened->Database(), reopened->Resources()); scene = std::move(reopened);
                    result = {{"opened", true}, {"file", path.string()}, {"snapshot", scopedInvoke(*scene, product, "scene", "List", {})}};
                } else throw std::invalid_argument("Unknown test bridge action");
            }
            response = {{"id", id}, {"ok", true}, {"result", result}};
        } catch (const std::exception& error) { response = {{"id", id}, {"ok", false}, {"error", std::string(error.what())}}; }
        catch (...) { response = {{"id", id}, {"ok", false}, {"error", std::string("Unknown test bridge failure")}}; }
        std::cout << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(response) << '\n' << std::flush;
        if (exiting) break;
    }
}
