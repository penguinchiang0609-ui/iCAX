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
#include <TubeDesigner/TubeDesigner.h>
#include <TubeDesigner/TubeDesignerComponents.h>
#include <TubeDesigner/RecoveredPartDrawingFeatures.h>
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
using iCAX::Data::ObjectMap;
using iCAX::Data::VariantArray;
using iCAX::GeometryData::BRepModel;

class Application final : public iCAX::Application::IApplicationContext {
public:
    Application() { paths.InstallDirectory = std::filesystem::current_path().string(); }
    const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override { return descriptor; }
    const iCAX::Application::CApplicationPaths& GetPaths() const override { return paths; }
    iCAX::Data::PropertyBag GetSettings() const override { return {}; }
    const iCAX::Services::CServiceProvider& Services() const override {
        throw std::logic_error("No service provider in recovery test");
    }
private:
    iCAX::Application::CApplicationDescriptor descriptor;
    iCAX::Application::CApplicationPaths paths;
};

class Scene final : public iCAX::Project::ISceneContext {
public:
    Scene() : project(iCAX::Data::GenerateNewUUID()), id(iCAX::Data::GenerateNewUUID()) {
        (void)iCAX::TubeDesigner::GetTubeDesignerContractVersion();
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
        throw std::logic_error("No scene services in test");
    }
private:
    std::atomic_bool stopped = false;
    iCAX::Data::uuid project, id;
    std::string name = "Imported tube hole recovery";
    std::shared_ptr<iCAX::Database::IRepository> repository;
    iCAX::Resource::CResourceLibrary resources;
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

TEST(ImportedPartHoleRecoverySDO, ShutdownDuringRecoveryNeverAttachesPartialDrawing) {
    for (const char* phase : {"recognize", "save"}) {
        Scene scene;
        const auto source = std::filesystem::current_path() / "samples" / "tube-one" /
            "03_round_tube_through_hole.step";
        const auto imported = Invoke(scene, "ImportNestingPart", {{"sourcePath", source.string()}});
        ASSERT_TRUE(imported.OK) << imported.Error;
        const auto id = imported.Payload.at("partEntityId").To<std::string>();
        const auto partId = iCAX::Data::uuid::from_string(id).value();
        const auto entity = scene.Database().GetEntity(partId);
        const auto part = std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
            entity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
        ASSERT_TRUE(part);
        const auto resourceId = part->GetManufacturingGeometryResourceID();
        const auto version = part->GetManufacturingGeometryResourceVersion();
        const auto original = scene.Resources().Get<BRepModel>(resourceId, version);
        ASSERT_TRUE(original);
        const auto bytes = iCAX::GeometryData::Persistence::Serialize(*original);
        bool canceledAtPhase = false;
        const auto recovered = Invoke(scene, "RecoverImportedPartDrawing", {{"partEntityId", id}},
            [&](const ObjectMap& report) {
                if (report.at("phase").To<std::string>() == phase) {
                    canceledAtPhase = true;
                    scene.RequestStop();
                }
            });
        EXPECT_TRUE(canceledAtPhase) << phase;
        EXPECT_FALSE(recovered.OK) << phase;
        EXPECT_NE(recovered.Error.find("canceled"), std::string::npos) << recovered.Error;
        EXPECT_FALSE(entity->GetComponent(iCAX::TubeDesigner::CPartDrawingComponent::S_ClassName));
        EXPECT_EQ(part->GetManufacturingGeometryResourceID(), resourceId);
        EXPECT_EQ(part->GetManufacturingGeometryResourceVersion(), version);
        EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(
            *scene.Resources().Get<BRepModel>(resourceId, version)), bytes);
    }
}

TEST(ImportedPartHoleRecoverySDO, ExistingStepHolesRequireExactEditorReplay) {
    for (const char* file : {
        "03_round_tube_through_hole.step",
        "05_double_cavity_blind_hole.step"}) {
        Scene scene;
        const auto source = std::filesystem::current_path() / "samples" / "tube-one" / file;
        const auto imported = Invoke(scene, "ImportNestingPart", {{"sourcePath", source.string()}});
        ASSERT_TRUE(imported.OK) << file << ": " << imported.Error;
        const auto id = imported.Payload.at("partEntityId").To<std::string>();
        const auto profile = imported.Payload.at("profile").To<ObjectMap>();
        const bool isRound = std::string(file).find("03_") == 0;
        EXPECT_EQ(profile.at("id").To<std::string>(),
            isRound ? "round" : "irregular") << file;
        const auto recovered = Invoke(scene, "RecoverImportedPartDrawing", {{"partEntityId", id}});
        if (isRound) {
            const auto importedEntity = scene.Database().GetEntity(
                iCAX::Data::uuid::from_string(id).value());
            ASSERT_TRUE(importedEntity);
            const auto importedPart = std::dynamic_pointer_cast<
                iCAX::TubeDesigner::CManufacturingPartComponent>(
                importedEntity->GetComponent(
                    iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
            ASSERT_TRUE(importedPart);
            const auto brep = scene.Resources().Get<BRepModel>(
                importedPart->GetManufacturingGeometryResourceID(),
                importedPart->GetManufacturingGeometryResourceVersion());
            ASSERT_TRUE(brep);
            const auto sourceShape = iCAX::OpenCascade::BuildOpenCascadeShape(*brep);
            ASSERT_TRUE(sourceShape.bOK);
            Bnd_Box box;
            BRepBndLib::AddOptimal(sourceShape.Shape, box, false, false);
            double minX, minY, minZ, maxX, maxY, maxZ;
            box.Get(minX, minY, minZ, maxX, maxY, maxZ);
            gp_Trsf toEditor;
            toEditor.SetTranslation(gp_Vec(-minX, -(minY + maxY) / 2,
                -(minZ + maxZ) / 2));
            const auto editorShape = BRepBuilderAPI_Transform(
                sourceShape.Shape, toEditor, true).Shape();
            const auto csg = iCAX::OpenCascade::ConvertBRepToTubeCSG(editorShape);
            const auto mapped = iCAX::TubeDesigner::MapNeutralTubeFeatures(
                csg.Geometry, importedPart->GetLength());
            ASSERT_TRUE(csg.ReplayValidation.bEquivalent);
            EXPECT_LT(csg.ReplayValidation.RelativeVolumeError, 1.0e-8);
            EXPECT_LT(csg.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
            ASSERT_TRUE(mapped.Complete);
            ASSERT_EQ(mapped.Features.size(), 1u);
            ASSERT_TRUE(recovered.OK) << recovered.Error;
            EXPECT_LT(recovered.Payload.at("relativeSymmetricDifference").To<double>(), 1.0e-8);
            const auto drawing = std::dynamic_pointer_cast<
                iCAX::TubeDesigner::CPartDrawingComponent>(
                importedEntity->GetComponent(
                    iCAX::TubeDesigner::CPartDrawingComponent::S_ClassName));
            ASSERT_TRUE(drawing);
            const auto config = drawing->GetDefinition();
            const auto features = config.at("features").To<VariantArray>();
            ASSERT_EQ(features.size(), 1u);
            const auto feature = features.front().To<ObjectMap>();
            EXPECT_TRUE(feature.at("opposite").To<bool>());
            const auto fetched = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
            ASSERT_TRUE(fetched.OK) << fetched.Error;
            EXPECT_EQ(fetched.Payload.at("definition").To<ObjectMap>(), config);
            const auto applied = Invoke(scene, "ApplyPartDrawing", {
                {"partEntityId", id},
                {"resourceId", importedPart->GetManufacturingGeometryResourceID()},
                {"resourceVersion", importedPart->GetManufacturingGeometryResourceVersion()},
                {"drawing", config.at("drawing")},
                {"features", config.at("features")},
                {"ends", config.at("ends")}
            });
            ASSERT_TRUE(applied.OK) << applied.Error;
            const auto editedPart = std::dynamic_pointer_cast<
                iCAX::TubeDesigner::CManufacturingPartComponent>(
                importedEntity->GetComponent(
                    iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
            ASSERT_TRUE(editedPart);
            const auto editedBRep = scene.Resources().Get<BRepModel>(
                editedPart->GetManufacturingGeometryResourceID(),
                editedPart->GetManufacturingGeometryResourceVersion());
            ASSERT_TRUE(editedBRep);
            const auto editedShape = iCAX::OpenCascade::BuildOpenCascadeShape(*editedBRep);
            ASSERT_TRUE(editedShape.bOK);
            const auto replayError = RelativeSymmetricDifference(
                sourceShape.Shape, editedShape.Shape);
            EXPECT_LT(replayError, 1.0e-8);
            std::cout << "[imported-hole-recovery] " << file
                      << " editorReplayError=" << replayError << std::endl;
        }
        if (!isRound) {
            EXPECT_FALSE(recovered.OK) << file;
            EXPECT_NE(recovered.Error.find("异型截面"), std::string::npos)
                << file << ": " << recovered.Error;
            std::cout << "[imported-hole-recovery] " << file
                      << " unsupported=" << recovered.Error << std::endl;
        }
        const auto entity = scene.Database().GetEntity(
            iCAX::Data::uuid::from_string(id).value());
        ASSERT_TRUE(entity);
        const auto drawing = entity->GetComponent(
            iCAX::TubeDesigner::CPartDrawingComponent::S_ClassName);
        EXPECT_EQ(bool(drawing), recovered.OK) << file;
    }
}

TEST(ImportedPartHoleRecoverySDO, RoundTubeSingleWallHoleReplaysInEditor) {
    const auto outer = BRepPrimAPI_MakeCylinder(50.0, 300.0).Shape();
    const auto inner = BRepPrimAPI_MakeCylinder(42.0, 300.0).Shape();
    BRepAlgoAPI_Cut hollow(outer, inner);
    ASSERT_TRUE(hollow.IsDone());
    const auto cutter = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(0.0, 70.0, 80.0), gp_Dir(0.0, -1.0, 0.0)),
        6.0, 30.0).Shape();
    BRepAlgoAPI_Cut cut(hollow.Shape(), cutter);
    ASSERT_TRUE(cut.IsDone());
    const auto source = std::filesystem::temp_directory_path()
        / ("icax-round-single-wall-hole-" +
            iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()) + ".step");
    struct RemoveFile {
        std::filesystem::path Path;
        ~RemoveFile() { std::error_code error; std::filesystem::remove(Path, error); }
    } cleanup{source};
    STEPControl_Writer writer;
    ASSERT_EQ(writer.Transfer(cut.Shape(), STEPControl_AsIs), IFSelect_RetDone);
    ASSERT_EQ(writer.Write(source.string().c_str()), IFSelect_RetDone);

    Scene scene;
    const auto imported = Invoke(scene, "ImportNestingPart", {{"sourcePath", source.string()}});
    ASSERT_TRUE(imported.OK) << imported.Error;
    const auto profile = imported.Payload.at("profile").To<ObjectMap>();
    const auto profileId = profile.at("id").To<std::string>();
    EXPECT_EQ(profileId, "round");
    const auto id = imported.Payload.at("partEntityId").To<std::string>();
    const auto entity = scene.Database().GetEntity(iCAX::Data::uuid::from_string(id).value());
    ASSERT_TRUE(entity);
    const auto part = std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
        entity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
    ASSERT_TRUE(part);
    const auto brep = scene.Resources().Get<BRepModel>(
        part->GetManufacturingGeometryResourceID(),
        part->GetManufacturingGeometryResourceVersion());
    ASSERT_TRUE(brep);
    const auto sourceShape = iCAX::OpenCascade::BuildOpenCascadeShape(*brep);
    ASSERT_TRUE(sourceShape.bOK);
    const auto originalBytes = iCAX::GeometryData::Persistence::Serialize(*brep);
    Bnd_Box box;
    BRepBndLib::AddOptimal(sourceShape.Shape, box, false, false);
    double minX, minY, minZ, maxX, maxY, maxZ;
    box.Get(minX, minY, minZ, maxX, maxY, maxZ);
    gp_Trsf toEditor;
    toEditor.SetTranslation(gp_Vec(-minX, -(minY + maxY) / 2, -(minZ + maxZ) / 2));
    const auto editorShape = BRepBuilderAPI_Transform(sourceShape.Shape, toEditor, true).Shape();
    const auto csg = iCAX::OpenCascade::ConvertBRepToTubeCSG(editorShape);
    const auto mapped = iCAX::TubeDesigner::MapNeutralTubeFeatures(csg.Geometry, part->GetLength());
    ASSERT_TRUE(csg.ReplayValidation.bEquivalent);
    EXPECT_LT(csg.ReplayValidation.RelativeVolumeError, 1.0e-8);
    EXPECT_LT(csg.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
    ASSERT_TRUE(mapped.Complete);
    ASSERT_EQ(mapped.Features.size(), 1u);
    const auto recovered = Invoke(scene, "RecoverImportedPartDrawing", {{"partEntityId", id}});
    ASSERT_TRUE(recovered.OK) << recovered.Error;
    EXPECT_LT(recovered.Payload.at("relativeSymmetricDifference").To<double>(), 1.0e-8);
    const auto savedBRep = scene.Resources().Get<BRepModel>(
        part->GetManufacturingGeometryResourceID(),
        part->GetManufacturingGeometryResourceVersion());
    ASSERT_TRUE(savedBRep);
    EXPECT_EQ(originalBytes, iCAX::GeometryData::Persistence::Serialize(*savedBRep));
    const auto drawing = std::dynamic_pointer_cast<iCAX::TubeDesigner::CPartDrawingComponent>(
        entity->GetComponent(iCAX::TubeDesigner::CPartDrawingComponent::S_ClassName));
    ASSERT_TRUE(drawing);
    const auto config = drawing->GetDefinition();
    ASSERT_EQ(config.at("features").To<VariantArray>().size(), 1u);
    EXPECT_NE(config.at("baseResourceId").To<std::string>(),
        part->GetManufacturingGeometryResourceID());
    const auto fetched = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
    ASSERT_TRUE(fetched.OK) << fetched.Error;
    EXPECT_EQ(fetched.Payload.at("definition").To<ObjectMap>(), config);
    const auto applied = Invoke(scene, "ApplyPartDrawing", {
        {"partEntityId", id},
        {"resourceId", part->GetManufacturingGeometryResourceID()},
        {"resourceVersion", part->GetManufacturingGeometryResourceVersion()},
        {"drawing", config.at("drawing")},
        {"features", config.at("features")},
        {"ends", config.at("ends")}
    });
    ASSERT_TRUE(applied.OK) << applied.Error;
    const auto editedEntity = scene.Database().GetEntity(iCAX::Data::uuid::from_string(id).value());
    ASSERT_TRUE(editedEntity);
    const auto editedPart = std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
        editedEntity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
    ASSERT_TRUE(editedPart);
    const auto editedBRep = scene.Resources().Get<BRepModel>(
        editedPart->GetManufacturingGeometryResourceID(),
        editedPart->GetManufacturingGeometryResourceVersion());
    ASSERT_TRUE(editedBRep);
    const auto editedShape = iCAX::OpenCascade::BuildOpenCascadeShape(*editedBRep);
    ASSERT_TRUE(editedShape.bOK);
    const auto replayError = RelativeSymmetricDifference(
        sourceShape.Shape, editedShape.Shape);
    EXPECT_LT(replayError, 1.0e-8);
    std::cout << "[imported-one-wall-hole] editorReplayError=" << replayError << std::endl;
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

TEST(ImportedPartHoleRecoverySDO, ManufacturedP1044FourHolesRecoverAndReplayThroughEditor) {
    const auto source = std::filesystem::current_path() / "samples" / "structural-steel"
        / "vendors-angle-channel" / "unistrut-P1044-c-fitting.step";
    ASSERT_TRUE(std::filesystem::is_regular_file(source));
    Scene scene;
    const auto imported = Invoke(scene, "ImportNestingPart", {{"sourcePath", source.string()}});
    ASSERT_TRUE(imported.OK) << imported.Error;
    ASSERT_EQ(imported.Payload.at("profile").To<ObjectMap>().at("id").To<std::string>(), "channel");
    const auto id = imported.Payload.at("partEntityId").To<std::string>();
    const auto partId = iCAX::Data::uuid::from_string(id).value();
    const auto getPart = [&]() -> std::shared_ptr<iCAX::TubeDesigner::CManufacturingPartComponent> {
        const auto entity = scene.Database().GetEntity(partId);
        if (!entity) return {};
        return std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
            entity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
    };
    const auto originalPart = getPart();
    ASSERT_TRUE(originalPart);
    const auto originalResourceId = originalPart->GetManufacturingGeometryResourceID();
    const auto originalResourceVersion = originalPart->GetManufacturingGeometryResourceVersion();
    const auto originalBRep = scene.Resources().Get<BRepModel>(
        originalResourceId, originalResourceVersion);
    ASSERT_TRUE(originalBRep);
    const auto originalBytes = iCAX::GeometryData::Persistence::Serialize(*originalBRep);
    const auto originalShape = iCAX::OpenCascade::BuildOpenCascadeShape(*originalBRep);
    ASSERT_TRUE(originalShape.bOK);
    ASSERT_FALSE(originalShape.Shape.IsNull());
    ASSERT_TRUE(BRepCheck_Analyzer(originalShape.Shape).IsValid());

    const auto recovered = Invoke(scene, "RecoverImportedPartDrawing", {
        {"partEntityId", id}, {"resourceVersion", originalResourceVersion}
    });
    ASSERT_TRUE(recovered.OK) << recovered.Error;
    ASSERT_TRUE(recovered.Payload.contains("relativeSymmetricDifference"));
    EXPECT_LE(recovered.Payload.at("relativeSymmetricDifference").To<double>(), 1.0e-8);
    const auto recoveredPart = getPart();
    ASSERT_TRUE(recoveredPart);
    EXPECT_EQ(recoveredPart->GetManufacturingGeometryResourceID(), originalResourceId);
    EXPECT_EQ(recoveredPart->GetManufacturingGeometryResourceVersion(), originalResourceVersion);
    const auto recoveredBRep = scene.Resources().Get<BRepModel>(
        recoveredPart->GetManufacturingGeometryResourceID(),
        recoveredPart->GetManufacturingGeometryResourceVersion());
    ASSERT_TRUE(recoveredBRep);
    EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*recoveredBRep), originalBytes);

    const auto fetched = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
    ASSERT_TRUE(fetched.OK) << fetched.Error;
    const auto definition = fetched.Payload.at("definition").To<ObjectMap>();
    ASSERT_TRUE(definition.contains("baseResourceId"));
    EXPECT_NE(definition.at("baseResourceId").To<std::string>(), originalResourceId);
    ASSERT_TRUE(definition.contains("features"));
    const auto features = definition.at("features").To<VariantArray>();
    ASSERT_EQ(features.size(), 4u);
    for (const auto& value : features) {
        const auto feature = value.To<ObjectMap>();
        for (const auto* key : {"type", "toolRef", "opposite", "diameter", "station"})
            ASSERT_TRUE(feature.contains(key)) << "Missing saved hole field: " << key;
        EXPECT_EQ(feature.at("type").To<std::string>(), "circle");
        const auto toolRef = feature.at("toolRef").To<ObjectMap>();
        // Saved references identify the resolved tool by id/version/digest;
        // the mapper's input-only scope is not part of this persisted record.
        for (const auto* key : {"id", "version", "digest"})
            ASSERT_TRUE(toolRef.contains(key)) << "Missing saved tool reference field: " << key;
        EXPECT_EQ(toolRef.at("id").To<std::string>(), "circle");
        EXPECT_FALSE(toolRef.at("version").To<std::string>().empty());
        EXPECT_FALSE(toolRef.at("digest").To<std::string>().empty());
        EXPECT_FALSE(feature.at("opposite").To<bool>());
        EXPECT_NEAR(feature.at("diameter").To<double>(), 14.0, 1.0e-6);
    }

    const auto apply = [&](const VariantArray& updatedFeatures) {
        const auto currentPart = getPart();
        if (!currentPart) return Invocation{.Error = "Editor save removed the part"};
        return Invoke(scene, "ApplyPartDrawing", {
            {"partEntityId", id},
            {"resourceId", currentPart->GetManufacturingGeometryResourceID()},
            {"resourceVersion", currentPart->GetManufacturingGeometryResourceVersion()},
            {"drawing", definition.at("drawing")},
            {"features", updatedFeatures},
            {"ends", definition.at("ends")}
        });
    };
    const auto saved = apply(features);
    ASSERT_TRUE(saved.OK) << saved.Error;
    const auto replayedPart = getPart();
    ASSERT_TRUE(replayedPart);
    const auto replayedBRep = scene.Resources().Get<BRepModel>(
        replayedPart->GetManufacturingGeometryResourceID(),
        replayedPart->GetManufacturingGeometryResourceVersion());
    ASSERT_TRUE(replayedBRep);
    const auto replayedShape = iCAX::OpenCascade::BuildOpenCascadeShape(*replayedBRep);
    ASSERT_TRUE(replayedShape.bOK);
    ASSERT_FALSE(replayedShape.Shape.IsNull());
    ASSERT_TRUE(BRepCheck_Analyzer(replayedShape.Shape).IsValid());
    const auto replayError = RelativeSymmetricDifference(originalShape.Shape, replayedShape.Shape);
    EXPECT_LE(replayError, 1.0e-8);

    // An editable recipe must move a cutter, not merely preserve the imported solid.
    auto movedFeatures = features;
    auto movedHole = movedFeatures.front().To<ObjectMap>();
    const auto movedStation = movedHole.at("station").To<double>() + 1.0;
    const auto radius = movedHole.at("diameter").To<double>() / 2.0;
    ASSERT_GT(movedStation - radius, 0.0);
    ASSERT_LT(movedStation + radius, replayedPart->GetLength());
    movedHole["station"] = movedStation;
    movedFeatures.front() = movedHole;
    const auto moved = apply(movedFeatures);
    ASSERT_TRUE(moved.OK) << moved.Error;
    const auto movedPart = getPart();
    ASSERT_TRUE(movedPart);
    const auto movedBRep = scene.Resources().Get<BRepModel>(
        movedPart->GetManufacturingGeometryResourceID(),
        movedPart->GetManufacturingGeometryResourceVersion());
    ASSERT_TRUE(movedBRep);
    const auto movedShape = iCAX::OpenCascade::BuildOpenCascadeShape(*movedBRep);
    ASSERT_TRUE(movedShape.bOK);
    ASSERT_FALSE(movedShape.Shape.IsNull());
    ASSERT_TRUE(BRepCheck_Analyzer(movedShape.Shape).IsValid());
    const auto movedError = RelativeSymmetricDifference(originalShape.Shape, movedShape.Shape);
    EXPECT_TRUE(std::isfinite(movedError));
    EXPECT_GT(movedError, 1.0e-8);
    const auto movedDrawing = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
    ASSERT_TRUE(movedDrawing.OK) << movedDrawing.Error;
    const auto storedFeatures = movedDrawing.Payload.at("definition").To<ObjectMap>()
        .at("features").To<VariantArray>();
    ASSERT_EQ(storedFeatures.size(), 4u);
    EXPECT_DOUBLE_EQ(storedFeatures.front().To<ObjectMap>().at("station").To<double>(), movedStation);

    const auto restored = apply(features);
    ASSERT_TRUE(restored.OK) << restored.Error;
    const auto restoredPart = getPart();
    ASSERT_TRUE(restoredPart);
    const auto restoredBRep = scene.Resources().Get<BRepModel>(
        restoredPart->GetManufacturingGeometryResourceID(),
        restoredPart->GetManufacturingGeometryResourceVersion());
    ASSERT_TRUE(restoredBRep);
    const auto restoredShape = iCAX::OpenCascade::BuildOpenCascadeShape(*restoredBRep);
    ASSERT_TRUE(restoredShape.bOK);
    ASSERT_FALSE(restoredShape.Shape.IsNull());
    ASSERT_TRUE(BRepCheck_Analyzer(restoredShape.Shape).IsValid());
    const auto restoredError = RelativeSymmetricDifference(originalShape.Shape, restoredShape.Shape);
    EXPECT_LE(restoredError, 1.0e-8);
    std::cout << "[imported-p1044-hole-recovery] editorReplayError=" << replayError
              << " movedHoleDifference=" << movedError
              << " restoredReplayError=" << restoredError << std::endl;
}

TEST(ImportedPartHoleRecoverySDO, ManufacturedP1026CurvePocketsRecoverAndRemainEditable) {
    std::string stage = "import";
    const auto number = [](const iCAX::Data::Variant& value, const char* field) {
        const auto canonical = iCAX::TubeDesigner::CanonicalDrawingGeometry(value);
        if (!canonical.Is<double>() || !std::isfinite(canonical.To<double>()))
            throw std::invalid_argument(std::string("P1026 field must be a finite number: ") + field);
        return canonical.To<double>();
    };
    try {
    const auto source = std::filesystem::current_path() / "samples" / "structural-steel"
        / "vendors-angle-channel" / "unistrut-P1026-two-hole-angle.step";
    ASSERT_TRUE(std::filesystem::is_regular_file(source));
    Scene scene;
    const auto imported = Invoke(scene, "ImportNestingPart", {{"sourcePath", source.string()}});
    ASSERT_TRUE(imported.OK) << imported.Error;
    ASSERT_EQ(imported.Payload.at("profile").To<ObjectMap>().at("id").To<std::string>(), "angle");
    const auto id = imported.Payload.at("partEntityId").To<std::string>();
    const auto partId = iCAX::Data::uuid::from_string(id).value();
    const auto getPart = [&]() -> std::shared_ptr<iCAX::TubeDesigner::CManufacturingPartComponent> {
        const auto entity = scene.Database().GetEntity(partId);
        if (!entity) return {};
        return std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
            entity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
    };
    const auto originalPart = getPart();
    ASSERT_TRUE(originalPart);
    const auto originalResourceId = originalPart->GetManufacturingGeometryResourceID();
    const auto originalResourceVersion = originalPart->GetManufacturingGeometryResourceVersion();
    const auto originalBRep = scene.Resources().Get<BRepModel>(originalResourceId, originalResourceVersion);
    ASSERT_TRUE(originalBRep);
    const auto originalBytes = iCAX::GeometryData::Persistence::Serialize(*originalBRep);
    const auto originalShape = iCAX::OpenCascade::BuildOpenCascadeShape(*originalBRep);
    ASSERT_TRUE(originalShape.bOK);
    ASSERT_FALSE(originalShape.Shape.IsNull());
    ASSERT_TRUE(BRepCheck_Analyzer(originalShape.Shape).IsValid());

    stage = "recover";
    const auto recovered = Invoke(scene, "RecoverImportedPartDrawing", {
        {"partEntityId", id}, {"resourceVersion", originalResourceVersion}
    });
    ASSERT_TRUE(recovered.OK) << recovered.Error;
    ASSERT_TRUE(recovered.Payload.contains("relativeSymmetricDifference"));
    EXPECT_LE(number(recovered.Payload.at("relativeSymmetricDifference"), "relativeSymmetricDifference"), 1.0e-8);
    const auto recoveredPart = getPart();
    ASSERT_TRUE(recoveredPart);
    EXPECT_EQ(recoveredPart->GetManufacturingGeometryResourceID(), originalResourceId);
    EXPECT_EQ(recoveredPart->GetManufacturingGeometryResourceVersion(), originalResourceVersion);
    const auto recoveredBRep = scene.Resources().Get<BRepModel>(
        recoveredPart->GetManufacturingGeometryResourceID(), recoveredPart->GetManufacturingGeometryResourceVersion());
    ASSERT_TRUE(recoveredBRep);
    EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*recoveredBRep), originalBytes);

    stage = "read definition";
    const auto fetched = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
    ASSERT_TRUE(fetched.OK) << fetched.Error;
    const auto definition = fetched.Payload.at("definition").To<ObjectMap>();
    ASSERT_TRUE(definition.contains("baseResourceId"));
    EXPECT_NE(definition.at("baseResourceId").To<std::string>(), originalResourceId);
    const auto features = definition.at("features").To<VariantArray>();
    ASSERT_EQ(features.size(), 7u);
    size_t circleCount = 0, pocketCount = 0, multiLoopCount = 0, splineCount = 0;
    size_t editedPocketIndex = features.size();
    for (size_t index = 0; index < features.size(); ++index) {
        stage = "inspect feature " + std::to_string(index);
        const auto feature = features[index].To<ObjectMap>();
        for (const auto* key : {"type", "toolRef", "opposite"})
            ASSERT_TRUE(feature.contains(key)) << "Missing saved feature field: " << key;
        const auto type = feature.at("type").To<std::string>();
        const auto toolRef = feature.at("toolRef").To<ObjectMap>();
        for (const auto* key : {"id", "version", "digest"})
            ASSERT_TRUE(toolRef.contains(key)) << "Missing saved tool reference field: " << key;
        EXPECT_EQ(toolRef.at("id").To<std::string>(), type);
        EXPECT_FALSE(toolRef.at("version").To<std::string>().empty());
        EXPECT_FALSE(toolRef.at("digest").To<std::string>().empty());
        EXPECT_FALSE(feature.at("opposite").To<bool>());
        if (type == "circle") {
            ++circleCount;
            EXPECT_NEAR(number(feature.at("diameter"), "diameter"), 14.0, 1.0e-6);
            continue;
        }
        ASSERT_EQ(type, "curve-pocket");
        ++pocketCount;
        for (const auto* key : {"blindHole", "cutDepth", "section"})
            ASSERT_TRUE(feature.contains(key)) << "Missing saved pocket field: " << key;
        EXPECT_TRUE(feature.at("blindHole").To<bool>());
        EXPECT_NEAR(number(feature.at("cutDepth"), "cutDepth"), 0.2, 1.0e-6);
        const auto section = feature.at("section").To<ObjectMap>();
        EXPECT_EQ(section.at("source").To<std::string>(), "recovered");
        const auto contours = section.at("profile").To<ObjectMap>().at("contours").To<VariantArray>();
        ASSERT_FALSE(contours.empty());
        ASSERT_LE(contours.size(), 2u);
        if (contours.size() == 2u) {
            ++multiLoopCount;
            if (editedPocketIndex == features.size()) editedPocketIndex = index;
        }
        for (const auto& contourValue : contours) {
            const auto contour = contourValue.To<ObjectMap>();
            EXPECT_EQ(contour.at("kind").To<std::string>(), "path");
            const auto segments = contour.at("segments").To<VariantArray>();
            ASSERT_FALSE(segments.empty());
            for (const auto& segmentValue : segments) {
                const auto segment = segmentValue.To<ObjectMap>();
                const auto kind = segment.at("kind").To<std::string>();
                if (kind == "line") continue;
                ASSERT_TRUE(kind == "bspline" || kind == "nurbs") << kind;
                ++splineCount;
                for (const auto* key : {"controlPoints", "degree", "knots", "multiplicities",
                                       "periodic", "startParameter", "endParameter"})
                    ASSERT_TRUE(segment.contains(key)) << "Missing exact spline field: " << key;
                const auto degree = number(segment.at("degree"), "degree");
                EXPECT_GE(degree, 1.0);
                EXPECT_DOUBLE_EQ(degree, std::floor(degree));
                EXPECT_GT(static_cast<double>(segment.at("controlPoints").To<VariantArray>().size()), degree);
                EXPECT_FALSE(segment.at("knots").To<VariantArray>().empty());
                EXPECT_EQ(segment.at("knots").To<VariantArray>().size(),
                    segment.at("multiplicities").To<VariantArray>().size());
                if (kind == "nurbs") {
                    ASSERT_TRUE(segment.contains("weights"));
                    EXPECT_EQ(segment.at("weights").To<VariantArray>().size(),
                        segment.at("controlPoints").To<VariantArray>().size());
                }
            }
        }
    }
    EXPECT_EQ(circleCount, 2u);
    EXPECT_EQ(pocketCount, 5u);
    EXPECT_EQ(multiLoopCount, 3u);
    EXPECT_GT(splineCount, 0u);
    ASSERT_LT(editedPocketIndex, features.size());

    const auto apply = [&](const VariantArray& updatedFeatures) {
        const auto currentPart = getPart();
        if (!currentPart) return Invocation{.Error = "Editor save removed the part"};
        return Invoke(scene, "ApplyPartDrawing", {
            {"partEntityId", id}, {"resourceId", currentPart->GetManufacturingGeometryResourceID()},
            {"resourceVersion", currentPart->GetManufacturingGeometryResourceVersion()},
            {"drawing", definition.at("drawing")}, {"features", updatedFeatures},
            {"ends", definition.at("ends")}
        });
    };
    const auto currentShape = [&]() -> TopoDS_Shape {
        const auto part = getPart();
        if (!part) return {};
        const auto brep = scene.Resources().Get<BRepModel>(
            part->GetManufacturingGeometryResourceID(), part->GetManufacturingGeometryResourceVersion());
        if (!brep) return {};
        const auto shape = iCAX::OpenCascade::BuildOpenCascadeShape(*brep);
        return shape.bOK ? shape.Shape : TopoDS_Shape{};
    };
    stage = "save original curve pockets";
    const auto saved = apply(features);
    ASSERT_TRUE(saved.OK) << saved.Error;
    const auto replayedShape = currentShape();
    ASSERT_FALSE(replayedShape.IsNull());
    ASSERT_TRUE(BRepCheck_Analyzer(replayedShape).IsValid());
    const auto replayError = RelativeSymmetricDifference(originalShape.Shape, replayedShape);
    EXPECT_LE(replayError, 1.0e-8);

    // Change an actual multi-loop cutter: the inner island and exact curves must
    // survive while its depth changes the manufactured material.
    auto deeperFeatures = features;
    auto deeperPocket = deeperFeatures[editedPocketIndex].To<ObjectMap>();
    deeperPocket["cutDepth"] = 0.3;
    deeperFeatures[editedPocketIndex] = deeperPocket;
    stage = "deepen multi-loop curve pocket";
    const auto deepened = apply(deeperFeatures);
    ASSERT_TRUE(deepened.OK) << deepened.Error;
    const auto deeperShape = currentShape();
    ASSERT_FALSE(deeperShape.IsNull());
    ASSERT_TRUE(BRepCheck_Analyzer(deeperShape).IsValid());
    const auto deeperError = RelativeSymmetricDifference(originalShape.Shape, deeperShape);
    EXPECT_TRUE(std::isfinite(deeperError));
    EXPECT_GT(deeperError, 1.0e-8);
    EXPECT_LT(MaterialVolume(deeperShape), MaterialVolume(replayedShape));
    const auto deeperDrawing = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
    ASSERT_TRUE(deeperDrawing.OK) << deeperDrawing.Error;
    const auto storedFeatures = deeperDrawing.Payload.at("definition").To<ObjectMap>()
        .at("features").To<VariantArray>();
    ASSERT_EQ(storedFeatures.size(), features.size());
    EXPECT_DOUBLE_EQ(number(storedFeatures[editedPocketIndex].To<ObjectMap>().at("cutDepth"), "saved cutDepth"), 0.3);
    for (size_t index = 0; index < features.size(); ++index) {
        const auto originalFeature = features[index].To<ObjectMap>();
        if (originalFeature.at("type").To<std::string>() == "curve-pocket")
            EXPECT_EQ(storedFeatures[index].To<ObjectMap>().at("section"), originalFeature.at("section"));
    }

    stage = "restore original curve pockets";
    const auto restored = apply(features);
    ASSERT_TRUE(restored.OK) << restored.Error;
    const auto restoredShape = currentShape();
    ASSERT_FALSE(restoredShape.IsNull());
    ASSERT_TRUE(BRepCheck_Analyzer(restoredShape).IsValid());
    const auto restoredError = RelativeSymmetricDifference(originalShape.Shape, restoredShape);
    EXPECT_LE(restoredError, 1.0e-8);

    // Malformed exact curves must fail atomically; neither the manufactured
    // resource nor the previously accepted editable recipe may change.
    const auto beforeInvalidPart = getPart();
    ASSERT_TRUE(beforeInvalidPart);
    const auto beforeInvalidId = beforeInvalidPart->GetManufacturingGeometryResourceID();
    const auto beforeInvalidVersion = beforeInvalidPart->GetManufacturingGeometryResourceVersion();
    const auto beforeInvalidBRep = scene.Resources().Get<BRepModel>(beforeInvalidId, beforeInvalidVersion);
    ASSERT_TRUE(beforeInvalidBRep);
    const auto beforeInvalidBytes = iCAX::GeometryData::Persistence::Serialize(*beforeInvalidBRep);
    const auto beforeInvalidDrawing = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
    ASSERT_TRUE(beforeInvalidDrawing.OK) << beforeInvalidDrawing.Error;
    for (const auto* brokenField : {"controlPoints", "knots"}) {
        stage = std::string("reject malformed ") + brokenField;
        SCOPED_TRACE(brokenField);
        auto invalidFeatures = features;
        auto pocket = invalidFeatures[editedPocketIndex].To<ObjectMap>();
        auto section = pocket.at("section").To<ObjectMap>();
        auto profile = section.at("profile").To<ObjectMap>();
        auto contours = profile.at("contours").To<VariantArray>();
        bool corrupted = false;
        for (auto& contourValue : contours) {
            auto contour = contourValue.To<ObjectMap>();
            auto segments = contour.at("segments").To<VariantArray>();
            for (auto& segmentValue : segments) {
                auto segment = segmentValue.To<ObjectMap>();
                const auto kind = segment.at("kind").To<std::string>();
                if (kind != "bspline" && kind != "nurbs") continue;
                segment[brokenField] = VariantArray{};
                segmentValue = segment;
                corrupted = true;
                break;
            }
            contour["segments"] = segments;
            contourValue = contour;
            if (corrupted) break;
        }
        ASSERT_TRUE(corrupted);
        profile["contours"] = contours;
        section["profile"] = profile;
        pocket["section"] = section;
        invalidFeatures[editedPocketIndex] = pocket;
        const auto invalid = apply(invalidFeatures);
        EXPECT_FALSE(invalid.OK);
        EXPECT_FALSE(invalid.Error.empty());
        const auto afterInvalidPart = getPart();
        ASSERT_TRUE(afterInvalidPart);
        EXPECT_EQ(afterInvalidPart->GetManufacturingGeometryResourceID(), beforeInvalidId);
        EXPECT_EQ(afterInvalidPart->GetManufacturingGeometryResourceVersion(), beforeInvalidVersion);
        const auto afterInvalidBRep = scene.Resources().Get<BRepModel>(
            afterInvalidPart->GetManufacturingGeometryResourceID(), afterInvalidPart->GetManufacturingGeometryResourceVersion());
        ASSERT_TRUE(afterInvalidBRep);
        EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*afterInvalidBRep), beforeInvalidBytes);
        const auto afterInvalidDrawing = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
        ASSERT_TRUE(afterInvalidDrawing.OK) << afterInvalidDrawing.Error;
        EXPECT_EQ(afterInvalidDrawing.Payload.at("definition"), beforeInvalidDrawing.Payload.at("definition"));
    }
    std::cout << "[imported-p1026-curve-recovery] pockets=" << pocketCount
              << " multiLoopPockets=" << multiLoopCount << " splines=" << splineCount
              << " editorReplayError=" << replayError << " deeperPocketDifference=" << deeperError
              << " restoredReplayError=" << restoredError << std::endl;
    } catch (const std::exception& error) {
        FAIL() << "P1026 public recovery stage '" << stage << "': " << error.what();
    }
}

TEST(ImportedPartHoleRecoverySDO, ManufacturedTekSpan60240ThroughCurvePocketsRecoverAndRemainEditable) {
    std::string stage = "import original solid";
    const auto number = [](const iCAX::Data::Variant& value, const char* field) {
        const auto canonical = iCAX::TubeDesigner::CanonicalDrawingGeometry(value);
        if (!canonical.Is<double>() || !std::isfinite(canonical.To<double>()))
            throw std::invalid_argument(std::string("TekSpan 60240 field must be a finite number: ") + field);
        return canonical.To<double>();
    };
    try {
        // The native assembly report records four solid occurrences. This is
        // its first original solid exported without geometric modification,
        // not a replacement reconstructed from sampled outline points.
        const auto source = std::filesystem::current_path() / "samples" / "structural-steel"
            / "recovery" / "report" / "solids"
            / "vendors-angle-channel__tekspan-60240-two-hole-angle.step-e419352c" / "solid-1.step";
        ASSERT_TRUE(std::filesystem::is_regular_file(source));
        Scene scene;
        const auto imported = Invoke(scene, "ImportNestingPart", {{"sourcePath", source.string()}});
        ASSERT_TRUE(imported.OK) << imported.Error;
        ASSERT_EQ(imported.Payload.at("profile").To<ObjectMap>().at("id").To<std::string>(), "angle");
        const auto id = imported.Payload.at("partEntityId").To<std::string>();
        const auto partId = iCAX::Data::uuid::from_string(id).value();
        const auto getPart = [&]() -> std::shared_ptr<iCAX::TubeDesigner::CManufacturingPartComponent> {
            const auto entity = scene.Database().GetEntity(partId);
            if (!entity) return {};
            return std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
                entity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
        };
        const auto originalPart = getPart();
        ASSERT_TRUE(originalPart);
        EXPECT_NEAR(originalPart->GetLength(), 70.0, 1.0e-6);
        const auto originalResourceId = originalPart->GetManufacturingGeometryResourceID();
        const auto originalResourceVersion = originalPart->GetManufacturingGeometryResourceVersion();
        const auto originalBRep = scene.Resources().Get<BRepModel>(originalResourceId, originalResourceVersion);
        ASSERT_TRUE(originalBRep);
        const auto originalBytes = iCAX::GeometryData::Persistence::Serialize(*originalBRep);
        const auto originalShape = iCAX::OpenCascade::BuildOpenCascadeShape(*originalBRep);
        ASSERT_TRUE(originalShape.bOK);
        ASSERT_FALSE(originalShape.Shape.IsNull());
        ASSERT_TRUE(BRepCheck_Analyzer(originalShape.Shape).IsValid());

        stage = "recover exact through curve pockets";
        const auto recovered = Invoke(scene, "RecoverImportedPartDrawing", {
            {"partEntityId", id}, {"resourceVersion", originalResourceVersion}
        });
        ASSERT_TRUE(recovered.OK) << recovered.Error;
        EXPECT_LE(number(recovered.Payload.at("relativeSymmetricDifference"), "relativeSymmetricDifference"), 1.0e-8);
        const auto recoveredPart = getPart();
        ASSERT_TRUE(recoveredPart);
        EXPECT_EQ(recoveredPart->GetManufacturingGeometryResourceID(), originalResourceId);
        EXPECT_EQ(recoveredPart->GetManufacturingGeometryResourceVersion(), originalResourceVersion);
        const auto recoveredBRep = scene.Resources().Get<BRepModel>(
            recoveredPart->GetManufacturingGeometryResourceID(), recoveredPart->GetManufacturingGeometryResourceVersion());
        ASSERT_TRUE(recoveredBRep);
        EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*recoveredBRep), originalBytes);

        stage = "read exact through-hole definition";
        const auto fetched = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
        ASSERT_TRUE(fetched.OK) << fetched.Error;
        const auto definition = fetched.Payload.at("definition").To<ObjectMap>();
        EXPECT_NE(definition.at("baseResourceId").To<std::string>(), originalResourceId);
        const auto features = definition.at("features").To<VariantArray>();
        ASSERT_EQ(features.size(), 6u);
        std::vector<size_t> pocketIndices;
        size_t circleCount = 0;
        bool rightPocket = false;
        bool bottomPocket = false;
        for (size_t index = 0; index < features.size(); ++index) {
            const auto feature = features[index].To<ObjectMap>();
            ASSERT_TRUE(feature.contains("type"));
            if (feature.at("type").To<std::string>() == "circle") {
                ++circleCount;
                continue;
            }
            pocketIndices.push_back(index);
            for (const auto* key : {"type", "toolRef", "toolTarget", "recordKind", "blindHole", "opposite", "section", "station", "face"})
                ASSERT_TRUE(feature.contains(key)) << "Missing saved through-hole field: " << key;
            EXPECT_EQ(feature.at("type").To<std::string>(), "curve-pocket");
            EXPECT_EQ(feature.at("toolTarget").To<std::string>(), "side");
            EXPECT_EQ(feature.at("recordKind").To<std::string>(), "profile");
            EXPECT_FALSE(feature.at("blindHole").To<bool>());
            EXPECT_FALSE(feature.at("opposite").To<bool>());
            const auto face = feature.at("face").To<std::string>();
            rightPocket |= face == "right";
            bottomPocket |= face == "bottom";
            const auto toolRef = feature.at("toolRef").To<ObjectMap>();
            EXPECT_EQ(toolRef.at("id").To<std::string>(), "curve-pocket");
            EXPECT_FALSE(toolRef.at("version").To<std::string>().empty());
            EXPECT_FALSE(toolRef.at("digest").To<std::string>().empty());
            const auto section = feature.at("section").To<ObjectMap>();
            EXPECT_EQ(section.at("source").To<std::string>(), "recovered");
            const auto contours = section.at("profile").To<ObjectMap>().at("contours").To<VariantArray>();
            ASSERT_EQ(contours.size(), 1u);
            const auto contour = contours.front().To<ObjectMap>();
            EXPECT_EQ(contour.at("kind").To<std::string>(), "path");
            EXPECT_FALSE(contour.contains("points"));
            const auto segments = contour.at("segments").To<VariantArray>();
            ASSERT_FALSE(segments.empty());
            size_t arcCount = 0;
            size_t lineCount = 0;
            for (const auto& segmentValue : segments) {
                const auto segment = segmentValue.To<ObjectMap>();
                const auto kind = segment.at("kind").To<std::string>();
                EXPECT_TRUE(kind == "line" || kind == "arc") << kind;
                arcCount += kind == "arc";
                lineCount += kind == "line";
                EXPECT_FALSE(segment.contains("points"));
            }
            EXPECT_EQ(arcCount, 4u);
            EXPECT_EQ(lineCount, 2u);
        }
        EXPECT_EQ(circleCount, 4u);
        ASSERT_EQ(pocketIndices.size(), 2u);
        EXPECT_TRUE(rightPocket);
        EXPECT_TRUE(bottomPocket);

        const auto apply = [&](const VariantArray& updatedFeatures) {
            const auto currentPart = getPart();
            if (!currentPart) return Invocation{.Error = "Editor save removed the part"};
            return Invoke(scene, "ApplyPartDrawing", {
                {"partEntityId", id}, {"resourceId", currentPart->GetManufacturingGeometryResourceID()},
                {"resourceVersion", currentPart->GetManufacturingGeometryResourceVersion()},
                {"drawing", definition.at("drawing")}, {"features", updatedFeatures},
                {"ends", definition.at("ends")}
            });
        };
        const auto currentShape = [&]() -> TopoDS_Shape {
            const auto part = getPart();
            if (!part) return {};
            const auto brep = scene.Resources().Get<BRepModel>(
                part->GetManufacturingGeometryResourceID(), part->GetManufacturingGeometryResourceVersion());
            if (!brep) return {};
            const auto shape = iCAX::OpenCascade::BuildOpenCascadeShape(*brep);
            return shape.bOK ? shape.Shape : TopoDS_Shape{};
        };
        stage = "save through curve pockets";
        const auto saved = apply(features);
        ASSERT_TRUE(saved.OK) << saved.Error;
        const auto replayedShape = currentShape();
        ASSERT_FALSE(replayedShape.IsNull());
        ASSERT_TRUE(BRepCheck_Analyzer(replayedShape).IsValid());
        const auto replayError = RelativeSymmetricDifference(originalShape.Shape, replayedShape);
        EXPECT_LE(replayError, 1.0e-8);

        // Change placement while keeping the exact line/curve definition. An
        // imported BRep masquerading as an editable recipe cannot pass this.
        for (const auto pocketIndex : pocketIndices) {
            auto movedFeatures = features;
            auto movedPocket = movedFeatures[pocketIndex].To<ObjectMap>();
            const auto face = movedPocket.at("face").To<std::string>();
            const auto movedStation = number(movedPocket.at("station"), "station") + 1.0;
            ASSERT_GT(movedStation, 0.0);
            ASSERT_LT(movedStation, originalPart->GetLength());
            movedPocket["station"] = movedStation;
            movedFeatures[pocketIndex] = movedPocket;
            stage = "move " + face + " through curve pocket";
            const auto moved = apply(movedFeatures);
            ASSERT_TRUE(moved.OK) << moved.Error;
            const auto movedShape = currentShape();
            ASSERT_FALSE(movedShape.IsNull());
            ASSERT_TRUE(BRepCheck_Analyzer(movedShape).IsValid());
            const auto movedError = RelativeSymmetricDifference(originalShape.Shape, movedShape);
            EXPECT_TRUE(std::isfinite(movedError));
            EXPECT_GT(movedError, 1.0e-8);
            const auto movedDrawing = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
            ASSERT_TRUE(movedDrawing.OK) << movedDrawing.Error;
            const auto storedFeatures = movedDrawing.Payload.at("definition").To<ObjectMap>()
                .at("features").To<VariantArray>();
            ASSERT_EQ(storedFeatures.size(), features.size());
            for (size_t index = 0; index < features.size(); ++index) {
                const auto stored = storedFeatures[index].To<ObjectMap>();
                const auto originalFeature = features[index].To<ObjectMap>();
                const auto expectedStation = index == pocketIndex ? movedStation
                    : number(originalFeature.at("station"), "original station");
                EXPECT_DOUBLE_EQ(number(stored.at("station"), "saved station"), expectedStation);
                if (originalFeature.at("type").To<std::string>() == "curve-pocket")
                    EXPECT_EQ(stored.at("section"), originalFeature.at("section"));
                EXPECT_FALSE(stored.at("blindHole").To<bool>());
                EXPECT_FALSE(stored.at("opposite").To<bool>());
            }

            stage = "restore " + face + " through curve pocket";
            const auto restored = apply(features);
            ASSERT_TRUE(restored.OK) << restored.Error;
            const auto restoredShape = currentShape();
            ASSERT_FALSE(restoredShape.IsNull());
            ASSERT_TRUE(BRepCheck_Analyzer(restoredShape).IsValid());
            const auto restoredError = RelativeSymmetricDifference(originalShape.Shape, restoredShape);
            EXPECT_LE(restoredError, 1.0e-8);
            std::cout << "[imported-tekspan60240-curve-recovery] pockets=" << pocketIndices.size()
                      << " face=" << face << " editorReplayError=" << replayError
                      << " movedPocketDifference=" << movedError
                      << " restoredReplayError=" << restoredError << std::endl;
        }
        const auto retainedOriginal = scene.Resources().Get<BRepModel>(originalResourceId, originalResourceVersion);
        ASSERT_TRUE(retainedOriginal);
        EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*retainedOriginal), originalBytes);
    } catch (const std::exception& error) {
        FAIL() << "TekSpan 60240 public recovery stage '" << stage << "': " << error.what();
    }
}

TEST(ImportedPartHoleRecoverySDO, ManufacturedTekSpan60235EndTrianglesRecoverAndRemainEditable) {
    std::string stage = "import original saddle bracket solid";
    const auto number = [](const iCAX::Data::Variant& value, const char* field) {
        const auto canonical = iCAX::TubeDesigner::CanonicalDrawingGeometry(value);
        if (!canonical.Is<double>() || !std::isfinite(canonical.To<double>()))
            throw std::invalid_argument(std::string("TekSpan 60235 field must be a finite number: ") + field);
        return canonical.To<double>();
    };
    try {
        // The supplier assembly's first original solid, exported without
        // modifying geometry. The two formerly residual end cuts each remove
        // a 10 x 10 right triangle through a 6 mm flange (300 mm3 per side).
        const auto source = std::filesystem::current_path() / "samples" / "structural-steel"
            / "recovery" / "report" / "solids"
            / "vendors-angle-channel__tekspan-60235-saddle-bracket.step-85659452" / "solid-1.step";
        ASSERT_TRUE(std::filesystem::is_regular_file(source));
        Scene scene;
        const auto imported = Invoke(scene, "ImportNestingPart", {{"sourcePath", source.string()}});
        ASSERT_TRUE(imported.OK) << imported.Error;
        ASSERT_EQ(imported.Payload.at("profile").To<ObjectMap>().at("id").To<std::string>(), "channel");
        const auto id = imported.Payload.at("partEntityId").To<std::string>();
        const auto partId = iCAX::Data::uuid::from_string(id).value();
        const auto getPart = [&]() -> std::shared_ptr<iCAX::TubeDesigner::CManufacturingPartComponent> {
            const auto entity = scene.Database().GetEntity(partId);
            if (!entity) return {};
            return std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
                entity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
        };
        const auto originalPart = getPart();
        ASSERT_TRUE(originalPart);
        EXPECT_NEAR(originalPart->GetLength(), 230.0, 1.0e-6);
        const auto originalResourceId = originalPart->GetManufacturingGeometryResourceID();
        const auto originalResourceVersion = originalPart->GetManufacturingGeometryResourceVersion();
        const auto originalBRep = scene.Resources().Get<BRepModel>(originalResourceId, originalResourceVersion);
        ASSERT_TRUE(originalBRep);
        const auto originalBytes = iCAX::GeometryData::Persistence::Serialize(*originalBRep);
        const auto originalShape = iCAX::OpenCascade::BuildOpenCascadeShape(*originalBRep);
        ASSERT_TRUE(originalShape.bOK);
        ASSERT_FALSE(originalShape.Shape.IsNull());
        ASSERT_TRUE(BRepCheck_Analyzer(originalShape.Shape).IsValid());

        stage = "recover complete saddle bracket";
        const auto recovered = Invoke(scene, "RecoverImportedPartDrawing", {
            {"partEntityId", id}, {"resourceVersion", originalResourceVersion}
        });
        ASSERT_TRUE(recovered.OK) << recovered.Error;
        EXPECT_LE(number(recovered.Payload.at("relativeSymmetricDifference"), "relativeSymmetricDifference"), 1.0e-8);
        const auto recoveredPart = getPart();
        ASSERT_TRUE(recoveredPart);
        EXPECT_EQ(recoveredPart->GetManufacturingGeometryResourceID(), originalResourceId);
        EXPECT_EQ(recoveredPart->GetManufacturingGeometryResourceVersion(), originalResourceVersion);
        const auto recoveredBRep = scene.Resources().Get<BRepModel>(originalResourceId, originalResourceVersion);
        ASSERT_TRUE(recoveredBRep);
        EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*recoveredBRep), originalBytes);

        stage = "read exact triangular end cuts";
        const auto fetched = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
        ASSERT_TRUE(fetched.OK) << fetched.Error;
        const auto definition = fetched.Payload.at("definition").To<ObjectMap>();
        EXPECT_NE(definition.at("baseResourceId").To<std::string>(), originalResourceId);
        const auto features = definition.at("features").To<VariantArray>();
        std::vector<size_t> triangleIndices;
        bool topTriangle = false, bottomTriangle = false;
        for (size_t index = 0; index < features.size(); ++index) {
            const auto feature = features[index].To<ObjectMap>();
            if (feature.at("type").To<std::string>() != "curve-pocket"
                || std::abs(number(feature.at("station"), "station") - 225.0) > 1.0e-6
                || std::abs(number(feature.at("offset"), "offset") + 57.0) > 1.0e-6)
                continue;
            triangleIndices.push_back(index);
            EXPECT_EQ(feature.at("toolTarget").To<std::string>(), "side");
            EXPECT_EQ(feature.at("recordKind").To<std::string>(), "profile");
            EXPECT_FALSE(feature.at("blindHole").To<bool>());
            EXPECT_FALSE(feature.at("opposite").To<bool>());
            const auto face = feature.at("face").To<std::string>();
            EXPECT_TRUE(face == "top" || face == "bottom");
            topTriangle |= face == "top";
            bottomTriangle |= face == "bottom";
            const auto toolRef = feature.at("toolRef").To<ObjectMap>();
            EXPECT_EQ(toolRef.at("id").To<std::string>(), "curve-pocket");
            EXPECT_FALSE(toolRef.at("version").To<std::string>().empty());
            EXPECT_FALSE(toolRef.at("digest").To<std::string>().empty());
            const auto section = feature.at("section").To<ObjectMap>();
            EXPECT_EQ(section.at("source").To<std::string>(), "recovered");
            const auto contours = section.at("profile").To<ObjectMap>().at("contours").To<VariantArray>();
            ASSERT_EQ(contours.size(), 1u);
            const auto contour = contours.front().To<ObjectMap>();
            EXPECT_EQ(contour.at("kind").To<std::string>(), "path");
            EXPECT_FALSE(contour.contains("points"));
            const auto segments = contour.at("segments").To<VariantArray>();
            ASSERT_EQ(segments.size(), 3u);
            double twiceArea = 0.0;
            for (const auto& segmentValue : segments) {
                const auto segment = segmentValue.To<ObjectMap>();
                ASSERT_EQ(segment.at("kind").To<std::string>(), "line");
                EXPECT_FALSE(segment.contains("points"));
                const auto start = segment.at("start").To<VariantArray>();
                const auto end = segment.at("end").To<VariantArray>();
                ASSERT_EQ(start.size(), 2u);
                ASSERT_EQ(end.size(), 2u);
                const auto x0 = number(start[0], "start.x"), y0 = number(start[1], "start.y");
                const auto x1 = number(end[0], "end.x"), y1 = number(end[1], "end.y");
                twiceArea += x0 * y1 - x1 * y0;
                EXPECT_NEAR(std::abs(x0), 5.0, 1.0e-6);
                EXPECT_NEAR(std::abs(y0), 5.0, 1.0e-6);
            }
            EXPECT_NEAR(std::abs(twiceArea) / 2.0, 50.0, 1.0e-6);
        }
        ASSERT_EQ(triangleIndices.size(), 2u);
        EXPECT_TRUE(topTriangle);
        EXPECT_TRUE(bottomTriangle);

        const auto apply = [&](const VariantArray& updatedFeatures) {
            const auto currentPart = getPart();
            if (!currentPart) return Invocation{.Error = "Editor save removed the part"};
            return Invoke(scene, "ApplyPartDrawing", {
                {"partEntityId", id}, {"resourceId", currentPart->GetManufacturingGeometryResourceID()},
                {"resourceVersion", currentPart->GetManufacturingGeometryResourceVersion()},
                {"drawing", definition.at("drawing")}, {"features", updatedFeatures},
                {"ends", definition.at("ends")}
            });
        };
        const auto currentShape = [&]() -> TopoDS_Shape {
            const auto part = getPart();
            if (!part) return {};
            const auto brep = scene.Resources().Get<BRepModel>(
                part->GetManufacturingGeometryResourceID(), part->GetManufacturingGeometryResourceVersion());
            if (!brep) return {};
            const auto shape = iCAX::OpenCascade::BuildOpenCascadeShape(*brep);
            return shape.bOK ? shape.Shape : TopoDS_Shape{};
        };
        stage = "save complete saddle bracket";
        const auto saved = apply(features);
        ASSERT_TRUE(saved.OK) << saved.Error;
        const auto savedShape = currentShape();
        ASSERT_FALSE(savedShape.IsNull());
        ASSERT_TRUE(BRepCheck_Analyzer(savedShape).IsValid());
        const auto replayError = RelativeSymmetricDifference(originalShape.Shape, savedShape);
        EXPECT_LE(replayError, 1.0e-8);

        stage = "move one newly recovered triangle";
        const auto editedIndex = triangleIndices.front();
        auto editedFeatures = features;
        auto editedTriangle = editedFeatures[editedIndex].To<ObjectMap>();
        const auto editedStation = number(editedTriangle.at("station"), "station") - 1.0;
        editedTriangle["station"] = editedStation;
        editedFeatures[editedIndex] = editedTriangle;
        const auto edited = apply(editedFeatures);
        ASSERT_TRUE(edited.OK) << edited.Error;
        const auto editedShape = currentShape();
        ASSERT_FALSE(editedShape.IsNull());
        ASSERT_TRUE(BRepCheck_Analyzer(editedShape).IsValid());
        const auto editedDifference = RelativeSymmetricDifference(originalShape.Shape, editedShape);
        EXPECT_TRUE(std::isfinite(editedDifference));
        EXPECT_GT(editedDifference, 1.0e-8);
        const auto editedDrawing = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
        ASSERT_TRUE(editedDrawing.OK) << editedDrawing.Error;
        const auto storedFeatures = editedDrawing.Payload.at("definition").To<ObjectMap>().at("features").To<VariantArray>();
        ASSERT_EQ(storedFeatures.size(), features.size());
        for (size_t index = 0; index < features.size(); ++index) {
            const auto stored = storedFeatures[index].To<ObjectMap>();
            const auto expected = editedFeatures[index].To<ObjectMap>();
            EXPECT_DOUBLE_EQ(number(stored.at("station"), "saved station"), number(expected.at("station"), "expected station"));
            if (expected.at("type").To<std::string>() == "curve-pocket")
                EXPECT_EQ(stored.at("section"), expected.at("section"));
        }

        stage = "restore the original triangle";
        const auto restored = apply(features);
        ASSERT_TRUE(restored.OK) << restored.Error;
        const auto restoredShape = currentShape();
        ASSERT_FALSE(restoredShape.IsNull());
        ASSERT_TRUE(BRepCheck_Analyzer(restoredShape).IsValid());
        const auto restoredError = RelativeSymmetricDifference(originalShape.Shape, restoredShape);
        EXPECT_LE(restoredError, 1.0e-8);
        const auto retainedOriginal = scene.Resources().Get<BRepModel>(originalResourceId, originalResourceVersion);
        ASSERT_TRUE(retainedOriginal);
        EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*retainedOriginal), originalBytes);
        std::cout << "[imported-tekspan60235-triangle-recovery] features=" << features.size()
                  << " triangles=" << triangleIndices.size() << " editorReplayError=" << replayError
                  << " movedTriangleDifference=" << editedDifference << " restoredReplayError=" << restoredError
                  << " originalBRepUnchanged=true" << std::endl;
    } catch (const std::exception& error) {
        FAIL() << "TekSpan 60235 public recovery stage '" << stage << "': " << error.what();
    }
}

TEST(ImportedPartHoleRecoverySDO, ManufacturedP1033RejectsUnverifiedReplayWithoutChangingOriginal) {
    std::string stage = "import";
    try {
        // The supplier-original STEP contains invalid curve-on-surface data.
        // Recognizable local features do not prove a valid independent replay.
        const auto source = std::filesystem::current_path() / "samples" / "structural-steel"
            / "vendors-angle-channel" / "unistrut-P1033-three-hole-t-angle.step";
        ASSERT_TRUE(std::filesystem::is_regular_file(source));
        Scene scene;
        const auto imported = Invoke(scene, "ImportNestingPart", {{"sourcePath", source.string()}});
        ASSERT_TRUE(imported.OK) << imported.Error;
        ASSERT_EQ(imported.Payload.at("profile").To<ObjectMap>().at("id").To<std::string>(), "angle");
        const auto id = imported.Payload.at("partEntityId").To<std::string>();
        const auto partId = iCAX::Data::uuid::from_string(id).value();
        const auto getPart = [&]() -> std::shared_ptr<iCAX::TubeDesigner::CManufacturingPartComponent> {
            const auto entity = scene.Database().GetEntity(partId);
            if (!entity) return {};
            return std::dynamic_pointer_cast<iCAX::TubeDesigner::CManufacturingPartComponent>(
                entity->GetComponent(iCAX::TubeDesigner::CManufacturingPartComponent::S_ClassName));
        };
        const auto originalPart = getPart();
        ASSERT_TRUE(originalPart);
        const auto originalResourceId = originalPart->GetManufacturingGeometryResourceID();
        const auto originalResourceVersion = originalPart->GetManufacturingGeometryResourceVersion();
        const auto originalBRep = scene.Resources().Get<BRepModel>(originalResourceId, originalResourceVersion);
        ASSERT_TRUE(originalBRep);
        const auto originalBytes = iCAX::GeometryData::Persistence::Serialize(*originalBRep);
        const auto originalEntity = scene.Database().GetEntity(partId);
        ASSERT_TRUE(originalEntity);
        ASSERT_FALSE(originalEntity->GetComponent(iCAX::TubeDesigner::CPartDrawingComponent::S_ClassName));

        stage = "reject unverified recovery";
        const auto recovered = Invoke(scene, "RecoverImportedPartDrawing", {
            {"partEntityId", id}, {"resourceVersion", originalResourceVersion}
        });
        ASSERT_FALSE(recovered.OK) << "P1033 must remain unsupported while independent replay is invalid";
        EXPECT_FALSE(recovered.Error.empty());
        EXPECT_NE(recovered.Error.find("原始零件未改变"), std::string::npos) << recovered.Error;
        EXPECT_NE(recovered.Error.find("回放"), std::string::npos) << recovered.Error;
        const auto currentPart = getPart();
        ASSERT_TRUE(currentPart);
        EXPECT_EQ(currentPart->GetManufacturingGeometryResourceID(), originalResourceId);
        EXPECT_EQ(currentPart->GetManufacturingGeometryResourceVersion(), originalResourceVersion);
        const auto currentBRep = scene.Resources().Get<BRepModel>(
            currentPart->GetManufacturingGeometryResourceID(), currentPart->GetManufacturingGeometryResourceVersion());
        ASSERT_TRUE(currentBRep);
        EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*currentBRep), originalBytes);
        const auto currentEntity = scene.Database().GetEntity(partId);
        ASSERT_TRUE(currentEntity);
        EXPECT_FALSE(currentEntity->GetComponent(iCAX::TubeDesigner::CPartDrawingComponent::S_ClassName));

        stage = "confirm no editable definition";
        const auto fetched = Invoke(scene, "GetPartDrawing", {{"partEntityId", id}});
        EXPECT_FALSE(fetched.OK);
        EXPECT_NE(fetched.Error.find("尚无三维编辑定义"), std::string::npos) << fetched.Error;
        EXPECT_FALSE(fetched.Payload.contains("definition"));
        std::cout << "[imported-p1033-recovery-rejected] unsupported=" << recovered.Error
                  << " originalResourceUnchanged=true originalBRepUnchanged=true editableDefinition=false"
                  << std::endl;
    } catch (const std::exception& error) {
        FAIL() << "P1033 rejection stage '" << stage << "': " << error.what();
    }
}
}
