#include "pch.h"
#include <ApplicationContext/IApplicationContext.h>
#include <Data/VariantSerializer.h>
#include <Database/IRepository.h>
#include <Database/MetaRegistrationCatalog.h>
#include <GeometryData/BRepPersistence.h>
#include <GeometryData/GeometryData.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h>
#include <ProjectContext/ISceneContext.h>
#include <Resources/ResourceLibrary.h>
#include <SDO/SDORegistrationCatalog.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <Services/ServiceProvider.h>
#include <TubeDesigner/TubeDesigner.h>
#include <TubeDesigner/TubeDesignerComponents.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <filesystem>
#include <iostream>
#include <optional>
#include <limits>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <TopExp_Explorer.hxx>

namespace imported_part_batch_recovery {
using iCAX::Data::ObjectMap;
using iCAX::Data::VariantArray;
using iCAX::GeometryData::BRepModel;

class Application final : public iCAX::Application::IApplicationContext {
public:
    Application() { paths.InstallDirectory = std::filesystem::current_path().string();
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services); }
    const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override { return descriptor; }
    const iCAX::Application::CApplicationPaths& GetPaths() const override { return paths; }
    iCAX::Data::PropertyBag GetSettings() const override { return {}; }
    const iCAX::Services::CServiceProvider& Services() const override {
        return services;
    }
private:
    iCAX::Application::CApplicationDescriptor descriptor;
    iCAX::Application::CApplicationPaths paths;
    mutable iCAX::Services::CServiceProvider services;
};

class Scene final : public iCAX::Project::ISceneContext {
public:
    Scene() : project(iCAX::Data::GenerateNewUUID()), id(iCAX::Data::GenerateNewUUID()) {
        (void)iCAX::TubeDesigner::GetTubeDesignerContractVersion();
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
        auto registry = iCAX::Database::CreateMetaRegistry();
        wchar_t exe[32768]{}, db[32768]{}, render[32768]{}, transform[32768]{};
        GetModuleFileNameW(nullptr, exe, 32768);
        GetModuleFileNameW(GetModuleHandleW(L"Database.dll"), db, 32768);
        GetModuleFileNameW(GetModuleHandleW(L"RenderInteraction.dll"), render, 32768);
        GetModuleFileNameW(GetModuleHandleW(L"Transform.dll"), transform, 32768);
        iCAX::Database::CMetaRegistrationCatalog::ReplayByModulePaths(*registry,
            {std::filesystem::path(db).string(), std::filesystem::path(render).string(),
             std::filesystem::path(transform).string(), std::filesystem::path(exe).string()});
        repository = iCAX::Database::GenerateRepository(id, registry);
        resources.SetScope(iCAX::Resource::MakeSceneResourceScope(
            "icax", "icax.tube-designer", project, id));
        iCAX::Resource::CResourceVersionCodec codec;
        codec.Serialize = [](const std::shared_ptr<void>& value)
            -> std::optional<std::vector<uint8_t>> {
            return iCAX::GeometryData::Persistence::Serialize(
                *std::static_pointer_cast<BRepModel>(value));
        };
        codec.Deserialize = [](std::span<const uint8_t> bytes) -> std::shared_ptr<void> {
            return std::make_shared<BRepModel>(
                iCAX::GeometryData::Persistence::Deserialize(bytes));
        };
        if (!resources.RegisterPersistenceCodec<BRepModel>(BRepModel::kResourceTypeName, codec))
            throw std::runtime_error("Cannot register BRep persistence");
        repository->BeginLoadBaseline();
        auto entity = repository->GetMetaEntity();
        if (!entity) entity = repository->CreateEntity(id);
        entity->AddComponent<iCAX::TubeDesigner::CTubeDesignerRootComponent>();
        repository->EndLoadBaseline();
    }
    const iCAX::Data::uuid& GetSceneID() const override { return id; }
    const iCAX::Data::uuid& GetSceneChannelID() const override { return id; }
    const std::string& GetSceneName() const override { return name; }
    iCAX::Interaction::CSDOEndpoint GetBackendSDOEndpoint() const override { return {}; }
    iCAX::Interaction::CSDOEndpoint GetFrontendSDOEndpoint() const override { return {}; }
    bool IsMainScene() const override { return true; }
    bool IsTransientScene() const override { return false; }
    iCAX::Database::IRepository& Database() override { return *repository; }
    const iCAX::Database::IRepository& Database() const override { return *repository; }
    iCAX::Resource::CResourceLibrary& Resources() override { return resources; }
    const iCAX::Resource::CResourceLibrary& Resources() const override { return resources; }
    bool HasPDOHub() const override { return false; }
    iCAX::PDO::IPDOHub& PDOHub() override { throw std::logic_error("No PDO in test"); }
    const iCAX::PDO::IPDOHub& PDOHub() const override { throw std::logic_error("No PDO in test"); }
    iCAX::Services::CServiceProvider& Services() const override {
        return services;
    }
private:
    iCAX::Data::uuid project, id;
    std::string name = "Imported tube hole recovery";
    std::shared_ptr<iCAX::Database::IRepository> repository;
    iCAX::Resource::CResourceLibrary resources;
    mutable iCAX::Services::CServiceProvider services;
};

struct Invocation {
    bool OK = false;
    ObjectMap Payload;
    std::string Error;
};

double MaterialVolume(const TopoDS_Shape& shape) {
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return std::abs(properties.Mass());
}

double RelativeSymmetricDifference(const TopoDS_Shape& first, const TopoDS_Shape& second) {
    const auto difference = [](const TopoDS_Shape& left, const TopoDS_Shape& right) {
        BRepAlgoAPI_Cut cut(left, right);
        cut.SetFuzzyValue(1.0e-6);
        cut.Build();
        if (!cut.IsDone() || cut.Shape().IsNull() || !BRepCheck_Analyzer(cut.Shape()).IsValid())
            return std::numeric_limits<double>::infinity();
        return MaterialVolume(cut.Shape());
    };
    return (difference(first, second) + difference(second, first))
        / std::max(MaterialVolume(first), 1.0);
}

Invocation Invoke(Scene& scene, const std::string& method, const ObjectMap& payload) {
    Application application;
    iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(registry);
    iCAX::Interaction::CInvocation request;
    request.nCallID = 1;
    request.Method = iCAX::Interaction::MakeSDOMethod("TubeDesigner", method);
    const auto json = iCAX::Data::VariantSerializer::Serialize(payload);
    request.Payload.assign(json.begin(), json.end());
    const auto sdo = registry.Find(request.Method.nSDOCode);
    if (!sdo || !sdo->HasMethod(request.Method.nMethodCode))
        return {.Error = "Missing SDO: " + method};
    try {
        const auto result = sdo->Invoke(request, application, nullptr, nullptr, &scene);
        if (!result.IsOK()) return {.Error = result.strError};
        auto parsed = iCAX::Data::VariantSerializer::Deserialize(
            std::string(result.Payload.begin(), result.Payload.end()));
        return {.OK = true, .Payload = parsed.To<ObjectMap>()};
    } catch (const std::exception& error) {
        return {.Error = error.what()};
    }
}

// Opt-in probe: one real CAD file per process keeps errors and timeouts isolated.
// Import attaches its neutral document directly; save uses the current CAD API.
TEST(ImportedPartBatchRecoverySDO, ExternalFileRoundTripsThroughCurrentCadDocumentAPI) {
    const auto environment = [](const wchar_t* name) {
        const DWORD capacity = GetEnvironmentVariableW(name, nullptr, 0);
        if (!capacity) return std::wstring();
        std::wstring value(capacity, L'\0');
        const DWORD length = GetEnvironmentVariableW(name, value.data(), capacity);
        if (!length || length >= capacity) return std::wstring();
        value.resize(length);
        return value;
    };
    const auto sourceWide = environment(L"ICAX_RECOVERY_SOURCE");
    const auto reportWide = environment(L"ICAX_RECOVERY_REPORT");
    if (sourceWide.empty() || reportWide.empty()) {
        std::cout << "[not-run] Set ICAX_RECOVERY_SOURCE and ICAX_RECOVERY_REPORT" << std::endl;
        return;
    }
    const auto utf8 = [](const std::filesystem::path& path) {
        const auto bytes = path.u8string();
        return std::string(bytes.begin(), bytes.end());
    };
    const std::filesystem::path sourcePath(sourceWide);
    const auto sourceValue = utf8(sourcePath);
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const std::filesystem::path reportPath(reportWide);
    ObjectMap report{{"schemaVersion", 1}, {"sourcePath", std::string(sourceValue)},
        {"status", std::string("running")}, {"passed", false}, {"stage", std::string("setup")}};
    ObjectMap stages;
    auto write = [&] {
        report["stages"] = stages;
        report["totalSeconds"] = std::chrono::duration<double>(Clock::now() - start).count();
        if (!reportPath.parent_path().empty()) std::filesystem::create_directories(reportPath.parent_path());
        std::ofstream stream(reportPath, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Cannot open recovery report");
        stream << iCAX::Data::VariantSerializer::Serialize(report);
        stream.flush();
    };
    auto require = [](bool condition, const std::string& reason) {
        if (!condition) throw std::runtime_error(reason);
    };
    auto timed = [&](Scene& scene, const std::string& stage, const std::string& method,
                     const ObjectMap& payload) {
        report["stage"] = stage;
        stages[stage] = ObjectMap{{"status", std::string("running")}};
        write();
        const auto began = Clock::now();
        auto result = Invoke(scene, method, payload);
        stages[stage] = ObjectMap{{"status", std::string(result.OK ? "ok" : "rejected")},
            {"seconds", std::chrono::duration<double>(Clock::now() - began).count()},
            {"reason", result.Error}};
        write();
        return result;
    };
    try {
        write();
        require(std::filesystem::is_regular_file(sourcePath), "Source file is missing");
        report["stage"] = std::string("inspectSource");
        stages["inspectSource"] = ObjectMap{{"status", std::string("running")}};
        write();
        const auto inspectedAt = Clock::now();
        TopoDS_Shape sourceShape;
        auto extension = sourcePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".step" || extension == ".stp") {
            STEPControl_Reader reader;
            require(reader.ReadFile(sourceValue.c_str()) == IFSelect_RetDone, "Cannot read source STEP");
            require(reader.TransferRoots() > 0, "Source STEP has no transferable roots");
            sourceShape = reader.OneShape();
        } else if (extension == ".iges" || extension == ".igs") {
            IGESControl_Reader reader;
            require(reader.ReadFile(sourceValue.c_str()) == IFSelect_RetDone, "Cannot read source IGES");
            require(reader.TransferRoots() > 0, "Source IGES has no transferable roots");
            sourceShape = reader.OneShape();
        } else {
            throw std::runtime_error("Expected STEP or IGES input");
        }
        require(!sourceShape.IsNull(), "Source CAD shape is empty");
        std::vector<TopoDS_Shape> sourceSolids;
        for (TopExp_Explorer explorer(sourceShape, TopAbs_SOLID); explorer.More(); explorer.Next())
            sourceSolids.push_back(explorer.Current());
        report["sourceSolidCount"] = static_cast<int>(sourceSolids.size());
        report["sourceKind"] = std::string(sourceSolids.size() > 1 ? "assembly" : "single-part");
        stages["inspectSource"] = ObjectMap{{"status", std::string("ok")},
            {"seconds", std::chrono::duration<double>(Clock::now() - inspectedAt).count()}};
        if (sourceSolids.size() > 1) {
            report["status"] = std::string("assembly");
            report["reason"] = std::string("Multiple original solids; assembly is not evaluated as one extrusion");
            const auto splitWide = environment(L"ICAX_RECOVERY_SPLIT_DIRECTORY");
            if (!splitWide.empty()) {
                const std::filesystem::path splitDirectory(splitWide);
                std::filesystem::create_directories(splitDirectory);
                VariantArray children;
                for (size_t index = 0; index < sourceSolids.size(); ++index) {
                    const auto child = splitDirectory / ("solid-" + std::to_string(index + 1) + ".step");
                    STEPControl_Writer writer;
                    require(writer.Transfer(sourceSolids[index], STEPControl_AsIs) == IFSelect_RetDone,
                        "Cannot transfer original solid for isolated testing");
                    require(writer.Write(utf8(child).c_str()) == IFSelect_RetDone, "Cannot export original solid");
                    children.emplace_back(ObjectMap{{"solidIndex", static_cast<int>(index + 1)},
                        {"sourcePath", utf8(child)}, {"parentFile", sourceValue},
                        {"derivation", std::string("Original solid exported without geometric modification")}});
                }
                report["children"] = children;
            }
            write();
            return;
        }
        Scene scene;
        const auto imported = timed(scene, "importRecognizeFit", "ImportNestingPart",
            {{"sourcePath", std::string(sourceValue)}});
        require(imported.OK, imported.Error);
        const auto id = imported.Payload.at("partEntityId").To<std::string>();
        report["profile"] = imported.Payload.at("profile");
        if (imported.Payload.contains("recognition")) report["recognition"] = imported.Payload.at("recognition");
        const auto entity = scene.Database().GetEntity(iCAX::Data::uuid::from_string(id).value());
        require(bool(entity), "Import did not create a part entity");
        const auto getPart = [&] {
            return std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
                entity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
        };
        const auto part = getPart();
        require(bool(part), "Import did not create manufacturing data");
        const auto sourceId = part->GetManufacturingGeometryResourceID();
        const auto sourceVersion = part->GetManufacturingGeometryResourceVersion();
        const auto originalBRep = scene.Resources().Get<BRepModel>(sourceId, sourceVersion);
        require(bool(originalBRep), "Imported BRep is missing");
        const auto originalBytes = iCAX::GeometryData::Persistence::Serialize(*originalBRep);
        const auto originalShape = iCAX::OpenCascade::BuildOpenCascadeShape(*originalBRep);
        require(originalShape.bOK && BRepCheck_Analyzer(originalShape.Shape).IsValid(), "Imported BRep is invalid");
        int solids = 0;
        for (TopExp_Explorer explorer(originalShape.Shape, TopAbs_SOLID); explorer.More(); explorer.Next()) ++solids;
        report["importedSolidCount"] = solids;
        report["lengthMm"] = part->GetLength();
        report["originalVolumeMm3"] = MaterialVolume(originalShape.Shape);
        const auto fetched = timed(scene, "readCadDocument", "GetTubeCadDocument", {{"partEntityId", id}});
        require(fetched.OK, fetched.Error);
        const auto cadComponent = entity->GetComponent(iCAX::TubeDesigner::CTubePartCadComponent::S_ClassName);
        require(bool(cadComponent), "Import did not attach its neutral CAD document");
        const auto geometry = fetched.Payload.at("geometry").To<ObjectMap>();
        report["solidNodeCount"] = static_cast<int>(geometry.at("SolidNodes").To<VariantArray>().size());
        report["documentResourceId"] = fetched.Payload.at("documentResourceId");
        report["geometry"] = geometry;
        const auto applied = timed(scene, "saveCadDocument", "ApplyTubeCadDocument", {
            {"partEntityId", id}, {"resourceVersion", sourceVersion},
            {"revision", fetched.Payload.at("revision")}, {"geometry", geometry}});
        require(applied.OK, applied.Error);
        report["stage"] = std::string("compareReplay");
        stages["compareReplay"] = ObjectMap{{"status", std::string("running")}};
        write();
        const auto comparedAt = Clock::now();
        const auto finalPart = getPart();
        require(bool(finalPart), "CAD save removed the part");
        const auto finalBRep = scene.Resources().Get<BRepModel>(
            finalPart->GetManufacturingGeometryResourceID(), finalPart->GetManufacturingGeometryResourceVersion());
        require(bool(finalBRep), "CAD save did not store BRep");
        const auto finalShape = iCAX::OpenCascade::BuildOpenCascadeShape(*finalBRep);
        require(finalShape.bOK && BRepCheck_Analyzer(finalShape.Shape).IsValid(), "CAD document BRep is invalid");
        const auto error = RelativeSymmetricDifference(originalShape.Shape, finalShape.Shape);
        report["relativeSymmetricDifference"] = std::isfinite(error) ? error : -1.0;
        report["replayedVolumeMm3"] = MaterialVolume(finalShape.Shape);
        const bool equivalent = std::isfinite(error) && error <= 1.0e-8;
        stages["compareReplay"] = ObjectMap{{"status", std::string(equivalent ? "ok" : "failed")},
            {"seconds", std::chrono::duration<double>(Clock::now() - comparedAt).count()}};
        require(equivalent, "Saved CAD geometry differs from original BRep (relative symmetric difference > 1e-8)");
        report["stage"] = std::string("complete");
        report["status"] = std::string("passed");
        report["passed"] = true;
        write();
    } catch (const std::exception& error) {
        report["status"] = std::string("failed");
        report["reason"] = std::string(error.what());
        try { write(); } catch (...) {}
        ADD_FAILURE() << error.what();
    }
}
}
