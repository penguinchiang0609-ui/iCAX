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
#include <TubeDesigner/PartDrawingDefinition.h>
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
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <limits>
#include <vector>
#include <atomic>

namespace imported_part_hole_recovery {
// Imported CAD owns its neutral document on import. The removed recipe recovery
// APIs have no tests; current shared editor coverage lives in UnifiedCadDocument.
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
    bool IsStopRequested() const noexcept override { return stopped.load(); }
    void RequestStop() { stopped.store(true); }
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
    std::atomic_bool stopped = false;
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

Invocation Invoke(Scene& scene, const std::string& method, const ObjectMap& payload,
    std::function<void(const ObjectMap&)> onReport = {}) {
    Application application;
    iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(registry);
    iCAX::Interaction::CInvocation request;
    request.nCallID = 1;
    request.Method = iCAX::Interaction::MakeSDOMethod("TubeDesigner", method);
    const auto json = iCAX::Data::VariantSerializer::Serialize(payload);
    request.Payload.assign(json.begin(), json.end());
    if (onReport) request.SetReportHandler([onReport](const std::vector<uint8_t>& bytes) {
        onReport(iCAX::Data::VariantSerializer::Deserialize(
            std::string(bytes.begin(), bytes.end())).To<ObjectMap>());
    });
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

TEST(ImportedPartHoleRecoverySDO, ShortManufacturedChannelImportsWithMatchedSection) {
    // Supplier-original P1044 is only 40 mm along its extrusion but 94.5 mm
    // across the C section. Length-to-section ratio is not proof of linearity.
    const auto source = std::filesystem::current_path() / "samples" / "structural-steel"
        / "vendors-angle-channel" / "unistrut-P1044-c-fitting.step";
    ASSERT_TRUE(std::filesystem::is_regular_file(source));
    Scene scene;
    const auto imported = Invoke(scene, "ImportNestingPart", {{"sourcePath", source.string()}});
    ASSERT_TRUE(imported.OK) << imported.Error;
    const auto profile = imported.Payload.at("profile").To<ObjectMap>();
    EXPECT_EQ(profile.at("id").To<std::string>(), "channel");
    const auto id = imported.Payload.at("partEntityId").To<std::string>();
    const auto document = Invoke(scene, "GetTubeCadDocument", {{"partEntityId", id}});
    ASSERT_TRUE(document.OK) << document.Error;
    EXPECT_TRUE(document.Payload.contains("documentResourceId"));
    EXPECT_FALSE(document.Payload.at("geometry").To<ObjectMap>().at("SolidNodes").To<VariantArray>().empty());
    const auto entity = scene.Database().GetEntity(iCAX::Data::uuid::from_string(id).value());
    ASSERT_TRUE(entity);
    const auto part = std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
        entity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
    ASSERT_TRUE(part);
    EXPECT_NEAR(part->GetLength(), 40.0, 1.0e-6);
    const auto brep = scene.Resources().Get<BRepModel>(
        part->GetManufacturingGeometryResourceID(), part->GetManufacturingGeometryResourceVersion());
    ASSERT_TRUE(brep);
    const auto shape = iCAX::OpenCascade::BuildOpenCascadeShape(*brep);
    ASSERT_TRUE(shape.bOK);
    ASSERT_FALSE(shape.Shape.IsNull());
    EXPECT_TRUE(BRepCheck_Analyzer(shape.Shape).IsValid());
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(shape.Shape, bounds, false, false);
    double minX, minY, minZ, maxX, maxY, maxZ;
    bounds.Get(minX, minY, minZ, maxX, maxY, maxZ);
    EXPECT_NEAR(maxX - minX, 40.0, 1.0e-6);
    EXPECT_NEAR(std::max(maxY - minY, maxZ - minZ), 94.5, 1.0e-6);
}

}
