#include "pch.h"
#include <IGESControl_Writer.hxx>
#include <FiveAxisCAM/FiveAxisCAM.h>
#include <FiveAxisCAM/FiveAxisIntentProjection.h>
#include <mutex>
#include <set>
#include <thread>
#include <condition_variable>
#include <FiveAxisCAM/FiveAxisRapidParallel.h>
#include <FiveAxisCAM/FiveAxisComponents.h>
#include <FiveAxisCAM/FiveAxisOrdering.h>
#include <FiveAxisCAM/FiveAxisMachinePlanPolicy.h>
#include <ApplicationContext/IApplicationContext.h>
#include <ProductContext/IProductContext.h>
#include <ProjectContext/IProjectContext.h>
#include <Machine/MachineMotion.h>
#include <Machine/MachineDescriptionLoader.h>
#include <Machine/MachineTransformConstraints.h>
#include <Data/VariantSerializer.h>
#include <PDO/IPDOHub.h>
#include <PDO/PDOLease.h>
#include <FiveAxisCAM/Generated/MachineIKTarget_generated.h>
#include <FiveAxisCAM/Generated/MachineIKResult_generated.h>
#include <FiveAxisCAM/Generated/MachineJogControl_generated.h>
#include <FiveAxisCAM/Generated/MachineJogResult_generated.h>
#include <FiveAxisCAM/Generated/TaskProgress_generated.h>
#include <Database/MetaRegistrationCatalog.h>
#include <Database/IChecker.h>
#include <GeometryData/BRepPersistence.h>
#include <GeometryServices/BRepDisplayService.h>
#include <ProjectFile/ProjectFile.h>
#include <Resources/BinaryResource.h>
#include <Resources/ResourceLibrary.h>
#include <Resources/ResourcePersistenceRegistrationCatalog.h>
#include <Resources/ResourceLoaderRegistry.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <SDO/SDORegistrationCatalog.h>
#include <SDO/SDORegistry.h>
#include <SDO/SDOChannel.h>
#include <SDO/SDOInvoker.h>
#include <SDO/SDOText.h>
#include <EntityViewRuntime/ViewSet.h>
#include <Resources/FlatBufferResource.h>
#include <Resources/ResourceAccess.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <iostream>
#include <numbers>
#include <Transform/TransformComponent.h>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <OpenCascadeResourceImport/GeometryFeatureAnalysisService.h>
#include <OpenCascadeResourceImport/CurveEditingService.h>
#include <OpenCascadeResourceImport/CadDocumentImportService.h>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <IntentToolpath/IntentToolpath.h>
#include <IntentToolpath/EditableToolpathInternal.h>
#include <bcrypt.h>
#include <ProcessPath/ProcessPathResources.h>
#include <GeometryServices/BoundaryLeadService.h>
#include <GeometryServices/OffsetCurveTraversal.h>
#include <GeometryServices/SurfaceTravelService.h>
#include <GeometryServices/RapidPathGeometryService.h>
#include <GeometryServices/RapidPathPlanningService.h>
#include <GeometryServices/CollisionDistanceService.h>
#include <GeometryServices/ConfigurationPathPlanningService.h>
#include <GeometryServices/PathOrientationPlanningService.h>
#include <GeometryServices/MachineKinematicsService.h>
#include <GeometryServices/MachineMotionPlanningService.h>
#include <future>
#include <GeometryServices/RigidMotionCollisionService.h>
#include <Geom_BSplineSurface.hxx>
#include <GeomAPI.hxx>
#include <OpenCascadeResourceImport/CadBRepImportService.h>
#include <OpenCascadeResourceImport/SectionPlateService.h>
#include <OpenCascadeResourceImport/SectionPlateEditingService.h>
#include <OpenCascadeResourceImport/TubeCadJson.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>
#include <gp_Cylinder.hxx>
#include <STEPControl_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <gp_Quaternion.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepTools.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <BndLib_Add3dCurve.hxx>
#include <GeomAdaptor_Curve.hxx>

namespace five_axis_tests
{
    using namespace iCAX::FiveAxisCAM;
    struct File final
    {
        std::filesystem::path Path = std::filesystem::temp_directory_path() /
            (L"五轴导入-" + std::filesystem::path(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())).wstring() + L".step");
        File(const TopoDS_Shape& shape=BRepPrimAPI_MakeSphere(30).Shape())
        {
            STEPControl_Writer writer;
            if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) throw std::runtime_error("Fixture transfer failed");
            std::ofstream stream(Path, std::ios::binary);
            if (writer.WriteStream(stream) != IFSelect_RetDone) throw std::runtime_error("Fixture write failed");
        }
        ~File() { std::error_code error; std::filesystem::remove(Path,error); }
        Text Utf8() const { const auto text=Path.u8string(); return {reinterpret_cast<const char*>(text.data()),text.size()}; }
    };
    Text Module(const wchar_t* name)
    {
        const auto handle = name ? LoadLibraryW(name) : GetModuleHandleW(nullptr);
        wchar_t path[32768]{};
        if (!handle || !GetModuleFileNameW(handle,path,32768))
            throw std::runtime_error("Native test module missing: "+(name?std::filesystem::path(name).string():Text("test executable"))+" (Windows "+std::to_string(GetLastError())+")");
        return std::filesystem::path(path).string();
    }
    class Scene final : public iCAX::Project::ISceneContext
    {
        ID id;
        std::string name="Five-axis native acceptance";
        std::shared_ptr<iCAX::PDO::IPDOHub> pdo;
        std::shared_ptr<iCAX::Database::IRepository> db;
        std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> loaders=std::make_shared<iCAX::Resource::CResourceLoaderRegistry>();
        iCAX::Resource::CResourceLibrary resources{loaders};
        mutable iCAX::Services::CServiceProvider services;
        std::unique_ptr<iCAX::View::CViewSet> views;
    public:
        ID Project;
        std::shared_ptr<iCAX::Application::IProductUserDataStore> FixtureLibraryStore;
        bool Closing = false;
        std::function<bool()> StopProbe;
        Scene(ID project = iCAX::Data::GenerateNewUUID(), ID scene = iCAX::Data::GenerateNewUUID(), bool initialize = true, bool machineImports=false, bool enablePDO=false)
            : id(scene), Project(project)
        {
            if(enablePDO)pdo=iCAX::PDO::GeneratePDOHub(iCAX::PDO::CPDOHubCreateInfo{});
            const std::vector<Text> modules{Module(L"Database.dll"), Module(L"FiveAxisCAM.dll"),Module(L"Transform.dll"),
                Module(L"RenderInteraction.dll"),Module(L"Machine.dll"),Module(L"CoalCollisionServices.dll"),Module(L"OmplPlanningServices.dll"),Module(L"DescartesPlanningServices.dll"),Module(L"MachineKinematicsServices.dll"),Module(L"MachineMotionPlanningServices.dll"),Module(L"OpenCascadeResourceImport.dll"),Module(L"OpenCascadeGeometryServices.dll"),Module(L"IntentToolpath.dll"),Module(nullptr)};
            auto registry=iCAX::Database::CreateMetaRegistry();
            iCAX::Database::CMetaRegistrationCatalog::ReplayByModulePaths(*registry,modules);
            db=iCAX::Database::GenerateRepository(id,registry);
            resources.SetScope(iCAX::Resource::MakeSceneResourceScope("icax","icax.five-axis-cam",Project,id));
            iCAX::Resource::CResourcePersistenceRegistrationCatalog::ReplayByModulePaths(resources,modules);
            if(machineImports){const auto importer=Module(L"STLResourceImport.dll");(void)Module(L"CamRuntime.dll");iCAX::Resource::CResourceLoaderRegistrationCatalog::ReplayByModulePaths(*loaders,{importer});iCAX::Resource::CResourcePersistenceRegistrationCatalog::ReplayByModulePaths(resources,{importer,Module(L"RenderData.dll")});}
            iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
            views=std::make_unique<iCAX::View::CViewSet>(*db,resources);
            if (initialize)
            {
                InitializeScene(*this);
            }
        }
        const ID& GetSceneID() const override { return id; }
        const ID& GetSceneChannelID() const override { return id; }
        const Text& GetSceneName() const override { return name; }
        iCAX::Interaction::CSDOEndpoint GetBackendSDOEndpoint() const override { return {}; }
        iCAX::Interaction::CSDOEndpoint GetFrontendSDOEndpoint() const override { return {}; }
        bool IsMainScene() const override { return true; }
        bool IsTransientScene() const override { return false; }
        iCAX::Database::IRepository& Database() override { return *db; }
        const iCAX::Database::IRepository& Database() const override { return *db; }
        iCAX::Resource::CResourceLibrary& Resources() override { return resources; }
        const iCAX::Resource::CResourceLibrary& Resources() const override { return resources; }
        bool HasPDOHub() const override { return static_cast<bool>(pdo); }
        iCAX::PDO::IPDOHub& PDOHub() override { if(!pdo)throw std::logic_error("No PDO in this native test");return *pdo; }
        const iCAX::PDO::IPDOHub& PDOHub() const override { if(!pdo)throw std::logic_error("No PDO in this native test");return *pdo; }
        iCAX::Services::CServiceProvider& Services() const override { return services; }
        bool IsStopRequested() const noexcept override { return Closing||(StopProbe&&StopProbe()); }
        bool HasViews() const override { return true; }
        iCAX::View::CViewSet& Views() const override { return *views; }
    };
    class Application final : public iCAX::Application::IApplicationContext
    {
        iCAX::Application::CApplicationDescriptor descriptor;
        iCAX::Application::CApplicationPaths paths;
        iCAX::Services::CServiceProvider services;
    public:
        const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override { return descriptor; }
        const iCAX::Application::CApplicationPaths& GetPaths() const override { return paths; }
        iCAX::Data::PropertyBag GetSettings() const override { return {}; }
        const iCAX::Services::CServiceProvider& Services() const override { return services; }
    };
    class Product final : public iCAX::Product::IProductContext
    {
        iCAX::Product::CProductDefinition definition;
        std::shared_ptr<iCAX::Application::IProductUserDataStore> store;
    public:
        Product(Text id="icax.five-axis-cam",std::shared_ptr<iCAX::Application::IProductUserDataStore> library={}) :store(std::move(library)) { definition.ProductID=id;if(id=="icax.five-axis-cam")definition.Capabilities["machineDefinition"]=Map{{"supportedFormats",Array{Map{{"formatId",Text("machine.urdf")},{"name",Text("URDF")},{"extensions",Array{Text(".urdf")}}},Map{{"formatId",Text("machine.sdf")},{"name",Text("SDF")},{"extensions",Array{Text(".sdf")}}}}}}; }
        const iCAX::Product::CProductDefinition& GetDefinition() const override { return definition; }
        const Text& GetProductID() const override { return definition.ProductID; }
        std::shared_ptr<iCAX::Application::IProductUserDataStore> GetUserDataStore() const override { return store; }
        iCAX::Product::CProductData GetProductData() const override { return {}; }
        iCAX::Services::CServiceProvider& GetServiceProvider() const override { throw std::logic_error("unused"); }
        iCAX::Database::IMetaRegistry& GetMetaRegistry() const override { throw std::logic_error("unused"); }
        iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override { throw std::logic_error("unused"); }
        iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override { throw std::logic_error("unused"); }
        iCAX::Interaction::CSDORegistry& GetSDORegistry() const override { throw std::logic_error("unused"); }
    };
    struct NativeSDOStatus
    {
        int Status;
        explicit NativeSDOStatus(int status):Status(status){}
        virtual ~NativeSDOStatus()=default;
    };
    struct NativeSDOInvalid final:std::invalid_argument,NativeSDOStatus
    {NativeSDOInvalid(const Text& message,int status):std::invalid_argument(message),NativeSDOStatus(status){}};
    struct NativeSDORuntime final:std::runtime_error,NativeSDOStatus
    {NativeSDORuntime(const Text& message,int status):std::runtime_error(message),NativeSDOStatus(status){}};
    Map InvokePayload(Scene* scene, const Text& group, const Text& method, const Text& json, Text productID="icax.five-axis-cam")
    {
        Application app; Product product(productID,scene?scene->FixtureLibraryStore:nullptr);
        auto registry=std::make_shared<iCAX::Interaction::CSDORegistry>();
        std::vector<Text> modules{Module(L"FiveAxisCAM.dll"),Module(L"EntityViewRuntime.dll")};
        if(group=="Machine"||group=="MachineDefinition")modules.push_back(Module(L"CamRuntime.dll"));
        iCAX::Interaction::CSDORegistrationCatalog::ReplayByModulePaths(*registry,modules);
        iCAX::Interaction::CInvocation call;
        call.Method=iCAX::Interaction::MakeSDOMethod(group,method);
        call.Payload.assign(json.begin(),json.end());
        iCAX::Interaction::CSDOChannel channel;iCAX::Interaction::CSDOInvoker invoker(registry);
        auto frontend=channel.GetEndAEndpoint(),backend=channel.GetEndBEndpoint();
        frontend.Send(iCAX::Interaction::CreateTextSDOFrame(1,call.Method.GetCode(),iCAX::Interaction::ESDOFrameKind::Request,json));
        invoker.DispatchAvailableFrames(backend,app,&product,nullptr,scene);auto frames=frontend.Receive();
        if(frames.size()!=1)throw std::runtime_error("SDO response count mismatch");
        iCAX::Interaction::CInvocationResult response;response.nStatus=frames.front().nStatus;response.Payload=std::move(frames.front().Payload);
        if(!response.IsOK())response.strError.assign(response.Payload.begin(),response.Payload.end());
        if(GetEnvironmentVariableW(L"ICAX_TEST_TRACE_SDO",nullptr,0))std::cerr<<group<<"."<<method<<" "<<(response.IsOK()?Text(response.Payload.begin(),response.Payload.end()):response.strError)<<std::endl;
        if (!response.IsOK()) {
            if(response.nStatus==iCAX::Interaction::EInvocationStatus::InvalidInvocation)throw NativeSDOInvalid(response.strError,static_cast<int>(response.nStatus));
            throw NativeSDORuntime(response.strError,static_cast<int>(response.nStatus));
        }
        return iCAX::Data::VariantSerializer::Deserialize(Text(response.Payload.begin(),response.Payload.end())).To<Map>();
    }
    Map Invoke(Scene* scene, const Text& group, const Text& method, const Map& payload={}, Text productID="icax.five-axis-cam")
    {
        return InvokePayload(scene,group,method,iCAX::Data::VariantSerializer::Serialize(payload),productID);
    }
    // Domain assertions use the authoritative snapshot. Wire tests and the
    // browser bridge above always traverse the real bounded SDO channel.
    Map Read(Scene& scene)
    {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        for(;;)
        {
            PumpTasks(scene);auto state=iCAX::FiveAxisCAM::ReadState(scene);
            const auto tasks=state.at("intentGenerationTasks").To<Array>();if(tasks.empty())return state;
            if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Native intent generation timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void SetProjectTestHead(Scene& scene,const std::shared_ptr<CFiveAxisSetupComponent>& setup,const Map& model)
    {
        const auto job=std::dynamic_pointer_cast<CFiveAxisJobComponent>(scene.Database().GetEntity(setup->GetJobID())->GetComponent(CFiveAxisJobComponent::S_ClassName));ASSERT_TRUE(job);
        auto instances=job->GetHeadInstances();const auto id=iCAX::Data::GenerateNewUUID();const auto text=iCAX::Data::to_string(id);
        instances[text]=Map{{"id",text},{"name",model.at("name")},{"revision",UInt(1)},{"model",model},{"parameters",Map{{"tcpMode",Text("Coordinates")},{"tcp",model.at("tcpLocalPosition")},{"beam",model.at("beamLocalDirection")},{"gaugeOrigin",Array{0.,0.,0.}},{"lengthMm",0.},{"axes",Array{}}}},
            {"profile",Map{}},{"binding",Map{}},{"calibrationConfirmed",true}};
        ASSERT_TRUE(job->SetProperty("HeadInstances",instances));ASSERT_TRUE(setup->SetProperty("HeadInstanceID",id));
    }
    Map ImportRequest(Scene& scene,const File& file)
    {
        const auto state=Read(scene);
        return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},
            {"setupId",state.at("activeSetupId")},{"sourcePath",file.Utf8()}};
    }
    Map Wait(Scene& scene,const Map& task)
    {
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        do
        {
            auto result=Invoke(&scene,"CamWorkpiece","QueryTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
            if (result.at("status").To<Text>()!="Running") return result;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now()<until);
        throw std::runtime_error("Import task timed out");
    }
    Map ImportedWorkpiece(Scene& scene,const File& file)
    {
        const auto task=Invoke(&scene,"CamWorkpiece","Import",ImportRequest(scene,file));
        const auto result=Wait(scene,task);
        if(result.at("status").To<Text>()!="Completed") throw std::runtime_error(iCAX::Data::VariantSerializer::Serialize(result));
        return result.at("result").To<Map>();
    }
    // Machining tests explicitly prepare their setup. Import itself creates
    // only the independent workpiece, as in the product workflow.
    Map AddSetup(Scene& scene,Map ids,const Text& machineType="HeadHead")
    {
        const auto setup=Invoke(&scene,"CamSetup","Manage",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
            {"expected",Read(scene).at("expected")},{"placementId",ids.at("placementId")},{"action",Text("create")},{"name",Text("测试装夹")},{"machineType",machineType}});
        ids["setupId"]=setup.at("setupId");ids["placementId"]=setup.at("placementId");return ids;
    }
    Map Imported(Scene& scene,const File& file,const Text& machineType="HeadHead"){return AddSetup(scene,ImportedWorkpiece(scene,file),machineType);}
    Map PlacementRequest(Scene& scene,const Map& ids)
    {
        return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())}, {"expected",Read(scene).at("expected")},
            {"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},
            {"transform",Array{0.,-1.,0.,125.,1.,0.,0.,20.,0.,0.,1.,-8.,0.,0.,0.,1.}}};
    }

TEST(FiveAxisDomain, ImportCommitsOnceAndUndoRedoRestoresWholeWorkpiece)
{
    Scene scene; File source;
    const auto request=ImportRequest(scene,source);
    const auto task=Invoke(&scene,"CamWorkpiece","Import",request);
    EXPECT_EQ(task,Invoke(&scene,"CamWorkpiece","Import",request));
    const auto completed=Wait(scene,task);
    ASSERT_EQ("Completed",completed.at("status").To<Text>())<<completed.at("error").To<Text>();
    const auto after=Read(scene);
    EXPECT_FALSE(after.at("workpieces").To<Array>().empty());
    EXPECT_EQ(1u,after.at("placements").To<Array>().size());
    EXPECT_EQ(1u,scene.Database().GetUndoArray().size());
    EXPECT_EQ(completed,Invoke(&scene,"CamWorkpiece","Import",request));
    EXPECT_EQ(1u,scene.Database().GetUndoArray().size());
    ASSERT_TRUE(scene.Database().Undo());
    EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().empty());
    EXPECT_TRUE(Read(scene).at("placements").To<Array>().empty());
    ASSERT_TRUE(scene.Database().Redo());
    EXPECT_EQ(after.at("workpieces"),Read(scene).at("workpieces"));
    EXPECT_EQ(after.at("placements"),Read(scene).at("placements"));
    EXPECT_NE(after.at("expected"),Read(scene).at("expected"));
}
TEST(FiveAxisDomain, ImportCentersOffsetGeometryAtWorldOrigin)
{
    Scene scene; File source(BRepPrimAPI_MakeBox(gp_Pnt(110., -80., 25.), 40., 60., 20.).Shape());
    const auto ids = Imported(scene, source);
    const auto state = Read(scene);
    const auto matrix = state.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>();
    EXPECT_NEAR(matrix[3].To<double>(), -130., 1e-7);
    EXPECT_NEAR(matrix[7].To<double>(), 50., 1e-7);
    EXPECT_NEAR(matrix[11].To<double>(), -35., 1e-7);
    const auto id = *ID::from_string(ids.at("placementId").To<Text>());
    const auto transform = std::dynamic_pointer_cast<iCAX::Transform::CTransformComponent>(scene.Database().GetEntity(id)->GetComponent("CTransformComponent"));
    ASSERT_TRUE(transform);
    EXPECT_NEAR(transform->GetProperty("PositionX").To<double>(), -130., 1e-7);
    EXPECT_NEAR(transform->GetProperty("PositionY").To<double>(), 50., 1e-7);
    EXPECT_NEAR(transform->GetProperty("PositionZ").To<double>(), -35., 1e-7);
    ASSERT_TRUE(scene.Database().Undo());
    ASSERT_TRUE(scene.Database().Redo());
    EXPECT_EQ(state.at("placements"), Read(scene).at("placements"));
}
TEST(FiveAxisDomain, AdditionalImportsAreIndependentUndoableAndCancellable)
{
    Scene scene;File source;const auto first=ImportedWorkpiece(scene,source);const auto before=Read(scene);
    const auto second=ImportedWorkpiece(scene,source);EXPECT_NE(first.at("workpieceId"),second.at("workpieceId"));
    EXPECT_NE(first.at("placementId"),second.at("placementId"));const auto two=Read(scene);
    EXPECT_TRUE(two.at("activeSetupId").To<Text>().empty());EXPECT_EQ(second.at("placementId"),two.at("activePlacementId"));
    ASSERT_TRUE(two.at("setups").To<Array>().empty());
    ASSERT_EQ(2u,two.at("workpieces").To<Array>().size());ASSERT_EQ(2u,two.at("placements").To<Array>().size());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));
    EXPECT_EQ(first.at("placementId"),Read(scene).at("activePlacementId"));EXPECT_TRUE(Read(scene).at("setups").To<Array>().empty());
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(two.at("workpieces"),Read(scene).at("workpieces"));
    EXPECT_EQ(second.at("placementId"),Read(scene).at("activePlacementId"));EXPECT_TRUE(Read(scene).at("setups").To<Array>().empty());
    const auto task=Invoke(&scene,"CamWorkpiece","Import",ImportRequest(scene,source));
    Invoke(&scene,"CamWorkpiece","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    EXPECT_EQ("Cancelled",Wait(scene,task).at("status").To<Text>());EXPECT_EQ(two.at("workpieces"),Read(scene).at("workpieces"));
}
TEST(FiveAxisDomain, SaveReopenWithoutSourcePreservesExactGeometryAndPlacement)
{
    Scene scene; File source;
    const auto ids=Imported(scene,source);
    const auto move=PlacementRequest(scene,ids);
    Invoke(&scene,"CamPlacement","SetTransform",move);
    const auto before=Read(scene);
    const auto w=before.at("workpieces").To<Array>().front().To<Map>();
    const auto ref=w.at("geometryRef").To<Map>();
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    ASSERT_NE(nullptr,geometry);
    const auto bytes=iCAX::GeometryData::Persistence::Serialize(*geometry);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="导入与定位";
    auto output=source.Path;output.replace_extension(".i5cam");
    file.Save(output,info,scene.Database(),scene.Resources());
    ASSERT_TRUE(std::filesystem::remove(source.Path));
    Scene reopened(scene.Project,scene.GetSceneID(),false);
    file.Open(output,reopened.Database(),reopened.Resources());
    InitializeScene(reopened);
    const auto after=Read(reopened);
    EXPECT_EQ(before.at("workpieces"),after.at("workpieces"));
    EXPECT_EQ(before.at("placements"),after.at("placements"));
    EXPECT_NE(before.at("expected").To<Map>().at("epoch"),after.at("expected").To<Map>().at("epoch"));
    const auto restored=reopened.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    ASSERT_NE(nullptr,restored);
    EXPECT_EQ(bytes,iCAX::GeometryData::Persistence::Serialize(*restored));
    const auto binary=w.at("sourceRef").To<Map>();
    ASSERT_FALSE(reopened.Resources().Get<iCAX::Resource::CBinaryResource>(binary.at("url").To<Text>(),1)->Content.empty());
    const auto entity=reopened.Database().GetEntity(*ID::from_string(ids.at("placementId").To<Text>()));
    const auto transform=std::dynamic_pointer_cast<iCAX::Transform::CTransformComponent>(entity->GetComponent("CTransformComponent"));
    ASSERT_NE(nullptr,transform);EXPECT_DOUBLE_EQ(125,transform->GetPositionX());EXPECT_NEAR(1.5707963267948966,transform->GetYawRadians(),1e-12);
    std::filesystem::remove(output);
}
TEST(FiveAxisDomain, ScaleBakesExactGeometryAndPreservesRigidPlacementUndoRedoAndSave)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(40,60,20).Shape());const auto ids=Imported(scene,source);
    Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,ids));
    const auto before=Read(scene),workpiece=before.at("workpieces").To<Array>().front().To<Map>();
    Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",before.at("expected")},
        {"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},{"sourceGeometry",workpiece.at("geometryRef")},
        {"factors",Array{2.,2.,2.}},{"center",Array{20.,30.,10.}}};
    const auto task=Invoke(&scene,"CamWorkpiece","Scale",request);
    const auto result=Wait(scene,task);ASSERT_EQ("Completed",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);
    const auto after=Read(scene),scaled=after.at("workpieces").To<Array>().front().To<Map>();
    EXPECT_EQ(workpiece.at("sourceRef"),scaled.at("sourceRef"));EXPECT_NE(workpiece.at("geometryRef"),scaled.at("geometryRef"));
    EXPECT_EQ(before.at("placements").To<Array>().front().To<Map>().at("transform"),after.at("placements").To<Array>().front().To<Map>().at("transform"));
    const auto ref=scaled.at("geometryRef").To<Map>();
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    ASSERT_TRUE(geometry);const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*geometry);ASSERT_TRUE(built.bOK);EXPECT_TRUE(BRepCheck_Analyzer(built.Shape).IsValid());
    Bnd_Box bounds;BRepBndLib::AddOptimal(built.Shape,bounds,false,false);double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);
    EXPECT_NEAR(80.,X-x,1e-5);EXPECT_NEAR(120.,Y-y,1e-5);EXPECT_NEAR(40.,Z-z,1e-5);
    EXPECT_NEAR(20.,(x+X)/2,1e-5);EXPECT_NEAR(30.,(y+Y)/2,1e-5);EXPECT_NEAR(10.,(z+Z)/2,1e-5);
    EXPECT_EQ(result.at("result"),Invoke(&scene,"CamWorkpiece","Scale",request).at("result"));
    auto stale=request;stale["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Scale",stale),std::exception);
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(after.at("workpieces"),Read(scene).at("workpieces"));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="缩放工件";
    auto output=source.Path;output.replace_extension(".i5cam");file.Save(output,info,scene.Database(),scene.Resources());
    ASSERT_TRUE(std::filesystem::remove(source.Path));Scene reopened(scene.Project,scene.GetSceneID(),false);
    file.Open(output,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(after.at("workpieces"),Read(reopened).at("workpieces"));EXPECT_EQ(after.at("placements"),Read(reopened).at("placements"));
    const auto restored=reopened.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    ASSERT_TRUE(restored);EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*geometry),iCAX::GeometryData::Persistence::Serialize(*restored));
    std::filesystem::remove(output);
}
TEST(FiveAxisDomain, ScaleRejectsInvalidFactorsWrongSourceAndCancelledOrSupersededPublication)
{
    Scene scene;File source;const auto ids=Imported(scene,source);const auto original=Read(scene);
    Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",original.at("expected")},
        {"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},
        {"sourceGeometry",original.at("workpieces").To<Array>().front().To<Map>().at("geometryRef")},
        {"factors",Array{2.,2.,2.}},{"center",Array{0.,0.,0.}}};
    for(const auto& factors:std::vector<Array>{{0.,1.,1.},{-1.,1.,1.},{std::numeric_limits<double>::infinity(),1.,1.},{true,1.,1.},{1.,1.},
        {2.,1.,1.},{1.,2.,1.},{1.,1.,2.},{2.,2.,1.},{2.,2.,2.+1e-12}}){
        auto bad=request;bad["factors"]=factors;EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Scale",bad),std::exception);EXPECT_EQ(original,Read(scene));
    }
    auto bad=request;bad["sourceGeometry"]=Map{};EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Scale",bad),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamWorkpiece","Scale",request),std::exception);
    auto task=Invoke(&scene,"CamWorkpiece","Scale",request);
    Invoke(&scene,"CamWorkpiece","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    EXPECT_EQ("Cancelled",Wait(scene,task).at("status").To<Text>());EXPECT_EQ(original.at("workpieces"),Read(scene).at("workpieces"));
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["expected"]=Read(scene).at("expected");
    task=Invoke(&scene,"CamWorkpiece","Scale",request);
    Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,ids));
    EXPECT_EQ("Superseded",Wait(scene,task).at("status").To<Text>());EXPECT_EQ(original.at("workpieces"),Read(scene).at("workpieces"));
}
TEST(FiveAxisDomain, PlacementRejectsReflectionScaleAndStaleRequests)
{
    Scene scene; File source;const auto ids=Imported(scene,source);
    auto request=PlacementRequest(scene,ids);
    const auto original=Read(scene);
    for(const double x:{-1.,2.})
    {
        auto invalid=request;invalid["transform"]=Array{x,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};
        EXPECT_THROW(Invoke(&scene,"CamPlacement","SetTransform",invalid),std::exception);
        EXPECT_EQ(original,Read(scene));
    }
    const auto result=Invoke(&scene,"CamPlacement","SetTransform",request);
    EXPECT_EQ(result,Invoke(&scene,"CamPlacement","SetTransform",request));
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    EXPECT_THROW(Invoke(&scene,"CamPlacement","SetTransform",request),std::exception);
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);
    EXPECT_EQ(original.at("placements"),Read(scene).at("placements"));
}
TEST(FiveAxisDomain, RedoThenUndoStillSupersedesOldImport)
{
    Scene scene;File source;ImportedWorkpiece(scene,source);
    ASSERT_TRUE(scene.Database().Undo());
    const auto task=Invoke(&scene,"CamWorkpiece","Import",ImportRequest(scene,source));
    ASSERT_TRUE(scene.Database().Redo());
    ASSERT_TRUE(scene.Database().Undo());
    EXPECT_EQ("Superseded",Wait(scene,task).at("status").To<Text>());
    EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().empty());
}
TEST(FiveAxisDomain, CancelFailureAndWrongContextNeverCommit)
{
    Scene scene;File source;
    const auto task=Invoke(&scene,"CamWorkpiece","Import",ImportRequest(scene,source));
    Invoke(&scene,"CamWorkpiece","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    EXPECT_EQ("Cancelled",Wait(scene,task).at("status").To<Text>());
    EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().empty());
    auto request=ImportRequest(scene,source);request["sourcePath"]=Text("missing.step");
    const auto failed=Invoke(&scene,"CamWorkpiece","Import",request);
    EXPECT_EQ("Failed",Wait(scene,failed).at("status").To<Text>());
    EXPECT_TRUE(scene.Database().GetUndoArray().empty());
    EXPECT_THROW(Invoke(nullptr,"CamScene","ReadState"),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamScene","ReadState",{},"icax.tube-designer"),std::exception);
    Scene other;
    EXPECT_THROW(Invoke(&other,"CamWorkpiece","QueryTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}}),std::exception);
}
TEST(FiveAxisDomain, CommitFailureDoesNotPublishHalfAWorkpiece)
{
    class RejectCommit final : public iCAX::Database::IChecker
    {
    public:
        bool Reject=true;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text& error) override
        {
            if(Reject) { Reject=false;error="Injected placement commit failure";return false; }
            return true;
        }
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&) override { return true; }
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet&,Text&) override { return true; }
    };
    Scene scene;File source;
    const auto original=Read(scene);const auto checker=std::make_shared<RejectCommit>();
    scene.Database().GetMetaRegistry()->RegistChecker<CFiveAxisPlacementComponent>(checker);
    const auto task=Invoke(&scene,"CamWorkpiece","Import",ImportRequest(scene,source));
    const auto result=Wait(scene,task);
    ASSERT_EQ("Failed",result.at("status").To<Text>());
    EXPECT_NE(Text::npos,result.at("error").To<Text>().find("Injected placement commit failure"));
    EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().empty());
    EXPECT_TRUE(Read(scene).at("placements").To<Array>().empty());
    EXPECT_EQ(original.at("inputRevision"),Read(scene).at("inputRevision"));
    EXPECT_TRUE(scene.Database().GetUndoArray().empty());
    EXPECT_GT(scene.Resources().Count(),0u); // unreferenced candidates, never authoritative
    EXPECT_NO_THROW(Imported(scene,source));
}
TEST(FiveAxisDomain, ClosingSceneRejectsPublicationAndCanBeDestroyedWithPendingWork)
{
    File source;auto scene=std::make_unique<Scene>();
    const auto task=Invoke(scene.get(),"CamWorkpiece","Import",ImportRequest(*scene,source));
    scene->Closing=true;
    EXPECT_THROW(PumpTasks(*scene),std::exception);
    EXPECT_TRUE(scene->Database().GetUndoArray().empty());
    scene.reset(); // no Scene pointer is retained by the geometry worker
    Scene next;
    EXPECT_TRUE(Read(next).at("workpieces").To<Array>().empty());
}

    Map RegistrationRequest(Scene& scene,const Map& ids)
    {
        return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},
            {"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},
            {"registration",Map{{"method",Text("PairedPoints")},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},
                {"unit",Text("mm")},{"maximumResidualMm",0.01},
                {"modelPoints",Array{Array{0.,0.,0.},Array{10.,0.,0.},Array{0.,20.,0.}}},
                {"measuredPoints",Array{Array{100.,200.,300.},Array{100.,210.,300.},Array{80.,200.,300.}}}}}};
    }
TEST(FiveAxisDomain, RegistrationPreviewIsReadOnlyAndApplyPreservesEvidenceAndUndo)
{
    Scene scene;File source;const auto ids=Imported(scene,source);auto request=RegistrationRequest(scene,ids);
    const auto before=Read(scene);const auto count=scene.Resources().Count();
    const auto preview=Invoke(&scene,"CamPlacement","SolveRegistration",request);
    ASSERT_TRUE(preview.at("qualified").To<bool>());
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(count,scene.Resources().Count());
    EXPECT_EQ(2u,scene.Database().GetUndoArray().size()); // import, explicit setup
    // Ignored untrusted output fields cannot change the recomputed placement.
    request["transform"]=Array{};request["qualified"]=false;
    const auto result=Invoke(&scene,"CamPlacement","ApplyRegistration",request);
    EXPECT_EQ(result,Invoke(&scene,"CamPlacement","ApplyRegistration",request));
    const auto after=Read(scene);const auto p=after.at("placements").To<Array>().front().To<Map>();
    EXPECT_EQ("AppliedInCAM",p.at("applicationMode").To<Text>());
    EXPECT_EQ(preview.at("transform"),p.at("transform"));
    EXPECT_EQ(before.at("workpieces"),after.at("workpieces"));
    EXPECT_EQ(3u,scene.Database().GetUndoArray().size()); // plus registration
    const auto ref=p.at("registrationRef").To<Map>();EXPECT_FALSE(ref.at("revision").To<Text>().empty());
    const auto placement=std::dynamic_pointer_cast<CFiveAxisPlacementComponent>(scene.Database().GetEntity(ID::from_string(p.at("id").To<Text>()).value())->GetComponent(CFiveAxisPlacementComponent::S_ClassName));
    ASSERT_TRUE(placement);EXPECT_EQ(count,scene.Resources().Count());
    const auto report=placement->GetRegistrationData().at("value").To<Map>();
    EXPECT_EQ(request.at("registration"),report.at("input"));EXPECT_EQ(preview.at("residualsMm"),report.at("residualsMm"));
    EXPECT_THROW(Invoke(&scene,"CamPlacement","SetTransform",request),std::exception); // same command, different method
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);
    EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);
    EXPECT_EQ(after.at("placements"),Read(scene).at("placements"));
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    EXPECT_THROW(Invoke(&scene,"CamPlacement","ApplyRegistration",request),std::exception);
}
TEST(FiveAxisDomain, RegistrationRejectsWrongFramePoorFitAndChangedSetupWithoutMutation)
{
    Scene scene;File source;const auto ids=Imported(scene,source);auto request=RegistrationRequest(scene,ids);
    const auto before=Read(scene);
    auto bad=request;auto specification=bad.at("registration").To<Map>();specification["modelFrame"]=Text("World");bad["registration"]=specification;
    EXPECT_THROW(Invoke(&scene,"CamPlacement","SolveRegistration",bad),std::exception);
    specification=request.at("registration").To<Map>();
    specification["measuredPoints"]=Array{Array{0.,0.,0.},Array{30.,0.,0.},Array{0.,40.,0.}};bad["registration"]=specification;
    EXPECT_FALSE(Invoke(&scene,"CamPlacement","SolveRegistration",bad).at("qualified").To<bool>());
    EXPECT_THROW(Invoke(&scene,"CamPlacement","ApplyRegistration",bad),std::exception);
    EXPECT_EQ(before,Read(scene));
    bad=request;bad["setupId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    EXPECT_THROW(Invoke(&scene,"CamPlacement","ApplyRegistration",bad),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamPlacement","ApplyRegistration",request),std::exception);
    EXPECT_EQ(2u,scene.Database().GetUndoArray().size()); // import, explicit setup
}
TEST(FiveAxisDomain, AppliedRegistrationReportSurvivesSaveReopenAndManualMoveClearsQualification)
{
    Scene scene;File source;const auto ids=Imported(scene,source);
    const auto registrationRequest=RegistrationRequest(scene,ids);
    Map readRequest{{"expected",Read(scene).at("expected")},{"placementId",ids.at("placementId")},{"setupId",ids.at("setupId")}};
    EXPECT_TRUE(Invoke(&scene,"CamPlacement","ReadRegistration",readRequest).at("report").To<Map>().empty());
    Invoke(&scene,"CamPlacement","ApplyRegistration",registrationRequest);const auto before=Read(scene);
    EXPECT_THROW(Invoke(&scene,"CamPlacement","ReadRegistration",readRequest),std::exception);
    readRequest["expected"]=before.at("expected");
    const auto savedReport=Invoke(&scene,"CamPlacement","ReadRegistration",readRequest);
    EXPECT_EQ(registrationRequest.at("registration"),savedReport.at("report").To<Map>().at("input"));
    EXPECT_EQ(3u,savedReport.at("report").To<Map>().at("residualsMm").To<Array>().size());
    EXPECT_EQ(before,Read(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="定位保存";
    auto output=source.Path;output.replace_extension(".i5cam");file.Save(output,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(output,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(before.at("placements"),Read(reopened).at("placements"));
    readRequest["expected"]=Read(reopened).at("expected");
    EXPECT_EQ(savedReport,Invoke(&reopened,"CamPlacement","ReadRegistration",readRequest));
    Invoke(&reopened,"CamPlacement","SetTransform",PlacementRequest(reopened,ids));
    const auto after=Read(reopened).at("placements").To<Array>().front().To<Map>();
    EXPECT_EQ("Pending",after.at("applicationMode").To<Text>());EXPECT_TRUE(after.at("registrationRef").To<Map>().empty());
    readRequest["expected"]=Read(reopened).at("expected");
    EXPECT_TRUE(Invoke(&reopened,"CamPlacement","ReadRegistration",readRequest).at("report").To<Map>().empty());
    ASSERT_TRUE(reopened.Database().Undo());PumpTasks(reopened);EXPECT_EQ(before.at("placements"),Read(reopened).at("placements"));
    std::filesystem::remove(output);
}
TEST(FiveAxisDomain, PickingUsesFixedExactGeometryAndApplyReevaluatesReferenceQueries)
{
    Scene scene;File source;const auto ids=Imported(scene,source);
    const Map query{{"mode",Text("point")},{"frame",Text("Workpiece")},{"triangleIndex",Text("0")},
        {"nearPoint",Array{29.8,0.,0.}},{"maximumDistanceMm",1.}};
    Map pick{{"expected",Read(scene).at("expected")},{"placementId",ids.at("placementId")},{"setupId",ids.at("setupId")},{"query",query}};
    const auto before=Read(scene);const auto resolved=Invoke(&scene,"CamPlacement","ResolveReference",pick);
    const auto faces=Invoke(&scene,"CamPlacement","ReadReferenceFaces",pick);
    EXPECT_EQ(faces.at("geometryRef"),resolved.at("geometryRef"));
    ASSERT_FALSE(faces.at("faces").To<Array>().empty());
    const auto firstFace=faces.at("faces").To<Array>().front().To<Map>();
    EXPECT_EQ(firstFace.at("faceId"),resolved.at("faceId"));
    EXPECT_EQ(firstFace.at("first").To<Text>(),"0");
    EXPECT_EQ(firstFace.at("count"),resolved.at("triangleRanges").To<Array>().front().To<Map>().at("count"));
    auto wrongSetup=pick;wrongSetup["setupId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    EXPECT_THROW(Invoke(&scene,"CamPlacement","ReadReferenceFaces",wrongSetup),std::exception);
    EXPECT_NEAR(30,resolved.at("point").To<Array>()[0].To<double>(),1e-8);EXPECT_EQ(before,Read(scene));
    auto request=pick;request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    request["registration"]=Map{{"method",Text("Points")},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},{"unit",Text("mm")},
        {"origin",Array{0.,0.,0.}},{"xPoint",Array{40.,0.,0.}},{"planePoint",Array{30.,10.,0.}},
        {"referenceQueries",Map{{"origin",query}}}};
    const auto preview=Invoke(&scene,"CamPlacement","SolveRegistration",request);
    EXPECT_NEAR(-30,preview.at("transform").To<Array>()[3].To<double>(),1e-8);
    const auto result=Invoke(&scene,"CamPlacement","ApplyRegistration",request).at("report").To<Map>();
    EXPECT_EQ(resolved.at("point"),result.at("input").To<Map>().at("origin"));
    EXPECT_FALSE(result.at("resolvedReferences").To<Map>().empty());
    EXPECT_THROW(Invoke(&scene,"CamPlacement","ResolveReference",pick),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamPlacement","ReadReferenceFaces",pick),std::exception);
}

TEST(FiveAxisDomain, RegistrationAcceptsSdkIntegerMeasurementsAndRejectsNonnumericValues)
{
    Scene scene;File source;const auto ids=Imported(scene,source);
    auto request=RegistrationRequest(scene,ids);
    auto specification=request.at("registration").To<Map>();
    specification["modelPoints"]=Array{Array{0,0,0},Array{10,0,0},Array{0,20,0}};
    specification["measuredPoints"]=Array{Array{100,200,300},Array{100,210,300},Array{80,200,300}};
    specification["maximumResidualMm"]=2;
    request["registration"]=specification;
    const auto before=Read(scene);
    const auto preview=Invoke(&scene,"CamPlacement","SolveRegistration",request);
    EXPECT_TRUE(preview.at("qualified").To<bool>());
    EXPECT_EQ(before,Read(scene));
    for(const Variant invalid : {Variant(true),Variant(Text("2")),Variant{}})
    {
        auto bad=specification;bad["maximumResidualMm"]=invalid;
        auto badRequest=request;badRequest["registration"]=bad;
        EXPECT_THROW(Invoke(&scene,"CamPlacement","ApplyRegistration",badRequest),std::exception);
        EXPECT_EQ(before,Read(scene));
    }
    const auto applied=Invoke(&scene,"CamPlacement","ApplyRegistration",request);
    EXPECT_EQ(preview.at("transform"),applied.at("report").To<Map>().at("transform"));
}

TEST(FiveAxisDomain, PickingAndPlacementAcceptSdkIntegers)
{
    Scene scene;File source;const auto ids=Imported(scene,source);
    const Map query{{"mode",Text("point")},{"frame",Text("Workpiece")},{"triangleIndex",Text("0")},
        {"nearPoint",Array{30,0,0}},{"maximumDistanceMm",2},{"sign",1}};
    Map pick{{"expected",Read(scene).at("expected")},{"placementId",ids.at("placementId")},{"setupId",ids.at("setupId")},{"query",query}};
    EXPECT_NEAR(30,Invoke(&scene,"CamPlacement","ResolveReference",pick).at("point").To<Array>()[0].To<double>(),1e-8);
    auto request=PlacementRequest(scene,ids);
    request["transform"]=Array{0,-1,0,125,1,0,0,20,0,0,1,-8,0,0,0,1};
    const auto before=Read(scene);
    for(const Variant invalid : {Variant(true),Variant(Text("1")),Variant{}})
    {
        auto values=request.at("transform").To<Array>();values[0]=invalid;
        auto bad=request;bad["transform"]=values;
        EXPECT_THROW(Invoke(&scene,"CamPlacement","SetTransform",bad),std::exception);
        EXPECT_EQ(before,Read(scene));
    }
    EXPECT_NO_THROW(Invoke(&scene,"CamPlacement","SetTransform",request));
    EXPECT_EQ(PlacementRequest(scene,ids).at("transform"),Read(scene).at("placements").To<Array>().front().To<Map>().at("transform"));
}

#include "ManualSetupDomainTests.inc"
#include "EngineeringDataTestHelpers.inc"
#include "RecognitionDomainTests.inc"
#include "AlignmentFrameDomainTests.inc"
#include "ProcessDomainTests.inc"
#include "LeadDomainTests.inc"
#include "CutEditingDomainTests.inc"
#include "CutSplittingDomainTests.inc"
#include "CutClearingDomainTests.inc"
#include "PlanGenerationDomainTests.inc"
#include "LeadMaterialSideTests.inc"
#include "OuterTrimLeadSideTests.inc"
#include "CompensationGenerationDomainTests.inc"
#include "ToolpathOrderDomainTests.inc"
#include "OrderPreviewDomainTests.inc"
#include "ToolAxisDomainTests.inc"
#include "ToolAxisSamplesDomainTests.inc"
#include "ToolAxisInfluenceDomainTests.inc"
#include "RoundedCoverGenerationTests.inc"
#include "MachiningOrderTests.inc"
#include "RapidPlanningDomainTests.inc"
#include "CollisionDistanceDomainTests.inc"
#include "OmplPlanningDomainTests.inc"
#include "DescartesPlanningDomainTests.inc"
#include "DescartesRealModelTests.inc"
#include "HeadModelDomainTests.inc"
#include "HeadStructureImportTests.inc"
#include "SimulationCollisionDomainTests.inc"
#include "CollisionParallelBenchmarkTests.inc"
#include "SimulationCollisionResumeTests.inc"
#include "ProductCollisionBoxTests.inc"
#include "CollisionRotationDomainTests.inc"
#include "RapidTransportOrientationTests.inc"
#include "MachinePlanDomainTests.inc"
#include "BuiltinHeadDomainTests.inc"
#include "SplitChainMachineDomainTests.inc"
#include "GantryHeadDomainTests.inc"
#include "MachineOffsetReplanDomainTests.inc"
#include "InteractiveIKPDODomainTests.inc"
#include "InteractiveCollisionRecoveryDomainTests.inc"
#include "MachineJogPDODomainTests.inc"
#include "TaskProgressPDODomainTests.inc"
#include "CollisionRepairDomainTests.inc"
#include "CollisionRepairBlockDomainTests.inc"
#include "RapidOrientationRepairSearchTests.inc"
#include "SimulationCollisionBoundsTests.inc"
#include "RapidConstructionDomainTests.inc"
#include "RapidAvoidanceDomainTests.inc"
#include "RapidManualDomainTests.inc"
#include "FastRapidGenerationTests.inc"
#include "SafeRapidParallelTests.inc"
#include "QuickBoxRapidTests.inc"
#include "SceneStateTransportTests.inc"
#include "EngineeringDataDomainTests.inc"
#include "StandardHoleDomainTests.inc"
#include "FixtureQueryTestHelpers.inc"
#include "FixtureDomainTests.inc"
#include "FixtureDraftDomainTests.inc"
#include "FixtureAutomaticFacesTests.inc"
#include "RapidCollisionCacheTests.inc"
#include "RapidCollisionDiskCacheTests.inc"
#include "FixtureLibraryDomainTests.inc"
#include "HeadInstanceDomainTests.inc"
#include "HeadPreviewDomainTests.inc"
#include "HeadScopeDomainTests.inc"
#include "OperationDomainTests.inc"
#include "HeadMachineCompatibilityDomainTests.inc"
#include "FullHeadCollisionTests.inc"
#include "FixtureJoineryTests.inc"
#include "FixtureSurfaceTests.inc"
#include "FixtureClearanceTests.inc"
#include "FixtureSupportDirectionTests.inc"
#include "FixtureOpeningGeometryTests.inc"
#include "SectionPlateEditingTests.inc"
#include "FixtureParallelTests.inc"
#include "FixtureSkeletonTests.inc"
#include "FixtureThirdPartyTests.inc"
#include "FixtureCutClearanceDomainTests.inc"
#include "FixturePartEditingDomainTests.inc"
#include "RecognitionLocationDomainTests.inc"
#include "PoseOrderingDomainTests.inc"
#include "MultiWorkpieceDomainTests.inc"
#include "SetupDomainTests.inc"
#include "PreparationDomainTests.inc"
#include "WorkflowAndSphereDomainTests.inc"
#include "WorkpieceRemovalDomainTests.inc"
#include "WorkpieceLockDomainTests.inc"
#include "WorkpieceEnableDomainTests.inc"
#include "UnifiedIntentWorkflowDomainTests.inc"
#include "PathEditingDomainTests.inc"
#include "BranchProcessNativeWorkflowTests.inc"
#include "PendingNativeWorkflowTests.inc"
#include "FilePreviewDomainTests.inc"
#include "FilePreviewCacheDomainTests.inc"

void CheckRealDiskBoundaryFramesAndGeneratedPaths(double scale)
{
    Scene scene;
    const auto source=std::filesystem::weakly_canonical(std::filesystem::path(__FILE__).parent_path()/"../../../../../../TestData/FA/2.stp.STEP");
    ASSERT_TRUE(std::filesystem::is_regular_file(source));const auto utf8=source.u8string();
    auto state=Read(scene);
    const auto imported=Wait(scene,Invoke(&scene,"CamWorkpiece","Import",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
        {"expected",state.at("expected")},{"setupId",state.at("activeSetupId")},{"sourcePath",Text(reinterpret_cast<const char*>(utf8.data()),utf8.size())}}));
    ASSERT_EQ("Completed",imported.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(imported);
    const auto ids=imported.at("result").To<Map>();state=Read(scene);
    if(scale!=1.){
        const auto scaled=Wait(scene,Invoke(&scene,"CamWorkpiece","Scale",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
            {"expected",state.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},
            {"sourceGeometry",state.at("workpieces").To<Array>().front().To<Map>().at("geometryRef")},
            {"factors",Array{scale,scale,scale}},{"center",Array{0.,0.,0.}}}));
        ASSERT_EQ("Completed",scaled.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(scaled);state=Read(scene);
    }
    const auto sources=Invoke(&scene,"CamCut","QuerySources",{{"expected",state.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}});
    const auto geometryRef=sources.at("sourceGeometry").To<Map>();
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(geometryRef.at("url").To<Text>(),std::stoull(geometryRef.at("version").To<Text>()));
    // User's 34-face selection: top AND complete outer side wall. Face 8 is a
    // half-round rim blend, whose own thin wire is NOT the requested lower rim.
    const std::set<UInt> frontFaces{9,10,11,12,13,14,15,16,17,18,19,20,21,22,
        68,69,70,71,72,74,76,77,78,156,157,158,159,160,161,162,163,164,165,182};
    Array selectedFaces;
    for(const auto& value:sources.at("faceUses").To<Array>()){
        const auto use=value.To<Map>();const auto id=std::stoull(use.at("localId").To<Text>());
        if(frontFaces.contains(id))selectedFaces.push_back(value);
    }
    ASSERT_EQ(frontFaces.size(),selectedFaces.size());
    const auto boundary=scene.Services().Resolve<iCAX::GeometryServices::IBoundaryLeadService>();
    for(const auto& face:geometry->Faces)if(frontFaces.contains(face.Id)){
        for(const auto wireID:face.WireIds)for(const auto& wire:geometry->Wires)if(wire.Id==wireID)
            for(std::size_t index=0;index<wire.Coedges.size();++index){
                const auto& c=wire.Coedges[index];
                for(const auto fraction:{0.,.25,.5,.75,1.}){const auto u=c.Range.First+(c.Range.Last-c.Range.First)*fraction;
                    iCAX::GeometryServices::BoundaryLeadInput q;q.FaceId=face.Id;q.WireId=wire.Id;q.CoedgeIndex=index;q.EdgeId=c.EdgeId;q.NativeParameter=u;
                    try{boundary->Frame(*geometry,q);}catch(const std::exception& error){ADD_FAILURE()<<"face "<<face.Id<<" edge "<<c.EdgeId<<" u="<<u<<": "<<error.what();}
                }
            }
    }
    Map extraction{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},
        {"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},{"sourceGeometry",sources.at("sourceGeometry")},{"extractionRuleRef",sources.at("extractionRuleRef")},
        {"selection",Map{{"mode",Text("Faces")},{"faceUses",selectedFaces}}},{"geometryOnly",true},{"representation",Text("Solid")},{"workpieceFamily",Text("GeneralSolid")},
        {"side",Map{{"outwardSign",1.}}},{"requestedKinds",Array{Text("Circle"),Text("Rectangle"),Text("Slot"),Text("GeneralContour")}},
        {"draftId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"draftRevision",Text("1")},{"draftDigest",Text("real-disk-regression")}};
    const auto terminal=Wait(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));
    ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(terminal);
    const auto candidates=Invoke(&scene,"CamCut","QueryCandidates",{{"epoch",terminal.at("epoch")},
        {"candidateSet",terminal.at("result").To<Map>().at("candidateSet")},{"pageSize",Text("100")}});
    std::size_t lowerRims=0;
    for(const auto& value:candidates.at("candidates").To<Array>()){
        const auto c=value.To<Map>();EXPECT_EQ("Ready",c.at("confirmationEligibility").To<Text>());
        if(c.at("boundaryRole").To<Text>()!="OuterTrim")continue;
        ++lowerRims;const auto b=c.at("boundary").To<Map>();std::set<UInt> edges;
        for(const auto& v:b.at("sourceSpans").To<Array>()){const auto span=v.To<Map>();edges.insert(std::stoull(span.at("edgeId").To<Text>()));EXPECT_NE(Text("8"),span.at("faceId").To<Text>());}
        EXPECT_EQ((std::set<UInt>{77,80,88,89,90,93,94,95,99,102,105,108,112,120,122,125}),edges);
        EXPECT_NEAR(265.0399444945867*scale,b.at("perimeterMm").To<double>(),1e-6);
        double minX=1e9,maxX=-1e9,minZ=1e9,maxZ=-1e9;
        for(const auto& v:b.at("previewPoints").To<Array>()){const auto p=v.To<Array>();
            minX=std::min(minX,p[0].To<double>());maxX=std::max(maxX,p[0].To<double>());
            minZ=std::min(minZ,p[2].To<double>());maxZ=std::max(maxZ,p[2].To<double>());
            EXPECT_GT(p[1].To<double>(),14.*scale);EXPECT_LT(p[1].To<double>(),16.*scale);
        }
        EXPECT_LT(minX,-46.*scale);EXPECT_GT(maxX,41.*scale);EXPECT_LT(minZ,-41.*scale);EXPECT_GT(maxZ,41.*scale);
    }
    ASSERT_EQ(1u,lowerRims);
    const auto create=CreateRequest(scene,extraction,terminal);
    auto prepare=PrepareRequest(scene,extraction,terminal);auto assignment=prepare.at("assignment").To<Map>();
    auto kerf=assignment.at("kerf").To<Map>();kerf["widthMm"]=0.;assignment["kerf"]=kerf;prepare["assignment"]=assignment;
    const auto created=Invoke(&scene,"CamCut","Create",WithPolicies(create,Invoke(&scene,"CamProcess","Prepare",prepare)));
    const auto cuts=created.at("createdIntentIds").To<Array>();ASSERT_EQ(19u,cuts.size());
    for(const auto& id:cuts){
        try{LeadPreview(scene,{id},{{"leadIn",Lead("Line",.2)}});}
        catch(const std::exception& error){
            const Text message=error.what();
            EXPECT_EQ(Text::npos,message.find("FRAME_DEGENERATE"))<<id.To<Text>()<<": "<<message;
            EXPECT_EQ(Text::npos,message.find("BOUNDARY_MISMATCH"))<<id.To<Text>()<<": "<<message;
            EXPECT_EQ(Text::npos,message.find("SOURCE_MISSING"))<<id.To<Text>()<<": "<<message;
        }
    }
    const auto result=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));
    EXPECT_EQ("Completed",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);
    std::size_t trims=0;
    for(const auto& value:Generated(scene).at("operations").To<Array>()){const auto op=value.To<Map>();if(op.at("boundaryRole").To<Text>()!="OuterTrim")continue;
        ++trims;EXPECT_EQ(0u,std::stoull(op.at("cadJunctions").To<Text>()));EXPECT_DOUBLE_EQ(0.,op.at("maximumCadJunctionMm").To<double>());
        const auto bytes=DataBytes(op.at("curveData").To<Map>());
        iCAX::GeometryData::Persistence::Reader reader(bytes);std::uint32_t version;reader.Read(version);iCAX::GeometryData::CompositeOffsetCurve3 exact;reader.Read(exact);
        iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>());
        EXPECT_FALSE(evaluator.Sample(exact).Points.empty());
        EXPECT_EQ(0,std::count_if(exact.Segments.begin(),exact.Segments.end(),[](const auto& s){return !s.SourceSegment;}));
    }
    EXPECT_EQ(1u,trims);
    if(const auto destination=ompl_evaluation::Env(L"ICAX_ORIENTATION_DISK_OUTPUT");!destination.empty()){
        iCAX::ProjectFile::CProjectFile project({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Real disk orientation evaluation input";
        project.Save(std::filesystem::path(destination),info,scene.Database(),scene.Resources());
    }
}
TEST(FiveAxisDiskRegression, RealDiskBoundaryFramesAndGeneratedPaths) { CheckRealDiskBoundaryFramesAndGeneratedPaths(1.); }
TEST(FiveAxisDiskRegression, FiveTimesScaledDiskRetainsExteriorTrim) { CheckRealDiskBoundaryFramesAndGeneratedPaths(5.); }

// Test-only stdin transport. Every mutation/query and View/resource below is
// produced by the production DLL; the browser adapter contains no CAM model.
namespace {
    Variant BrowserWireValue(const Variant& value)
    {
        // The SDK exposes UUID variants as strings in JavaScript. Match that
        // conversion for this JSON-only process transport after the real SDO.
        if(value.Is<ID>())return iCAX::Data::to_string(value.To<ID>());
        if(value.Is<Map>()){Map result;for(const auto& [key,child]:std::get<Map>(value.m_Value))result[key]=BrowserWireValue(child);return result;}
        if(value.Is<Array>()){Array result;for(const auto& child:std::get<Array>(value.m_Value))result.emplace_back(BrowserWireValue(child));return result;}
        return value;
    }
}
TEST(FiveAxisBridge, DISABLED_Serve)
{
    File fixtureSource(BRepPrimAPI_MakeBox(30,30,60).Shape());
    File rapidFixtureSource(BRepPrimAPI_MakeBox(gp_Pnt(-.025,-2,0),.05,4,2).Shape());
    const auto projectID=iCAX::Data::GenerateNewUUID();
    auto scene=std::make_unique<Scene>(projectID,projectID,true,true,true);
    auto machineHost=std::make_unique<AssetPlanHost>(projectID);
    BindBuiltinHeadSources(*scene,InstalledHeadSources());
    // Each bridge process owns an isolated in-memory fixture library. Browser
    // acceptance must never read or write the user's product configuration.
    scene->FixtureLibraryStore=TestFixtureLibraryStore();
    File source(GetEnvironmentVariableW(L"ICAX_FIVE_AXIS_BRIDGE_BULK_CUTS",nullptr,0)?BulkRecognitionPart():
        GetEnvironmentVariableW(L"ICAX_FIVE_AXIS_BRIDGE_RECOGNITION",nullptr,0)?RecognitionPart():
        GetEnvironmentVariableW(L"ICAX_FIVE_AXIS_BRIDGE_BOX",nullptr,0)?BRepPrimAPI_MakeBox(40,60,20).Shape():BRepPrimAPI_MakeSphere(30).Shape());
    auto saved=source.Path;saved.replace_extension(".i5cam");
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    for(Text line;std::getline(std::cin,line);)
    {
        iCAX::Data::Variant requestID;Map response;bool exiting=false;
        try
        {
            const auto request=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(line).To<Map>();
            requestID=request.at("id");const auto action=request.at("action").To<Text>();Map result;
            if(action=="exit") { exiting=true;result={{"exited",true}}; }
            else if(action=="info") result={{"projectId",iCAX::Data::to_string(scene->Project)},
                {"sceneId",iCAX::Data::to_string(scene->GetSceneID())},{"sourcePath",source.Utf8()},{"fixturePath",fixtureSource.Utf8()},{"rapidFixturePath",rapidFixtureSource.Utf8()}};
            // Performance captures use the authoritative state without routing
            // an entire saved collision report through the bounded SDO queue.
            else if(action=="read-state") result=Read(*scene);
            else if(action=="prepare-machine-placement-test"){
                Imported(*scene,source);const auto machine=CreateProductPlanMachine(*scene);Text error;
                const auto support=scene->Database().GetEntity(machine.Support),tool=scene->Database().GetEntity(machine.Tool);
                if(!support->GetComponent("CMachineElementComponent")->SetProperty("WorkSupport",true,error)||!support->GetComponent("CMachineCollisionComponent")->SetProperty("Collisions",PlanBox(0,0,-.495,1),error))throw std::runtime_error(error);
                PlanAttach(tool,"CMachineToolComponent",{{"TcpLocalPosition",Array{0.,0.,0.}},{"BeamLocalDirection",Array{0.,0.,-1.}}});
                if(!tool->GetComponent("CMachineJointComponent")->SetProperty("Position",150.,error))throw std::runtime_error(error);
                File base(BRepPrimAPI_MakeBox(gp_Pnt(-20,-80,-30),40,160,20).Shape());auto import=GenerateRequest(*scene);import["sourcePath"]=base.Utf8();const auto imported=Wait(*scene,Invoke(scene.get(),"CamFixture","Import",import));
                if(imported.at("status").To<Text>()!="Completed")throw std::runtime_error("Fixture import failed");
                auto mount=GenerateRequest(*scene);mount["machineId"]=iCAX::Data::to_string(machine.Machine);result=Invoke(scene.get(),"CamSimulation","MountMachine",mount);
            }
            else if(action=="reset-rapid-probe-cache")
            {
                // Performance fixture only: force a cold automatic batch in
                // this isolated in-memory scene. Never save its test changes.
                const auto state=Read(*scene);const auto setupID=ID::from_string(state.at("activeSetupId").To<Text>()).value();
                const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene->Database().GetEntity(setupID)->GetComponent(CFiveAxisSetupComponent::S_ClassName));
                auto overrides=setup->GetRapidOverrides();
                for(auto& [_,value]:overrides){auto record=value.To<Map>();record.erase("manualPath");record["inputKey"]=Text("isolated-cold-rapid-probe");value=std::move(record);}
                auto& transaction=scene->Database().BeginTransaction("Isolated rapid performance cache reset");
                transaction.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"GeneratedRapidPaths",Map{}},{"RapidOverrides",overrides}});
                if(!scene->Database().CommitTransaction(transaction))throw std::runtime_error("Rapid probe reset rejected");
                result={{"isolatedAutomaticBatch",true},{"persistentProjectSaved",false}};
            }
            else if(action=="engineering-storage") result=EngineeringStorageAudit(*scene);
            else if(action=="machining-storage"){
                const auto state=Read(*scene);const auto id=ID::from_string(state.at("activeSetupId").To<Text>()).value();
                const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene->Database().GetEntity(id)->GetComponent(CFiveAxisSetupComponent::S_ClassName));
                const auto generated=setup->GetGeneratedToolpaths();
                const auto count=generated.contains("operations")?generated.at("operations").To<Array>().size():0;
                result={{"cutDataCount",std::to_string(count)},
                    {"generatedDataPresent",!setup->GetGeneratedToolpaths().empty()},{"rapidDataPresent",!setup->GetGeneratedRapidPaths().empty()}};
            }
            else if(action=="prepare-axis-test"){PrepareOrderPart(*scene);result=ReadStateProjection(*scene);}
            else if(action=="new-project"){
                const auto store=scene->FixtureLibraryStore;
                auto created=std::make_unique<Scene>(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),false,true,true);
                created->FixtureLibraryStore=store;InitializeScene(*created);BindFixtureLibraryStore(*created,store);BindBuiltinHeadSources(*created,InstalledHeadSources());
                scene=std::move(created);machineHost=std::make_unique<AssetPlanHost>(scene->Project);result=ReadStateProjection(*scene);
            }
            else if(action=="invoke")
            {
                const auto method=request.at("method").To<Text>();const auto dot=method.find('.');
                const bool sceneScope=request.at("scope").To<Text>()=="scene";
                // The standalone app publishes the scene between browser
                // requests. Advance the same real View projection in this
                // process fixture, including before the first View read.
                if(sceneScope){PumpTasks(*scene);scene->Views().Publish();}
                // Preserve the SDK's typed wire payload, including int coordinates.
                // StandardJsonCodec normalizes integers to long long and hides bugs.
                const auto group=method.substr(0,dot);
                if(sceneScope&&(group=="Machine"||group=="MachineDefinition"))
                    result=machineHost->Call(*scene,group,method.substr(dot+1),iCAX::Data::VariantSerializer::Deserialize(request.at("payloadText").To<Text>()).To<Map>());
                else result=InvokePayload(sceneScope?scene.get():nullptr,group,method.substr(dot+1),request.at("payloadText").To<Text>());
            }
            else if(action=="pdo-read")
            {
                PumpTasks(*scene);PumpTaskProgressPDO(*scene);scene->PDOHub().SwapOutSlot();
                const auto slot=std::stoull(request.at("slotId").To<Text>());
                const auto& arena=scene->PDOHub().GetSharedArenaName();
                if(request.at("arenaName").To<Text>()!=Text(arena.begin(),arena.end()))throw std::invalid_argument("PDO arena mismatch");
                iCAX::PDO::CPDOReadLease lease(scene->PDOHub().GetSlot(slot));Array bytes;
                const auto* data=static_cast<const std::uint8_t*>(lease.Data());for(std::size_t i=0;i<lease.PayloadSize();++i)bytes.emplace_back(static_cast<UInt>(data[i]));
                result={{"bytes",bytes},{"dataVersion",std::to_string(lease.DataVersion())}};
            }
            else if(action=="resource")
            {
                scene->Views().Publish();
                const auto url=request.at("url").To<Text>();
                auto version=scene->Resources().GetVersion(url);
                if(request.contains("version")&&!request.at("version").To<Text>().empty()) version=std::stoull(request.at("version").To<Text>());
                const auto body=scene->Resources().Get<iCAX::Resource::CFlatBufferResource>(url,version);
                if(!body)throw std::runtime_error("Missing real native resource");
                if(request.contains("encoding")&&request.at("encoding").To<Text>()=="base64"){
                    // Test transport only: avoid a Variant object per byte for
                    // large real assembly views. The production resource bytes
                    // are returned unchanged and parsed by the actual SDK.
                    Text encoded;const auto& bytes=body->Bytes();static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                    if(!request.at("head").To<bool>()){encoded.reserve((bytes.size()+2)/3*4);for(size_t i=0;i<bytes.size();i+=3){unsigned bits=static_cast<unsigned>(bytes[i])<<16;if(i+1<bytes.size())bits|=static_cast<unsigned>(bytes[i+1])<<8;if(i+2<bytes.size())bits|=bytes[i+2];encoded.push_back(alphabet[(bits>>18)&63]);encoded.push_back(alphabet[(bits>>12)&63]);encoded.push_back(i+1<bytes.size()?alphabet[(bits>>6)&63]:'=');encoded.push_back(i+2<bytes.size()?alphabet[bits&63]:'=');}}
                    result={{"base64",encoded},{"version",std::to_string(version)}};
                }else{Array bytes;if(!request.at("head").To<bool>())for(auto b:body->Bytes())bytes.emplace_back(static_cast<UInt>(b));result={{"bytes",bytes},{"version",std::to_string(version)}};}
            }
            else if(action=="undo"||action=="redo")
            {
                const bool changed=action=="undo"?scene->Database().Undo():scene->Database().Redo();
                PumpTasks(*scene);scene->Views().Publish();result={{"changed",changed},{"state",ReadStateProjection(*scene)}};
            }
            else if(action=="open-project")
            {
                const auto path=request.at("path").To<Text>();
                const auto prepared=file.PrepareOpen(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()),path.size())));
                const auto info=prepared.Result().Info;auto reopened=std::make_unique<Scene>(info.ProjectID,info.MainSceneID,false,true,true);
                reopened->FixtureLibraryStore=scene->FixtureLibraryStore;
                file.Restore(prepared,reopened->Database(),reopened->Resources());
                if(request.contains("replayJournal")&&request.at("replayJournal").To<bool>())reopened->Database().ReplayOperationLog(path+".log","ICAX_FIVE_AXIS_CAM_QSAVE",1);
                if(!request.contains("readBeforeAwake")||!request.at("readBeforeAwake").To<bool>())InitializeScene(*reopened);
                else (void)ReadStateProjection(*reopened);
                // Match initial/new-project fixtures: the installed manifest
                // supplies this catalog; it is not persisted in an archive.
                BindBuiltinHeadSources(*reopened,InstalledHeadSources());
                scene=std::move(reopened);machineHost=std::make_unique<AssetPlanHost>(scene->Project);
                result={{"projectId",iCAX::Data::to_string(scene->Project)},{"sceneId",iCAX::Data::to_string(scene->GetSceneID())}};
            }
            else if(action=="intent-operations")
            {
                Array operations,derived;for(const auto& entity:scene->Database().FilterEntities([](auto){return true;})){
                    if(const auto c=entity->GetComponent("CEditableOperationComponent"))operations.emplace_back(Map{{"id",iCAX::Data::to_string(entity->GetID())},{"definition",c->GetProperty("Definition")}});
                    if(const auto c=entity->GetComponent("CEditableDerivedComponent"))derived.emplace_back(Map{{"id",iCAX::Data::to_string(entity->GetID())},{"data",c->GetProperty("Data")}});
                }result={{"operations",operations},{"derived",derived}};
            }
            else if(action=="face-inventory")
            {
                const auto ref=Read(*scene).at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();
                const auto geometry=scene->Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
                Array faces;for(const auto& face:geometry->Faces)faces.emplace_back(Map{{"id",std::to_string(face.Id)},{"wireCount",std::to_string(face.WireIds.size())}});
                result={{"faces",faces}};
            }
            else if(action=="entity-transforms")
            {
                // Read the production Transform dependency chain directly;
                // this host intentionally has no application Behaviour tick.
                // Replay the same non-persistent pose projection called by
                // MachinePoseBehaviour::OnAwake/OnUpdate after native reopen.
                // Persistent joint coordinates and native history stay intact.
                const auto history=scene->Database().GetUndoArray().size();
                for(const auto& entity:scene->Database().FilterEntities([](auto e){return e->HasComponent("CMachineElementComponent");}))
                    iCAX::CAM::SynchronizeMachineElementPose(*entity);
                if(scene->Database().GetUndoArray().size()!=history)throw std::runtime_error("Runtime machine pose projection altered native history");
                PumpTasks(*scene);Array records;
                for(const auto& value:request.at("entityIds").To<Array>()){
                    const auto id=ID::from_string(value.To<Text>());if(!id)throw std::invalid_argument("Invalid transform entity ID");
                    const auto entity=scene->Database().GetEntity(*id);const auto transform=entity?std::dynamic_pointer_cast<iCAX::Transform::CTransformComponent>(entity->GetComponent(iCAX::Transform::CTransformComponent::S_ClassName)):nullptr;
                    if(!transform)throw std::invalid_argument("Transform entity is missing");
                    const auto world=transform->GetLocalToWorldMatrix();Array matrix;for(int r=0;r<4;++r)for(int c=0;c<4;++c)matrix.emplace_back(world(r,c));
                    records.emplace_back(Map{{"id",value},{"parentEntityId",iCAX::Data::to_string(transform->GetParentEntityID())},{"world",matrix}});
                }
                result={{"transforms",records}};
            }
            else if(action=="export-project")
            {
                const auto path=request.at("path").To<Text>();
                const auto destination=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()),path.size()));
                iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene->Project;info.MainSceneID=scene->GetSceneID();info.ProjectName="汽车冲压件 · 装夹测试";
                file.Save(destination,info,scene->Database(),scene->Resources());result={{"saved",true}};
            }
            else if(action=="save-reopen")
            {
                iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene->Project;info.MainSceneID=scene->GetSceneID();info.ProjectName="Native browser import";
                file.Save(saved,info,scene->Database(),scene->Resources());std::filesystem::remove(source.Path);
                auto reopened=std::make_unique<Scene>(scene->Project,scene->GetSceneID(),false,true,true);
                reopened->FixtureLibraryStore=scene->FixtureLibraryStore;
                file.Open(saved,reopened->Database(),reopened->Resources());InitializeScene(*reopened);BindBuiltinHeadSources(*reopened,InstalledHeadSources());scene=std::move(reopened);result=ReadStateProjection(*scene);
            }
            else throw std::invalid_argument("Unknown test action");
            response={{"id",requestID},{"ok",true},{"result",result}};
        }
        catch(const std::exception& error){response={{"id",requestID},{"ok",false},{"error",Text(error.what())}};
            if(const auto* failure=dynamic_cast<const NativeSDOStatus*>(&error))response["sdoStatus"]=failure->Status;}
        Text wire;
        try{wire=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(BrowserWireValue(response));}
        catch(const std::exception& error){wire=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Map{{"id",requestID},{"ok",false},{"error",Text("Bridge response: ")+error.what()}});}
        std::cout<<"__FIVE_RPC__"<<wire<<std::endl;
        if(exiting)break;
    }
    std::error_code error;std::filesystem::remove(saved,error);
}
}
