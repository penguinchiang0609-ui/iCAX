#include "pch.h"
#include <ApplicationContext/IApplicationContext.h>
#include <ApplicationContext/UserDataStore.h>
#include <ProductContext/IProductContext.h>
#pragma comment(lib, "ProductContext.lib")
#include <Data/VariantSerializer.h>
#include <Database/IRepository.h>
#include <Database/MetaRegistrationCatalog.h>
#include <GeometryData/BRepPersistence.h>
#include <GeometryData/GeometryData.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <ProjectContext/ISceneContext.h>
#include <ProjectFile/ProjectFile.h>
#include <Resources/ResourceLibrary.h>
#include <Resources/ResourceLoaderRegistry.h>
#include <Resources/ResourceLoaderRegistrationCatalog.h>
#include <Resources/FlatBufferResource.h>
#include <RenderData/RenderData.h>
#include <TubeDesigner/ComponentModelLibrary.h>
#include <RenderInteraction/RenderInteractionComponents.h>
#include <SDO/SDORegistrationCatalog.h>
#include <TemplateRuntime/PythonTemplateHost.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <TemplateRuntime/TemplateCodec.h>
#include <TubeDesigner/TubeDesigner.h>
#include <TubeDesigner/TubeDesignerComponents.h>
#include <TubeDesigner/DisassemblyProgress.h>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepGProp.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <cctype>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <IGESControl_Writer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <array>
#include <atomic>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace punch_persistence_acceptance {
bool logInvocations=true;
using iCAX::Data::ObjectMap;
using iCAX::Data::VariantArray;
using iCAX::TubeDesigner::CManufacturingPartComponent;
using iCAX::TubeDesigner::CPartDrawingComponent;
using iCAX::TubeDesigner::CPunchWizardComponent;
using iCAX::GeometryData::BRepModel;

class Application final : public iCAX::Application::IApplicationContext {
public:
    Application() { paths.InstallDirectory=std::filesystem::current_path().string(); }
    const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override { return descriptor; }
    const iCAX::Application::CApplicationPaths& GetPaths() const override { return paths; }
    iCAX::Data::PropertyBag GetSettings() const override { return {}; }
    const iCAX::Services::CServiceProvider& Services() const override { throw std::logic_error("Acceptance test has no application services"); }
private:
    iCAX::Application::CApplicationDescriptor descriptor;
    iCAX::Application::CApplicationPaths paths;
};

class Scene final : public iCAX::Project::ISceneContext {
public:
    Scene(iCAX::Data::uuid projectId=iCAX::Data::GenerateNewUUID(),iCAX::Data::uuid sceneId=iCAX::Data::GenerateNewUUID(),bool root=true,bool resourceExporters=false)
        : project(projectId),id(sceneId) {
        (void)iCAX::TubeDesigner::GetTubeDesignerContractVersion();
        if(resourceExporters) {
            auto registry=std::make_shared<iCAX::Resource::CResourceLoaderRegistry>();
            iCAX::Resource::CResourceLoaderRegistrationCatalog::ReplayAll(*registry);
            resources=iCAX::Resource::CResourceLibrary(std::move(registry));
        }
        auto registry=iCAX::Database::CreateMetaRegistry();
        wchar_t exe[32768]{},dbModule[32768]{},renderModule[32768]{},transformModule[32768]{};
        GetModuleFileNameW(nullptr,exe,32768);GetModuleFileNameW(GetModuleHandleW(L"Database.dll"),dbModule,32768);
        GetModuleFileNameW(GetModuleHandleW(L"RenderInteraction.dll"),renderModule,32768);
        GetModuleFileNameW(GetModuleHandleW(L"Transform.dll"),transformModule,32768);
        iCAX::Database::CMetaRegistrationCatalog::ReplayByModulePaths(*registry,
            {std::filesystem::path(dbModule).string(),std::filesystem::path(renderModule).string(),
             std::filesystem::path(transformModule).string(),std::filesystem::path(exe).string()});
        repository=iCAX::Database::GenerateRepository(id,registry);
        resources.SetScope(iCAX::Resource::MakeSceneResourceScope("icax","icax.tube-designer",project,id));
        iCAX::Resource::CResourceVersionCodec codec;
        codec.Serialize=[](const std::shared_ptr<void>& value)->std::optional<std::vector<uint8_t>> {
            return iCAX::GeometryData::Persistence::Serialize(*std::static_pointer_cast<BRepModel>(value));
        };
        codec.Deserialize=[](std::span<const uint8_t> bytes)->std::shared_ptr<void> {
            return std::make_shared<BRepModel>(iCAX::GeometryData::Persistence::Deserialize(bytes));
        };
        if(!resources.RegisterPersistenceCodec<BRepModel>(BRepModel::kResourceTypeName,codec))throw std::runtime_error("Cannot register BRep persistence");
        if(root) {
            repository->BeginLoadBaseline();
            auto entity=repository->GetMetaEntity();if(!entity)entity=repository->CreateEntity(id);
            entity->AddComponent<iCAX::TubeDesigner::CTubeDesignerRootComponent>();
            repository->EndLoadBaseline();
        }
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
    iCAX::PDO::IPDOHub& PDOHub() override { throw std::logic_error("Acceptance does not create UI/PDO"); }
    const iCAX::PDO::IPDOHub& PDOHub() const override { throw std::logic_error("Acceptance does not create UI/PDO"); }
    iCAX::Services::CServiceProvider& Services() const override { throw std::logic_error("Acceptance does not create scene services"); }
    iCAX::Data::uuid project;
private:
    iCAX::Data::uuid id;
    std::string name="Punch persistence acceptance";
    std::shared_ptr<iCAX::Database::IRepository> repository;
    iCAX::Resource::CResourceLibrary resources;
};

ObjectMap invoke(Scene& scene,const std::string& method,const ObjectMap& payload,iCAX::Product::IProductContext* product=nullptr,
    iCAX::Interaction::CInvocation::ReportHandler report={}) {
    Application application;iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(registry);
    iCAX::Interaction::CInvocation request;request.nCallID=1;
    if(report)request.SetReportHandler(std::move(report));
    request.Method=iCAX::Interaction::MakeSDOMethod("TubeDesigner",method);
    const auto json=iCAX::Data::VariantSerializer::Serialize(payload);request.Payload.assign(json.begin(),json.end());
    const auto sdo=registry.Find(request.Method.nSDOCode);
    if(!sdo||!sdo->HasMethod(request.Method.nMethodCode))throw std::runtime_error("Missing real SDO: "+method);
    if(logInvocations)std::cout<<"[persistence-sdo] "<<method<<std::endl;
    const auto invokeStart=std::chrono::steady_clock::now();
    const auto result=sdo->Invoke(request,application,product,nullptr,&scene);
    if(!result.IsOK())throw std::runtime_error(method+": "+result.strError);
    const auto backendSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-invokeStart).count();
    auto decoded=iCAX::Data::VariantSerializer::Deserialize(std::string(result.Payload.begin(),result.Payload.end())).To<ObjectMap>();
    char* profileEnv=nullptr; std::size_t profileEnvLength=0;
    (void)_dupenv_s(&profileEnv,&profileEnvLength,"ICAX_PROFILE_TEMPLATE_PREVIEW");
    const bool profileEnabled=profileEnv!=nullptr; std::free(profileEnv);
    if(method=="GeneratePreview" && profileEnabled)
        std::cout<<"[persistence-sdo] invoke="<<backendSeconds<<"s decode="
            <<std::chrono::duration<double>(std::chrono::steady_clock::now()-invokeStart).count()-backendSeconds
            <<"s payload="<<result.Payload.size()<<" bytes\n";
    return decoded;
}
TEST(ProfileLibrarySDOTest, SystemCatalogDoesNotRequirePersonalData)
{
    Scene scene;
    const auto result=invoke(scene,"ListSystemProfiles",{});
    const auto profiles=result.at("systemProfiles").To<VariantArray>();
    ASSERT_EQ(21u,profiles.size());
    for(const auto& value:profiles) {
        const auto profile=value.To<ObjectMap>();
        EXPECT_FALSE(profile.contains("error"));
        EXPECT_FALSE(profile.contains("resources"));
        EXPECT_TRUE(profile.at("capabilities").To<ObjectMap>().at("preview").To<bool>());
    }
}

std::filesystem::path acceptanceRuntimePath(const std::vector<std::filesystem::path>& candidates) {
    const auto root=std::filesystem::current_path();
    for(const auto& candidate:candidates) {
        const auto path=root/candidate;
        if(std::filesystem::exists(path))return path;
    }
    throw std::runtime_error("Acceptance runtime file was not found in the working directory");
}
ObjectMap runtime(const std::string& file,const ObjectMap& input) {
    const auto worker=acceptanceRuntimePath({"runtime/template-python/icax_template_worker.py",
        "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py"});
    const auto python=acceptanceRuntimePath({"runtime/python/python312.dll",
        "src/x64/Debug/runtime/python/python312.dll","src/x64/Release/runtime/python/python312.dll"});
    const auto shared=acceptanceRuntimePath({"apps/tube-designer/templates/_shared",
        "src/apps/tube-designer/templates/_shared"});
    iCAX::TemplateRuntime::CPythonTemplateHost host({python,worker,worker.parent_path()});
    return host.Invoke({{"protocol",std::string("icax.template-runtime")},{"protocolVersion",1ull},{"operation",std::string("evaluate")},
        {"templatePath",(shared/file).string()},
        {"template",ObjectMap{{"id",std::string("persistence-test")},{"version",std::string("1.0.0")},{"packageDigest",std::string("test")}}},
        {"parameters",input},{"context",ObjectMap{}}});
}
ObjectMap systemProfile(const std::string& id,ObjectMap values={}) {
    const auto profileRoot=acceptanceRuntimePath({"apps/tube-designer/templates/profile",
        "src/apps/tube-designer/templates/profile"});
    return runtime("profile_package_runtime.py",{{"action",std::string("evaluate-system")},{"systemProfileId",id},{"values",values},
        {"profileRoot",profileRoot.string()}}).at("profile").To<ObjectMap>();
}

TEST(ProfileLibrarySDOTest, DirectRecognitionReturnsParametersAndPoseWithoutGeneratingCandidates)
{
    Scene scene;
    const auto input=systemProfile("rect",{{"width",55.0},{"depth",33.0},
        {"wallThickness",2.0},{"cornerRadius",4.0},{"innerRadius",2.0}});
    const auto guard=(std::filesystem::current_path()/
        "src/tests/icax-plugins/geometry/ExtrusionRecognition/ExtrusionRecognitionTest/DirectFittingTestRuntime.py").string();
    runtime(guard,{{"action",std::string("guard")}});
    ObjectMap result;
    try {
        result=invoke(scene,"RecognizeProfileSection",{{"scope",std::string("system")},
            {"profileId",std::string("rect")},{"section",input}});
    } catch(...) {
        runtime(guard,{{"action",std::string("unguard")}});
        throw;
    }
    const auto calls=runtime(guard,{{"action",std::string("unguard")}});
    EXPECT_GT(calls.at("fittingCalls").To<std::int64_t>(),0);
    ASSERT_TRUE(result.at("matched").To<bool>());
    const auto items=result.at("results").To<VariantArray>();
    ASSERT_EQ(1u,items.size());
    const auto candidates=items.front().To<ObjectMap>().at("candidates").To<VariantArray>();
    ASSERT_EQ(1u,candidates.size());
    const auto candidate=candidates.front().To<ObjectMap>();
    EXPECT_FALSE(candidate.contains("profile"));
    EXPECT_TRUE(candidate.contains("rotationDegrees"));
    EXPECT_TRUE(candidate.contains("translation"));
    const auto parameters=candidate.at("parameters").To<ObjectMap>();
    EXPECT_NEAR(55.0,parameters.at("width").To<double>(),1e-8);
    EXPECT_NEAR(33.0,parameters.at("depth").To<double>(),1e-8);
    EXPECT_NEAR(2.0,parameters.at("wallThickness").To<double>(),1e-8);
}

TEST(ProfileLibrarySDOTest, ImportedStepPersistsDirectFittingParameters)
{
    Scene scene;
    const auto guard=(std::filesystem::current_path()/
        "src/tests/icax-plugins/geometry/ExtrusionRecognition/ExtrusionRecognitionTest/DirectFittingTestRuntime.py").string();
    runtime(guard,{{"action",std::string("guard")}});
    ObjectMap result;
    try {
        result=invoke(scene,"ImportNestingPart",{{"sourcePath",(std::filesystem::current_path()/
            "samples/tube-one/01_round_tube_plain.step").string()}});
    } catch(...) {
        runtime(guard,{{"action",std::string("unguard")}});
        throw;
    }
    const auto calls=runtime(guard,{{"action",std::string("unguard")}});
    EXPECT_GT(calls.at("fittingCalls").To<std::int64_t>(),0);
    const auto profile=result.at("profile").To<ObjectMap>();
    EXPECT_EQ("round",profile.at("kind").To<std::string>());
    const auto parameters=profile.at("parameters").To<ObjectMap>();
    EXPECT_GT(parameters.at("width").To<double>(),0.0);
    EXPECT_GT(parameters.at("wallThickness").To<double>(),0.0);
    EXPECT_EQ(2u,profile.at("contours").To<VariantArray>().size());
    EXPECT_TRUE(profile.contains("placement"));
}

ObjectMap dxfProfile() {
    return runtime("dxf_profile_importer.py",{{"sourcePath",(std::filesystem::current_path()/
        "src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/PartDrawingSection.dxf").string()}}).at("profile").To<ObjectMap>();
}
ObjectMap section(const ObjectMap& profile,const std::string& source="library") { return {{"source",source},{"profile",profile}}; }
ObjectMap end(const std::string& id,ObjectMap parameters={},double trim=0,ObjectMap profileSection={}) {
    ObjectMap e{{"type",std::string("template")},{"datum",std::string("long")},{"rotation",0.},{"trim",trim},
        {"toolRef",ObjectMap{{"id",id}}},{"toolParameters",parameters}};
    if(!profileSection.empty())e["section"]=profileSection;return e;
}
ObjectMap request(const std::string& name,const std::string& profileId,ObjectMap ends={},VariantArray features={}) {
    return {{"name",name},{"quantity",2ull},{"material",std::string("acceptance-only")},
        {"drawing",ObjectMap{{"schemaVersion",1ull},{"length",1000.},{"section",section(systemProfile(profileId))}}},
        {"features",features},{"ends",ends}};
}

ObjectMap resolveAssemblyRequestSections(ObjectMap planned) {
    const auto resolveSection=[](ObjectMap item) {
        if(!item.contains("section"))return item;
        auto value=item.at("section").To<ObjectMap>();
        if(value.contains("profile")||!value.contains("profileRef"))return item;
        const auto reference=value.at("profileRef").To<ObjectMap>();
        const auto parameters=value.contains("parameters")?value.at("parameters").To<ObjectMap>():ObjectMap{};
        item["section"]=section(systemProfile(reference.at("id").To<std::string>(),parameters));
        return item;
    };
    auto ends=planned.at("ends").To<ObjectMap>();
    for(auto& [key,value]:ends)value=resolveSection(value.To<ObjectMap>());
    auto features=planned.at("features").To<VariantArray>();
    for(auto& value:features)value=resolveSection(value.To<ObjectMap>());
    planned["ends"]=ends;
    planned["features"]=features;
    return planned;
}

ObjectMap previewAssemblyManufacturingPart(Scene& scene,const ObjectMap& planned,
    const std::string& previewResourceKey={}) {
    const auto request=resolveAssemblyRequestSections(planned);
    const auto reference=request.at("profileRef").To<ObjectMap>();
    const auto parameters=request.at("parameters").To<ObjectMap>();
    ObjectMap payload{
        {"name",std::string("合并装配模板原生预览")},{"quantity",1ull},{"material",std::string("acceptance-only")},
        {"drawing",ObjectMap{{"schemaVersion",1ull},{"length",request.at("length")},
            {"section",section(systemProfile(reference.at("id").To<std::string>(),parameters))}}},
        {"features",request.at("features")},{"ends",request.at("ends")},{"diagnosticBooleanPreview",true},
    };
    if(!previewResourceKey.empty())payload["previewResourceKey"]=previewResourceKey;
    return invoke(scene,"PreviewPunchWizard",payload);
}

TEST(AssemblyTemplateSDOTest, ResolvesScenePartsAndBuildsManufacturingGeometry)
{
    Scene scene;
    const auto catalogue=invoke(scene,"GetAssemblyTemplates",{});
    EXPECT_TRUE(catalogue.at("errors").To<VariantArray>().empty());
    const auto assemblies=catalogue.at("assemblies").To<VariantArray>();
    ASSERT_GE(assemblies.size(),5u);
    const auto findAssembly=[&](const std::string& id) -> ObjectMap {
        for(const auto& value:assemblies) {
            const auto item=value.To<ObjectMap>();
            if(item.at("id").To<std::string>()==id)return item;
        }
        throw std::runtime_error("Missing assembly template: "+id);
    };
    const auto coldBend=findAssembly("bend");
    const auto segmentedBend=findAssembly("segmented-bend");
    for(const auto& value:coldBend.at("parameters").To<VariantArray>()) {
        const auto key=value.To<ObjectMap>().at("key").To<std::string>();
        EXPECT_NE(key,"bendMethod");
        EXPECT_NE(key,"slotProcess");
    }
    EXPECT_NE(coldBend.at("id").To<std::string>(),segmentedBend.at("id").To<std::string>());

    const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("mechanical-fastener")},
        {"parameters",ObjectMap{{"nominalDiameter",12.0},{"holeClearance",1.0},
            {"count",3},{"pitch",45.0},{"washer",std::string("both")}}},
        {"processDrafts",ObjectMap{}},
    });
    EXPECT_EQ(plan.at("schema").To<std::string>(),"icax.assembly-preview-plan");
    EXPECT_EQ(plan.at("templateId").To<std::string>(),"mechanical-fastener");
    const auto workflow=plan.at("resolvedWorkflow").To<ObjectMap>();
    EXPECT_EQ(workflow.at("scope").To<std::string>(),"template-example");
    EXPECT_EQ(workflow.at("blankParts").To<VariantArray>().size(),2u);
    EXPECT_EQ(workflow.at("partOperations").To<VariantArray>().size(),2u);
    EXPECT_FALSE(workflow.at("assemblySteps").To<VariantArray>().empty());
    EXPECT_FALSE(workflow.at("bom").To<VariantArray>().empty());
    const auto design=plan.at("designParts").To<VariantArray>();
    const auto manufacturing=plan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(design.size(),2u);
    ASSERT_EQ(manufacturing.size(),2u);
    for(const auto& value:design)EXPECT_EQ(value.To<ObjectMap>().at("matrix").To<VariantArray>().size(),16u);

    const auto generated=manufacturing.front().To<ObjectMap>().at("request").To<ObjectMap>();
    ObjectMap manufacturingRequest{
        {"name",std::string("装配下料预览")},{"quantity",1ull},{"material",std::string("acceptance-only")},
        {"drawing",ObjectMap{{"schemaVersion",1ull},{"length",generated.at("length")},
            {"section",section(systemProfile("rect",{{"width",80.0},{"depth",20.0},{"wallThickness",2.0},{"cornerRadius",2.0},{"innerRadius",1.0}}))}}},
        {"features",generated.at("features")},{"ends",generated.at("ends")},{"diagnosticBooleanPreview",true},
    };
    const auto preview=invoke(scene,"PreviewPunchWizard",manufacturingRequest);
    ASSERT_TRUE(preview.contains("previewComputed"));
    EXPECT_TRUE(preview.at("previewComputed").To<bool>());
    EXPECT_TRUE(preview.contains("baseGeometry"));
    EXPECT_TRUE(preview.contains("geometry"));
    if(preview.contains("resultValid"))EXPECT_TRUE(preview.at("resultValid").To<bool>());

    // The embedded notch's retained local arc is a tool parameter, not the
    // finished bend radius.  Its default must remain valid for the standard
    // 40 mm-deep preview tube when selected as its own assembly template.
    const auto embeddedPlan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("node-embedded-arc-integrated")},
        {"parameters",ObjectMap{{"angle",90.0}}},
        {"processDrafts",ObjectMap{}},
    });
    const auto embeddedParts=embeddedPlan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(embeddedParts.size(),1u);
    const auto embeddedRequest=embeddedParts.front().To<ObjectMap>().at("request").To<ObjectMap>();
    const auto embeddedFeatures=embeddedRequest.at("features").To<VariantArray>();
    ASSERT_EQ(embeddedFeatures.size(),1u);
    const auto embeddedParameters=embeddedFeatures.front().To<ObjectMap>().at("toolParameters").To<ObjectMap>();
    const auto& arcRadius=embeddedParameters.at("arcRadius");
    const auto arcRadiusValue=arcRadius.Is<double>() ? arcRadius.To<double>()
        : arcRadius.Is<std::int64_t>() ? static_cast<double>(arcRadius.To<std::int64_t>())
        : static_cast<double>(arcRadius.To<int>());
    EXPECT_EQ(arcRadiusValue,10.0);
    EXPECT_FALSE(embeddedParameters.contains("bendRadius"));
    ObjectMap embeddedPreviewRequest{
        {"name",std::string("装配嵌入圆弧槽预览")},{"quantity",1ull},{"material",std::string("acceptance-only")},
        {"drawing",ObjectMap{{"schemaVersion",1ull},{"length",embeddedRequest.at("length")},
            {"section",section(systemProfile("rect"))}}},
        {"features",embeddedRequest.at("features")},{"ends",embeddedRequest.at("ends")},{"diagnosticBooleanPreview",true},
    };
    const auto embeddedPreview=invoke(scene,"PreviewPunchWizard",embeddedPreviewRequest);
    EXPECT_FALSE(embeddedPreview.contains("resultError"))
        <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(embeddedPreview);
    EXPECT_TRUE(embeddedPreview.at("previewComputed").To<bool>());
    EXPECT_TRUE(embeddedPreview.contains("geometry"));

    // The edge-arc fold is an independent assembly template.  Its bridge and
    // relief still come from the linked single-part groove resource.
    const auto edgeArcPlan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("node-edge-arc-integrated")},
        {"parameters",ObjectMap{{"angle",90.0}}},
        {"processDrafts",ObjectMap{{"node-slot",ObjectMap{{"edge-arc-groove",ObjectMap{
            {"bridge",2.0},{"reliefDiameter",10.0},{"reliefLift",0.0},{"reliefDepth",0.0},
            {"reliefSide",std::string("negative")},{"bottomCut",false}}}}}}},
    });
    const auto edgeArcParts=edgeArcPlan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(edgeArcParts.size(),1u);
    const auto edgeArcRequest=edgeArcParts.front().To<ObjectMap>().at("request").To<ObjectMap>();
    const auto edgeArcFeatures=edgeArcRequest.at("features").To<VariantArray>();
    ASSERT_EQ(edgeArcFeatures.size(),1u);
    const auto edgeArcParameters=edgeArcFeatures.front().To<ObjectMap>().at("toolParameters").To<ObjectMap>();
    EXPECT_TRUE(edgeArcParameters.contains("bridge"));
    EXPECT_TRUE(edgeArcParameters.contains("leftArc"));
    EXPECT_EQ(edgeArcParameters.at("bridge").To<double>(),2.0);
    EXPECT_EQ(edgeArcParameters.at("reliefDiameter").To<double>(),10.0);
    EXPECT_FALSE(edgeArcParameters.at("bottomCut").To<bool>());
    EXPECT_FALSE(edgeArcParameters.contains("bendRadius"));
    ObjectMap edgeArcPreviewRequest{
        {"name",std::string("装配边弧槽预览")},{"quantity",1ull},{"material",std::string("acceptance-only")},
        {"drawing",ObjectMap{{"schemaVersion",1ull},{"length",edgeArcRequest.at("length")},
            {"section",section(systemProfile("rect"))}}},
        {"features",edgeArcRequest.at("features")},{"ends",edgeArcRequest.at("ends")},{"diagnosticBooleanPreview",true},
    };
    const auto edgeArcPreview=invoke(scene,"PreviewPunchWizard",edgeArcPreviewRequest);
    EXPECT_FALSE(edgeArcPreview.contains("resultError"))
        <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(edgeArcPreview);
    EXPECT_TRUE(edgeArcPreview.at("previewComputed").To<bool>());
    EXPECT_TRUE(edgeArcPreview.contains("geometry"));
}

TEST(AssemblyTemplateSDOTest, PlainAssemblyPreviewReusesItsBlankGeometry)
{
    Scene scene;
    auto payload = request("无加工装配构件", "rect");
    payload["previewResourceKey"] = std::string("plain-assembly-member");
    payload["diagnosticBooleanPreview"] = true;
    const auto first = invoke(scene, "PreviewPunchWizard", payload);
    ASSERT_TRUE(first.at("previewComputed").To<bool>());
    ASSERT_TRUE(first.at("resultValid").To<bool>());
    EXPECT_EQ(first.at("baseGeometry"), first.at("geometry"));
    EXPECT_EQ(first.at("toolCount").To<unsigned long long>(), 0ull);
    const auto second = invoke(scene, "PreviewPunchWizard", payload);
    EXPECT_EQ(second.at("baseGeometry"), second.at("geometry"));
    EXPECT_EQ(first.at("baseGeometry"), second.at("baseGeometry"));
}

TEST(AssemblyTemplateSDOTest, CrossThroughCutsOneContinuousHostAndKeepsTheOtherBlank)
{
    Scene scene;
    const auto plan = invoke(scene, "ResolveAssemblyTemplatePreview", ObjectMap{
        {"templateId", std::string("cross-through")},
        {"parameters", ObjectMap{{"fitGap", 0.2}}},
    });
    ASSERT_TRUE(plan.contains("designParts"));
    ASSERT_TRUE(plan.contains("manufacturingParts"));
    const auto design = plan.at("designParts").To<VariantArray>();
    const auto manufacturing = plan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(design.size(), 2u);
    ASSERT_EQ(manufacturing.size(), 2u);
    const auto host = manufacturing[0].To<ObjectMap>();
    const auto through = manufacturing[1].To<ObjectMap>();
    ASSERT_TRUE(host.contains("request"));
    ASSERT_TRUE(through.contains("request"));
    const auto hostRequest = host.at("request").To<ObjectMap>();
    const auto throughRequest = through.at("request").To<ObjectMap>();
    ASSERT_TRUE(hostRequest.contains("features"));
    ASSERT_TRUE(throughRequest.contains("features"));
    ASSERT_TRUE(hostRequest.contains("length"));
    ASSERT_TRUE(throughRequest.contains("length"));
    ASSERT_EQ(hostRequest.at("features").To<VariantArray>().size(), 1u);
    EXPECT_TRUE(throughRequest.at("features").To<VariantArray>().empty());
    EXPECT_EQ(hostRequest.at("length").To<double>(), 440.0);
    EXPECT_EQ(throughRequest.at("length").To<double>(), 440.0);

    const auto hostPreview = previewAssemblyManufacturingPart(scene, hostRequest, "cross-through-host");
    ASSERT_TRUE(hostPreview.at("previewComputed").To<bool>());
    ASSERT_TRUE(hostPreview.at("resultValid").To<bool>());
    EXPECT_GT(hostPreview.at("toolCount").To<unsigned long long>(), 0ull);
    EXPECT_NE(hostPreview.at("baseGeometry"), hostPreview.at("geometry"));
    const auto throughPreview = previewAssemblyManufacturingPart(scene, throughRequest, "cross-through-plain");
    ASSERT_TRUE(throughPreview.at("previewComputed").To<bool>());
    ASSERT_TRUE(throughPreview.at("resultValid").To<bool>());
    EXPECT_EQ(throughPreview.at("baseGeometry"), throughPreview.at("geometry"));

    const auto hostModel = scene.Resources().Get<BRepModel>(scene.Resources().MakeNamedResourceURL(
        "tube-designer/punch-preview/assembly/cross-through-host"));
    const auto throughModel = scene.Resources().Get<BRepModel>(scene.Resources().MakeNamedResourceURL(
        "tube-designer/punch-preview/assembly/cross-through-plain/base"));
    ASSERT_TRUE(hostModel);
    ASSERT_TRUE(throughModel);
    const auto hostShape = iCAX::OpenCascade::BuildOpenCascadeShape(*hostModel);
    const auto throughShape = iCAX::OpenCascade::BuildOpenCascadeShape(*throughModel);
    ASSERT_TRUE(hostShape.bOK);
    ASSERT_TRUE(throughShape.bOK);
    const auto place = [](const TopoDS_Shape& shape, const ObjectMap& designPart) {
        const auto matrix = designPart.at("matrix").To<VariantArray>();
        const auto number = [](const iCAX::Data::Variant& value) -> double {
            if (value.Is<double>()) return value.To<double>();
            if (value.Is<std::int64_t>()) return static_cast<double>(value.To<std::int64_t>());
            if (value.Is<std::uint64_t>()) return static_cast<double>(value.To<std::uint64_t>());
            return static_cast<double>(value.To<int>());
        };
        gp_Trsf transform;
        transform.SetValues(number(matrix[0]), number(matrix[1]), number(matrix[2]), number(matrix[3]),
            number(matrix[4]), number(matrix[5]), number(matrix[6]), number(matrix[7]),
            number(matrix[8]), number(matrix[9]), number(matrix[10]), number(matrix[11]));
        return BRepBuilderAPI_Transform(shape, transform, true).Shape();
    };
    const auto finishedHost = place(hostShape.Shape, design[0].To<ObjectMap>());
    const auto finishedThrough = place(throughShape.Shape, design[1].To<ObjectMap>());
    BRepAlgoAPI_Common overlap(finishedHost, finishedThrough);
    ASSERT_TRUE(overlap.IsDone());
    GProp_GProps overlapVolume;
    BRepGProp::VolumeProperties(overlap.Shape(), overlapVolume);
    EXPECT_NEAR(overlapVolume.Mass(), 0.0, 0.1);
}

TEST(AssemblyTemplateSDOTest, SceneStockOverridesStaySeparateFromMiterProcessParameters)
{
    Scene scene;
    const ObjectMap sectionParameters{
        {"width",72.0},{"depth",44.0},{"wallThickness",2.0},
        {"cornerRadius",3.0},{"innerRadius",1.0},
    };
    const ObjectMap profileRef{{"scope",std::string("system")},{"id",std::string("rect")}};
    const ObjectMap sceneParts{
        {"memberA",ObjectMap{{"profileRef",profileRef},{"parameters",sectionParameters},{"length",310.0}}},
        {"memberB",ObjectMap{{"profileRef",profileRef},{"parameters",sectionParameters},{"length",190.0}}},
    };
    const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("two-end-end-angle")},
        {"parameters",ObjectMap{{"fitGap",0.0}}},
        {"sceneParameters",ObjectMap{{"jointAngle",90.0},{"planeRotation",0.0}}},
        {"processDrafts",ObjectMap{}},
        {"sceneParts",sceneParts},
    });
    const auto processParameters=plan.at("parameters").To<ObjectMap>();
    ASSERT_EQ(processParameters.size(),1u);
    EXPECT_DOUBLE_EQ(processParameters.at("fitGap").To<double>(),0.0);
    EXPECT_FALSE(processParameters.contains("jointAngle"));
    EXPECT_FALSE(processParameters.contains("planeRotation"));
    EXPECT_FALSE(processParameters.contains("width"));
    EXPECT_FALSE(processParameters.contains("length"));
    const auto resolvedSceneParameters=plan.at("sceneParameters").To<ObjectMap>();
    ASSERT_EQ(resolvedSceneParameters.size(),2u);
    EXPECT_DOUBLE_EQ(resolvedSceneParameters.at("jointAngle").To<double>(),90.0);
    EXPECT_FALSE(resolvedSceneParameters.contains("fitGap"));
    EXPECT_DOUBLE_EQ(resolvedSceneParameters.at("planeRotation").To<double>(),0.0);

    const auto resolvedScene=plan.at("sceneParts").To<ObjectMap>();
    ASSERT_EQ(resolvedScene.size(),2u);
    for(const auto& [role,length]:std::array<std::pair<const char*,double>,2>{{
            {"memberA",310.0},{"memberB",190.0}}}) {
        const auto part=resolvedScene.at(role).To<ObjectMap>();
        EXPECT_EQ(part.at("profileRef").To<ObjectMap>().at("id").To<std::string>(),"rect");
        EXPECT_DOUBLE_EQ(part.at("length").To<double>(),length);
        const auto parameters=part.at("parameters").To<ObjectMap>();
        EXPECT_DOUBLE_EQ(parameters.at("width").To<double>(),72.0);
        EXPECT_DOUBLE_EQ(parameters.at("depth").To<double>(),44.0);
    }

    const auto expectRequest=[&](const ObjectMap& part,const std::string& role) {
        const auto request=part.at("request").To<ObjectMap>();
        const double expectedLength=role=="memberA"?310.0:190.0;
        EXPECT_DOUBLE_EQ(request.at("length").To<double>(),expectedLength);
        EXPECT_EQ(request.at("profileRef").To<ObjectMap>().at("id").To<std::string>(),"rect");
        const auto parameters=request.at("parameters").To<ObjectMap>();
        EXPECT_DOUBLE_EQ(parameters.at("width").To<double>(),72.0);
        EXPECT_DOUBLE_EQ(parameters.at("depth").To<double>(),44.0);
    };
    const auto designs=plan.at("designParts").To<VariantArray>();
    ASSERT_EQ(designs.size(),2u);
    for(const auto& value:designs) {
        const auto part=value.To<ObjectMap>();
        expectRequest(part,part.at("role").To<std::string>());
    }
    const auto blanks=plan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(blanks.size(),2u);
    for(const auto& value:blanks) {
        const auto part=value.To<ObjectMap>();
        expectRequest(part,part.at("sourceRole").To<std::string>());
    }

    const ObjectMap roundRef{{"scope",std::string("system")},{"id",std::string("round")}};
    const ObjectMap roundParameters{
        {"width",50.0},{"wallThickness",2.0},{"innerOffsetX",0.0},{"innerOffsetY",0.0},
    };
    const auto round=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("two-end-end-angle")},
        {"parameters",ObjectMap{{"fitGap",0.0}}},
        {"sceneParameters",ObjectMap{{"jointAngle",90.0},{"planeRotation",0.0}}},
        {"sceneParts",ObjectMap{
            {"memberA",ObjectMap{{"profileRef",roundRef},{"parameters",roundParameters},{"length",340.0}}},
            {"memberB",ObjectMap{{"profileRef",roundRef},{"parameters",roundParameters},{"length",210.0}}},
        }},
    });
    const auto roundScene=round.at("sceneParts").To<ObjectMap>();
    ASSERT_EQ(roundScene.size(),2u);
    for(const auto& [role,length]:std::array<std::pair<const char*,double>,2>{{
            {"memberA",340.0},{"memberB",210.0}}}) {
        const auto stock=roundScene.at(role).To<ObjectMap>();
        EXPECT_EQ(stock.at("profileRef").To<ObjectMap>().at("id").To<std::string>(),"round");
        EXPECT_DOUBLE_EQ(stock.at("length").To<double>(),length);
        EXPECT_DOUBLE_EQ(stock.at("parameters").To<ObjectMap>().at("width").To<double>(),50.0);
        EXPECT_DOUBLE_EQ(stock.at("parameters").To<ObjectMap>().at("wallThickness").To<double>(),2.0);
    }
    for(const auto& field:std::array<const char*,2>{"designParts","manufacturingParts"}) {
        const auto parts=round.at(field).To<VariantArray>();
        ASSERT_EQ(parts.size(),2u);
        for(const auto& value:parts) {
            const auto part=value.To<ObjectMap>();
            const auto role=part.at(field==std::string("designParts")?"role":"sourceRole").To<std::string>();
            const auto request=part.at("request").To<ObjectMap>();
            EXPECT_EQ(request.at("profileRef").To<ObjectMap>().at("id").To<std::string>(),"round");
            EXPECT_DOUBLE_EQ(request.at("length").To<double>(),role=="memberA"?340.0:210.0);
            EXPECT_DOUBLE_EQ(request.at("parameters").To<ObjectMap>().at("width").To<double>(),50.0);
        }
    }

    const auto legacy=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("two-end-end-angle")},
        {"parameters",ObjectMap{{"jointAngle",75.0},{"fitGap",2.0},{"planeRotation",0.0}}},
    });
    const auto legacyEcho=legacy.at("parameters").To<ObjectMap>();
    ASSERT_EQ(legacyEcho.size(),3u);
    EXPECT_DOUBLE_EQ(legacyEcho.at("jointAngle").To<double>(),75.0);
    EXPECT_DOUBLE_EQ(legacyEcho.at("fitGap").To<double>(),2.0);
    EXPECT_DOUBLE_EQ(legacyEcho.at("planeRotation").To<double>(),0.0);
}

TEST(AssemblyTemplateSDOTest, SleeveClearanceAndLockingResolveThroughNativeInterface)
{
    Scene scene;
    const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("insert-sleeve")},
        {"parameters",ObjectMap{{"fitClearance",2.0},{"lockMethod",std::string("bolt")}}},
        {"processDrafts",ObjectMap{}},
    });
    const auto manufacturing=plan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(manufacturing.size(),2u);
    const auto deeper=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("insert-sleeve")},
        {"parameters",ObjectMap{{"fitClearance",2.0},{"lockMethod",std::string("bolt")},{"insertDepth",150.0}}},
        {"processDrafts",ObjectMap{}},
    });
    EXPECT_FALSE(deeper.at("sceneParameters").To<ObjectMap>().contains("insertDepth"));
    EXPECT_DOUBLE_EQ(deeper.at("parameters").To<ObjectMap>().at("insertDepth").To<double>(),150.0);
    const auto productParts=plan.at("designParts").To<VariantArray>();
    const auto deeperParts=deeper.at("designParts").To<VariantArray>();
    ASSERT_EQ(productParts.size(),deeperParts.size());
    for (std::size_t part=0;part<productParts.size();++part) {
        const auto initial=productParts[part].To<ObjectMap>().at("matrix").To<VariantArray>();
        const auto changed=deeperParts[part].To<ObjectMap>().at("matrix").To<VariantArray>();
        ASSERT_EQ(initial.size(),changed.size());
        for (std::size_t component=0;component<initial.size();++component)
            EXPECT_NEAR(initial[component].To<double>(),changed[component].To<double>(),1e-7);
    }
    const auto receive=manufacturing[1].To<ObjectMap>().at("request").To<ObjectMap>();
    const auto section=receive.at("parameters").To<ObjectMap>();
    EXPECT_NEAR(section.at("width").To<double>(),34.6,1e-7);
    const auto workflow=plan.at("resolvedWorkflow").To<ObjectMap>();
    EXPECT_EQ(workflow.at("validationStatus").To<std::string>(),"failed");
    const auto bom=workflow.at("bom").To<VariantArray>();
    ASSERT_EQ(bom.size(),1u);
    EXPECT_EQ(bom.front().To<ObjectMap>().at("label").To<std::string>(),"套接锁止螺栓");
    const auto checks=workflow.at("checks").To<VariantArray>();
    EXPECT_TRUE(std::any_of(checks.begin(),checks.end(),[](const auto& value) {
        return value.To<ObjectMap>().at("status").To<std::string>()=="requires-definition";
    }));
    const auto fitted=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("insert-sleeve")},
        {"parameters",ObjectMap{{"fitClearance",2.0},{"lockMethod",std::string("bolt")}}},
        {"sceneParts",ObjectMap{{"receivePart",ObjectMap{
            {"parameters",ObjectMap{{"width",38.0}}},
        }}}},
    });
    const auto fittedScene=fitted.at("sceneParts").To<ObjectMap>();
    const auto fittedReceive=fittedScene.at("receivePart").To<ObjectMap>();
    const auto fittedParameters=fittedReceive.at("parameters").To<ObjectMap>();
    EXPECT_NEAR(fittedParameters.at("width").To<double>(),38.0,1e-7);
    const auto& wallThickness=fittedParameters.at("wallThickness");
    const auto wallThicknessValue=wallThickness.Is<double>() ? wallThickness.To<double>()
        : wallThickness.Is<std::int64_t>() ? static_cast<double>(wallThickness.To<std::int64_t>())
        : wallThickness.Is<std::uint64_t>() ? static_cast<double>(wallThickness.To<std::uint64_t>())
        : static_cast<double>(wallThickness.To<int>());
    EXPECT_DOUBLE_EQ(wallThicknessValue,2.0);
    EXPECT_EQ(fitted.at("resolvedWorkflow").To<ObjectMap>().at("validationStatus").To<std::string>(),
        "requires-definition");
}

TEST(AssemblyTemplateSDOTest, SaddleSceneAngleChangesPoseAndEndCutWithoutChangingProcessInputs)
{
    Scene scene;
    const auto resolve=[&](double angle) {
        return invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string("saddle-weld")},
            {"parameters",ObjectMap{{"fitGap",0.5}}},
            {"sceneParameters",ObjectMap{{"intersectionAngle",angle}}},
        });
    };
    const auto perpendicular=resolve(90.0);
    const auto angled=resolve(60.0);
    const auto baselineProcess=perpendicular.at("parameters").To<ObjectMap>();
    const auto angledProcess=angled.at("parameters").To<ObjectMap>();
    EXPECT_EQ(baselineProcess.size(),angledProcess.size());
    EXPECT_FALSE(angledProcess.contains("intersectionAngle"));
    EXPECT_DOUBLE_EQ(angledProcess.at("fitGap").To<double>(),0.5);
    EXPECT_DOUBLE_EQ(perpendicular.at("sceneParameters").To<ObjectMap>()
        .at("intersectionAngle").To<double>(),90.0);
    EXPECT_DOUBLE_EQ(angled.at("sceneParameters").To<ObjectMap>()
        .at("intersectionAngle").To<double>(),60.0);

    const auto baselineDesign=perpendicular.at("designParts").To<VariantArray>();
    const auto angledDesign=angled.at("designParts").To<VariantArray>();
    ASSERT_EQ(baselineDesign.size(),2u);
    ASSERT_EQ(angledDesign.size(),2u);
    const auto baselineMatrix=baselineDesign[0].To<ObjectMap>().at("matrix").To<VariantArray>();
    const auto angledMatrix=angledDesign[0].To<ObjectMap>().at("matrix").To<VariantArray>();
    EXPECT_NEAR(baselineMatrix[0].To<double>(),0.0,1e-7);
    EXPECT_NEAR(angledMatrix[0].To<double>(),0.5,1e-7);

    const auto cutAngle=[](const ObjectMap& plan) {
        const auto blanks=plan.at("manufacturingParts").To<VariantArray>();
        const auto request=blanks[0].To<ObjectMap>().at("request").To<ObjectMap>();
        const auto ends=request.at("ends").To<ObjectMap>();
        return ends.at("start").To<ObjectMap>().at("angle").To<double>();
    };
    EXPECT_DOUBLE_EQ(cutAngle(perpendicular),90.0);
    EXPECT_DOUBLE_EQ(cutAngle(angled),60.0);
}

TEST(AssemblyTemplateSDOTest, SegmentedBendReturnsTemplateOwnedTargetAndCutEnvelopeLength)
{
    Scene scene;
    const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("segmented-bend")},
        {"parameters",ObjectMap{{"angle",90.0},{"bendRadius",40.0}}},
        {"processDrafts",ObjectMap{}},
    });
    const auto manufacturing=plan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(manufacturing.size(),1u);
    const auto request=manufacturing.front().To<ObjectMap>().at("request").To<ObjectMap>();
    const auto length=request.at("length").To<double>();
    EXPECT_NEAR(length,608.058,0.01);
    const auto features=request.at("features").To<VariantArray>();
    ASSERT_EQ(features.size(),1u);
    EXPECT_EQ(features.front().To<ObjectMap>().at("toolRef").To<ObjectMap>().at("id").To<std::string>(),
              "segmented-bend");
    ASSERT_TRUE(plan.contains("formedPreviewMesh"));
    const auto mesh=plan.at("formedPreviewMesh").To<ObjectMap>();
    EXPECT_FALSE(mesh.at("positions").To<VariantArray>().empty());
    EXPECT_FALSE(mesh.at("indices").To<VariantArray>().empty());
    const auto metadata=mesh.at("metadata").To<ObjectMap>();
    EXPECT_EQ(metadata.at("previewKind").To<std::string>(),"target-shape");
    EXPECT_EQ(metadata.at("formingValidation").To<std::string>(),"not-performed");
}

TEST(AssemblyTemplateSDOTest, FlexibleSlitBendReturnsTargetAndCuttableContinuousBlank)
{
    Scene scene;
    const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("flexible-slit-bend-integrated")},
        {"parameters",ObjectMap{{"angle",90.0},{"bendRadius",40.0}}},
        {"processDrafts",ObjectMap{}},
    });
    const auto parameters=plan.at("parameters").To<ObjectMap>();
    EXPECT_EQ(parameters.at("angle").To<double>(),90.0);
    EXPECT_EQ(parameters.at("bendRadius").To<double>(),40.0);
    const auto manufacturing=plan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(manufacturing.size(),1u);
    const auto request=manufacturing.front().To<ObjectMap>().at("request").To<ObjectMap>();
    EXPECT_NEAR(request.at("length").To<double>(),520.0+40.0*std::acos(-1.0)/2.0,0.01);
    const auto features=request.at("features").To<VariantArray>();
    ASSERT_EQ(features.size(),1u);
    EXPECT_EQ(features.front().To<ObjectMap>().at("toolRef").To<ObjectMap>().at("id").To<std::string>(),
              "flexible-slit-bend");
    ASSERT_TRUE(plan.contains("formedPreviewMesh"));
    const auto mesh=plan.at("formedPreviewMesh").To<ObjectMap>();
    EXPECT_FALSE(mesh.at("positions").To<VariantArray>().empty());
    EXPECT_FALSE(mesh.at("indices").To<VariantArray>().empty());
    const auto metadata=mesh.at("metadata").To<ObjectMap>();
    EXPECT_EQ(metadata.at("previewKind").To<std::string>(),"target-shape");
    EXPECT_EQ(metadata.at("formingValidation").To<std::string>(),"calibration-required");
    const auto preview=previewAssemblyManufacturingPart(scene,request);
    EXPECT_FALSE(preview.contains("resultError"))
        <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
    EXPECT_TRUE(preview.at("previewComputed").To<bool>());
    EXPECT_TRUE(preview.contains("geometry"));
    if(preview.contains("geometry"))
        EXPECT_NE(preview.at("baseGeometry").To<ObjectMap>().at("url").To<std::string>(),
                  preview.at("geometry").To<ObjectMap>().at("url").To<std::string>());

    for(const std::string mode:{"straight","u"}) {
        SCOPED_TRACE(mode);
        const auto variant=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string("flexible-slit-bend-integrated")},
            {"parameters",ObjectMap{{"angle",90.0},{"bendRadius",40.0}}},
            {"processDrafts",ObjectMap{{"node-slot",ObjectMap{{"flexible-slit-bend",ObjectMap{
                {"slitMode",mode}}}}}}},
        });
        const auto variantMesh=variant.at("formedPreviewMesh").To<ObjectMap>();
        EXPECT_EQ(variantMesh.at("metadata").To<ObjectMap>().at("slitMode").To<std::string>(),mode);
        const auto variantPart=variant.at("manufacturingParts").To<VariantArray>().front().To<ObjectMap>();
        const auto variantPreview=previewAssemblyManufacturingPart(scene,variantPart.at("request").To<ObjectMap>());
        EXPECT_FALSE(variantPreview.contains("resultError"))
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(variantPreview);
        EXPECT_TRUE(variantPreview.at("previewComputed").To<bool>());
        if(variantPreview.contains("geometry"))
            EXPECT_NE(variantPreview.at("baseGeometry").To<ObjectMap>().at("url").To<std::string>(),
                      variantPreview.at("geometry").To<ObjectMap>().at("url").To<std::string>());
    }
}

TEST(AssemblyTemplateSDOTest, EmbeddedArcFoldReturnsTemplateTargetAndRealCut)
{
    Scene scene;
    const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("node-embedded-arc-integrated")},
        {"parameters",ObjectMap{{"angle",90.0}}},
        {"processDrafts",ObjectMap{{"node-slot",ObjectMap{{"embedded-arc-notch",ObjectMap{
            {"arcRadius",8.0},{"rightArc",false}}}}}}},
    });
    EXPECT_EQ(plan.at("parameters").To<ObjectMap>().at("angle").To<double>(),90.0);
    const auto manufacturing=plan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(manufacturing.size(),1u);
    const auto request=manufacturing.front().To<ObjectMap>().at("request").To<ObjectMap>();
    EXPECT_NEAR(request.at("length").To<double>(),594.0,0.01);
    const auto features=request.at("features").To<VariantArray>();
    ASSERT_EQ(features.size(),1u);
    const auto feature=features.front().To<ObjectMap>();
    EXPECT_EQ(feature.at("toolRef").To<ObjectMap>().at("id").To<std::string>(),"embedded-arc-notch");
    const auto toolParameters=feature.at("toolParameters").To<ObjectMap>();
    EXPECT_EQ(toolParameters.at("arcRadius").To<double>(),8.0);
    EXPECT_FALSE(toolParameters.at("rightArc").To<bool>());
    ASSERT_TRUE(plan.contains("formedPreviewMesh"));
    const auto mesh=plan.at("formedPreviewMesh").To<ObjectMap>();
    EXPECT_FALSE(mesh.at("positions").To<VariantArray>().empty());
    EXPECT_FALSE(mesh.at("indices").To<VariantArray>().empty());
    const auto metadata=mesh.at("metadata").To<ObjectMap>();
    EXPECT_EQ(metadata.at("previewKind").To<std::string>(),"target-shape");
    EXPECT_EQ(metadata.at("formingValidation").To<std::string>(),"not-performed");
    const auto preview=previewAssemblyManufacturingPart(scene,request);
    EXPECT_FALSE(preview.contains("resultError"))
        <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
    EXPECT_TRUE(preview.at("previewComputed").To<bool>());
    if(preview.contains("geometry"))
        EXPECT_NE(preview.at("baseGeometry").To<ObjectMap>().at("url").To<std::string>(),
                  preview.at("geometry").To<ObjectMap>().at("url").To<std::string>());
}

TEST(AssemblyTemplateSDOTest, MergedFamiliesProduceNativeManufacturingGeometry)
{
    Scene scene;
    const std::vector<std::pair<std::string,ObjectMap>> cases{
        {"insert-sleeve",{{"connectionMode",std::string("sleeve")}}},
        {"two-end-end-angle",{{"jointAngle",90.0}}},
        {"two-end-middle",{{"interfaceMode",std::string("singleInsert")}}},
        {"two-end-middle",{{"interfaceMode",std::string("throughInsert")},{"fitGap",10.0}}},
        {"two-end-middle",{{"interfaceMode",std::string("saddle")}}},
        {"mechanical-fastener",{{"fastenerType",std::string("adjustableBolt")},{"adjustment",18.0}}},
        {"weld-interface",{{"weldType",std::string("lap")}}},
        {"weld-interface",{{"weldType",std::string("plug")}}},
        {"weld-interface",{{"weldType",std::string("slot")}}},
        {"bend",{{"angle",75.0}}},
        {"segmented-bend",{{"angle",75.0},{"bendRadius",40.0}}},
        {"four-end-end-end-end",{}},
    };
    for(const auto& [templateId,parameters]:cases) {
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",templateId},{"parameters",parameters},{"processDrafts",ObjectMap{}},
        });
        SCOPED_TRACE(templateId);
        EXPECT_EQ(plan.at("templateId").To<std::string>(),templateId);
        const auto manufacturing=plan.at("manufacturingParts").To<VariantArray>();
        ASSERT_FALSE(manufacturing.empty());
        for(const auto& value:manufacturing) {
            const auto planned=value.To<ObjectMap>().at("request").To<ObjectMap>();
            const auto preview=previewAssemblyManufacturingPart(scene,planned);
            EXPECT_FALSE(preview.contains("resultError"))
                <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
            EXPECT_TRUE(preview.at("previewComputed").To<bool>());
            EXPECT_TRUE(preview.contains("baseGeometry"));
            EXPECT_TRUE(preview.contains("geometry"));
        }
    }
}

TEST(AssemblyTemplateSDOTest, NodeJointTemplatesBuildNativeManufacturingGeometry)
{
    Scene scene;
    struct TemplateCase {
        std::string id;
        ObjectMap parameters;
        std::size_t blankCount;
    };
    const std::vector<TemplateCase> cases{
        {"wrap-a-over-b",{},2},
        {"wrap-a-over-b",{{"maleFemale",true},{"pairCount",std::string("two")}},2},
        {"wrap-a-over-b",{{"maleFemale",true},{"pairCount",std::string("four")}},2},
        {"node-v-notch-integrated",{{"angle",75.0}},1},
        {"node-embedded-arc-integrated",{{"angle",75.0}},1},
        {"node-edge-arc-integrated",{{"angle",75.0}},1},
        {"flexible-slit-bend-integrated",{{"angle",75.0}},1},
    };
    for(const auto& item:cases) {
        SCOPED_TRACE(item.id);
        SCOPED_TRACE(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(item.parameters));
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",item.id},{"parameters",item.parameters},{"processDrafts",ObjectMap{}},
        });
        EXPECT_EQ(plan.at("templateId").To<std::string>(),item.id);
        const auto returned=plan.at("parameters").To<ObjectMap>();
        for(const auto& [key,value]:item.parameters) {
            ASSERT_TRUE(returned.contains(key));
            if(value.Is<bool>())EXPECT_EQ(returned.at(key).To<bool>(),value.To<bool>());
            else if(value.Is<std::string>())EXPECT_EQ(returned.at(key).To<std::string>(),value.To<std::string>());
            else EXPECT_DOUBLE_EQ(returned.at(key).To<double>(),value.To<double>());
        }
        const auto manufacturing=plan.at("manufacturingParts").To<VariantArray>();
        ASSERT_EQ(manufacturing.size(),item.blankCount);
        for(const auto& value:manufacturing) {
            const auto planned=value.To<ObjectMap>().at("request").To<ObjectMap>();
            const auto preview=previewAssemblyManufacturingPart(scene,planned);
            EXPECT_FALSE(preview.contains("resultError"))
                <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
            ASSERT_TRUE(preview.contains("previewComputed"));
            EXPECT_TRUE(preview.at("previewComputed").To<bool>());
            EXPECT_TRUE(preview.contains("baseGeometry"));
            EXPECT_TRUE(preview.contains("geometry"));
        }
    }
}

TEST(AssemblyTemplateSDOTest, TeeTabSlotTemplateBuildsRealTwoAndFourWallCuts)
{
    Scene scene;
    const auto number=[](const iCAX::Data::Variant& value) {
        if(value.Is<double>())return value.To<double>();
        if(value.Is<int>())return static_cast<double>(value.To<int>());
        if(value.Is<std::int64_t>())return static_cast<double>(value.To<std::int64_t>());
        if(value.Is<std::uint64_t>())return static_cast<double>(value.To<std::uint64_t>());
        throw std::runtime_error("T tab-slot parameter is not numeric");
    };
    for (const auto& [choice,count] : std::vector<std::pair<std::string,int>>{
        {"two",2},{"four",4}}) {
        SCOPED_TRACE(choice);
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string("end-side-tab-slot")},
            {"parameters",ObjectMap{{"pairCount",choice}}},
            {"processDrafts",ObjectMap{}},
        });
        EXPECT_EQ(plan.at("templateId").To<std::string>(),"end-side-tab-slot");
        const auto design=plan.at("designParts").To<VariantArray>();
        const auto manufacturing=plan.at("manufacturingParts").To<VariantArray>();
        ASSERT_EQ(design.size(),2u);
        ASSERT_EQ(manufacturing.size(),2u);
        const auto host=manufacturing[0].To<ObjectMap>().at("request").To<ObjectMap>();
        const auto branch=manufacturing[1].To<ObjectMap>().at("request").To<ObjectMap>();
        const auto hostFeatures=host.at("features").To<VariantArray>();
        ASSERT_EQ(hostFeatures.size(),1u);
        const auto slots=hostFeatures[0].To<ObjectMap>();
        EXPECT_EQ(slots.at("toolRef").To<ObjectMap>().at("id").To<std::string>(),"paired-side-slots");
        EXPECT_EQ(slots.at("reference").To<std::string>(),"center");
        EXPECT_DOUBLE_EQ(number(slots.at("station")),0.0);
        EXPECT_EQ(slots.at("face").To<std::string>(),"top");
        const auto slotParameters=slots.at("toolParameters").To<ObjectMap>();
        EXPECT_EQ(number(slotParameters.at("pairCount")),count);
        EXPECT_FALSE(slotParameters.at("allowEndOpening").To<bool>());
        const auto branchEnds=branch.at("ends").To<ObjectMap>();
        const auto tabs=branchEnds.at("end").To<ObjectMap>();
        EXPECT_EQ(tabs.at("toolRef").To<ObjectMap>().at("id").To<std::string>(),"paired-end-tabs");
        EXPECT_EQ(number(tabs.at("toolParameters").To<ObjectMap>().at("pairCount")),count);
        EXPECT_EQ(number(branch.at("length")),232.0);
        std::array<TopoDS_Shape,2> finished;
        for (std::size_t index=0;index<finished.size();++index) {
            const auto resourceKey="tee-tab-slot-"+choice+"-"+std::to_string(index);
            const auto preview=previewAssemblyManufacturingPart(scene,
                index==0 ? host : branch,resourceKey);
            EXPECT_FALSE(preview.contains("resultError"))
                <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
            ASSERT_TRUE(preview.contains("previewComputed"));
            EXPECT_TRUE(preview.at("previewComputed").To<bool>());
            EXPECT_TRUE(preview.contains("baseGeometry"));
            EXPECT_TRUE(preview.contains("geometry"));
            const auto data=scene.Resources().Get<BRepModel>(scene.Resources().MakeNamedResourceURL(
                "tube-designer/punch-preview/assembly/"+resourceKey));
            ASSERT_TRUE(data);
            const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*data);
            ASSERT_TRUE(rebuilt.bOK);
            ASSERT_FALSE(rebuilt.Shape.IsNull());
            ASSERT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
            const auto pose=design[index].To<ObjectMap>().at("matrix").To<VariantArray>();
            gp_Trsf transform;
            transform.SetValues(number(pose[0]),number(pose[1]),number(pose[2]),number(pose[3]),
                number(pose[4]),number(pose[5]),number(pose[6]),number(pose[7]),
                number(pose[8]),number(pose[9]),number(pose[10]),number(pose[11]));
            finished[index]=BRepBuilderAPI_Transform(rebuilt.Shape,transform,true).Shape();
        }
        BRepAlgoAPI_Common overlap(finished[0],finished[1]);
        ASSERT_TRUE(overlap.IsDone());
        GProp_GProps material;
        BRepGProp::VolumeProperties(overlap.Shape(),material);
        EXPECT_LE(material.Mass(),1.0e-4)
            <<"The T template example must assemble without intersecting material";
    }
}

TEST(AssemblyTemplateSDOTest, TeeProfileInsertCutsTopOpeningAndBranchBlank)
{
    Scene scene;
    const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("t-profile-insert")},
        {"parameters",ObjectMap{{"intersectionAngle",90.0},
            {"insertDepth",12.0},{"fitGap",0.2}}},
        {"processDrafts",ObjectMap{}},
    });
    const auto design=plan.at("designParts").To<VariantArray>();
    const auto manufacturing=plan.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(design.size(),2u);
    ASSERT_EQ(manufacturing.size(),2u);
    const auto branchDesign=design[1].To<ObjectMap>();
    const auto matrix=branchDesign.at("matrix").To<VariantArray>();
    EXPECT_DOUBLE_EQ(matrix[8].To<double>(),-1.0)
        <<"the upright branch must approach the host from +Z";
    const auto host=manufacturing[0].To<ObjectMap>().at("request").To<ObjectMap>();
    const auto branch=manufacturing[1].To<ObjectMap>().at("request").To<ObjectMap>();
    const auto openings=host.at("features").To<VariantArray>();
    ASSERT_EQ(openings.size(),1u);
    const auto opening=openings[0].To<ObjectMap>();
    EXPECT_EQ(opening.at("face").To<std::string>(),"top");
    EXPECT_EQ(opening.at("reference").To<std::string>(),"center");
    EXPECT_EQ(opening.at("direction").To<std::string>(),"positive");
    EXPECT_DOUBLE_EQ(opening.at("clearance").To<double>(),0.2);
    EXPECT_DOUBLE_EQ(branch.at("length").To<double>(),232.0);
    const auto branchEnd=branch.at("ends").To<ObjectMap>()
        .at("end").To<ObjectMap>();
    EXPECT_EQ(branchEnd.at("toolRef").To<ObjectMap>()
        .at("id").To<std::string>(),"end-miter");
    for(const auto& request:{host,branch}) {
        const auto preview=previewAssemblyManufacturingPart(scene,request);
        EXPECT_FALSE(preview.contains("resultError"))
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
        ASSERT_TRUE(preview.contains("previewComputed"));
        EXPECT_TRUE(preview.at("previewComputed").To<bool>());
        EXPECT_TRUE(preview.contains("baseGeometry"));
        EXPECT_TRUE(preview.contains("geometry"));
    }
}

TEST(AssemblyTemplateSDOTest, FinishedInterfacesMeetAndExplosionUsesBlankPlacements)
{
    Scene scene;
    const auto point=[](const ObjectMap& part,double localX) {
        const auto matrix=part.at("matrix").To<VariantArray>();
        return std::array<double,3>{
            matrix[0].To<double>()*localX+matrix[3].To<double>(),
            matrix[4].To<double>()*localX+matrix[7].To<double>(),
            matrix[8].To<double>()*localX+matrix[11].To<double>(),
        };
    };
    const auto expectSame=[](const auto& left,const auto& right) {
        for(std::size_t index=0;index<3;++index)EXPECT_NEAR(left[index],right[index],1e-7);
    };

    const auto angled=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("two-end-end-angle")},
        {"parameters",ObjectMap{{"jointAngle",90.0},{"fitGap",0.0},{"planeRotation",0.0}}},
        {"processDrafts",ObjectMap{}},
    });
    const auto angledDesign=angled.at("designParts").To<VariantArray>();
    ASSERT_EQ(angledDesign.size(),2u);
    // Mitered end faces meet across their full section.  Their uncut axial
    // datum centers are deliberately offset, so equating those centers would
    // accept an overlapping corner and reject a correctly fitted miter.
    const auto angledA=point(angledDesign[0].To<ObjectMap>(),260.0);
    const auto angledB=point(angledDesign[1].To<ObjectMap>(),0.0);
    EXPECT_GT(std::hypot(angledA[0]-angledB[0],angledA[2]-angledB[2]),1.0);
    const auto angledBlanks=angled.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(angledBlanks.size(),2u);
    const auto aEnds=angledBlanks[0].To<ObjectMap>().at("request").To<ObjectMap>().at("ends").To<ObjectMap>();
    const auto bEnds=angledBlanks[1].To<ObjectMap>().at("request").To<ObjectMap>().at("ends").To<ObjectMap>();
    EXPECT_EQ(aEnds.at("end").To<ObjectMap>().at("rotation").To<double>(),90.0);
    EXPECT_EQ(bEnds.at("start").To<ObjectMap>().at("rotation").To<double>(),90.0);

    const auto tabSlot=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("tab-slot-lock")},{"parameters",ObjectMap{}},{"processDrafts",ObjectMap{}},
    });
    const auto tabSlotDesign=tabSlot.at("designParts").To<VariantArray>();
    ASSERT_EQ(tabSlotDesign.size(),2u);
    expectSame(point(tabSlotDesign[0].To<ObjectMap>(),280.0),point(tabSlotDesign[1].To<ObjectMap>(),0.0));

    const auto endMiddle=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("two-end-middle")},
        {"parameters",ObjectMap{{"interfaceMode",std::string("saddle")},{"intersectionAngle",60.0},{"fitGap",0.0}}},
        {"processDrafts",ObjectMap{}},
    });
    const auto endMiddleDesign=endMiddle.at("designParts").To<VariantArray>();
    ASSERT_EQ(endMiddleDesign.size(),2u);
    expectSame(point(endMiddleDesign[0].To<ObjectMap>(),220.0),point(endMiddleDesign[1].To<ObjectMap>(),220.0));

    const auto tabSlotBlanks=tabSlot.at("manufacturingParts").To<VariantArray>();
    ASSERT_EQ(tabSlotBlanks.size(),2u);
    const auto explodedTab=point(tabSlotBlanks[0].To<ObjectMap>(),0.0);
    const auto explodedSlot=point(tabSlotBlanks[1].To<ObjectMap>(),0.0);
    EXPECT_LT(explodedTab[0],explodedSlot[0]);
    EXPECT_NEAR(explodedTab[1],explodedSlot[1],1e-7);
    EXPECT_NEAR(explodedTab[2],explodedSlot[2],1e-7);
}

TEST(AssemblyTemplateSDOTest, MiteredLEndJoinCutFacesContactWithoutSolidOverlap)
{
    const auto number=[](const iCAX::Data::Variant& value) -> double {
        if(value.Is<double>())return value.To<double>();
        if(value.Is<std::int64_t>())return static_cast<double>(value.To<std::int64_t>());
        if(value.Is<std::uint64_t>())return static_cast<double>(value.To<std::uint64_t>());
        if(value.Is<int>())return static_cast<double>(value.To<int>());
        return static_cast<double>(value.To<unsigned int>());
    };
    const auto worldShape=[&](Scene& scene,const ObjectMap& blank,const ObjectMap& design,
            const std::string& resourceKey) {
        const auto request=blank.at("request").To<ObjectMap>();
        const auto preview=previewAssemblyManufacturingPart(scene,request,resourceKey);
        EXPECT_FALSE(preview.contains("resultError"))
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
        EXPECT_TRUE(preview.at("previewComputed").To<bool>());
        const auto data=scene.Resources().Get<BRepModel>(
            scene.Resources().MakeNamedResourceURL("tube-designer/punch-preview/assembly/"+resourceKey));
        if(!data)throw std::runtime_error("Mitered assembly preview has no manufactured BRep");
        const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*data);
        if(!rebuilt.bOK||rebuilt.Shape.IsNull())
            throw std::runtime_error("Mitered assembly BRep cannot rebuild");
        EXPECT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
        GProp_GProps localProperties;
        BRepGProp::VolumeProperties(rebuilt.Shape,localProperties);
        EXPECT_GT(localProperties.Mass(),1.0);
        const auto matrix=design.at("matrix").To<VariantArray>();
        gp_Trsf transform;
        transform.SetValues(number(matrix[0]),number(matrix[1]),number(matrix[2]),number(matrix[3]),
            number(matrix[4]),number(matrix[5]),number(matrix[6]),number(matrix[7]),
            number(matrix[8]),number(matrix[9]),number(matrix[10]),number(matrix[11]));
        return BRepBuilderAPI_Transform(rebuilt.Shape,transform,true).Shape();
    };
    for(const auto& [angle,plane]:std::array<std::pair<double,double>,4>{{
            {0.0,0.0},{60.0,35.0},{90.0,0.0},{120.0,25.0}}}) {
      for(const double gap:std::array<double,2>{{0.0,2.0}}) {
        SCOPED_TRACE("jointAngle="+std::to_string(angle)+", fitGap="+
            std::to_string(gap)+", planeRotation="+std::to_string(plane));
        Scene scene;
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string("two-end-end-angle")},
            {"parameters",ObjectMap{{"jointAngle",angle},{"fitGap",gap},{"planeRotation",plane}}},
            {"processDrafts",ObjectMap{}},
        });
        const auto designs=plan.at("designParts").To<VariantArray>();
        const auto blanks=plan.at("manufacturingParts").To<VariantArray>();
        ASSERT_EQ(designs.size(),2u);
        ASSERT_EQ(blanks.size(),2u);
        const double axialRetreat=gap/(2.0*std::cos(angle*std::acos(-1.0)/360.0));
        const auto aEnds=blanks[0].To<ObjectMap>().at("request").To<ObjectMap>()
            .at("ends").To<ObjectMap>();
        const auto bEnds=blanks[1].To<ObjectMap>().at("request").To<ObjectMap>()
            .at("ends").To<ObjectMap>();
        EXPECT_NEAR(number(aEnds.at("end").To<ObjectMap>().at("trim")),axialRetreat,1e-7);
        EXPECT_NEAR(number(bEnds.at("start").To<ObjectMap>().at("trim")),axialRetreat,1e-7);
        const auto a=worldShape(scene,blanks[0].To<ObjectMap>(),designs[0].To<ObjectMap>(),"miter-l-a");
        const auto b=worldShape(scene,blanks[1].To<ObjectMap>(),designs[1].To<ObjectMap>(),"miter-l-b");
        ASSERT_FALSE(a.IsNull());
        ASSERT_FALSE(b.IsNull());
        BRepAlgoAPI_Common common(a,b);
        ASSERT_TRUE(common.IsDone());
        GProp_GProps overlap;
        BRepGProp::VolumeProperties(common.Shape(),overlap);
        EXPECT_NEAR(overlap.Mass(),0.0,0.1)
            <<"The two finished cut tubes must not interpenetrate";
        BRepExtrema_DistShapeShape contact(a,b);
        contact.Perform();
        ASSERT_TRUE(contact.IsDone());
        EXPECT_NEAR(contact.Value(),gap,0.05)
            <<"The manufactured cut ends must have the requested physical face gap";
        // OpenCascade's solid/solid Common omits a shared face when the
        // intersection has no volume.  Probe four separated points on the
        // actual cut wall instead: both side walls and both top/bottom walls
        // must be ON each finished BRep, ruling out a single-tip contact.
        const auto aRequest=blanks[0].To<ObjectMap>().at("request").To<ObjectMap>();
        const auto sectionParameters=aRequest.at("parameters").To<ObjectMap>();
        const double width=number(sectionParameters.at("width"));
        const double depth=number(sectionParameters.at("depth"));
        const double wall=number(sectionParameters.at("wallThickness"));
        const double radius=number(sectionParameters.at("cornerRadius"));
        const double phi=plane*std::acos(-1.0)/180.0;
        const double halfSpan=(width/2-radius)*std::abs(std::sin(phi))
            +(depth/2-radius)*std::abs(std::cos(phi))+radius;
        const double slope=std::tan(angle*std::acos(-1.0)/360.0);
        const double length=number(aRequest.at("length"));
        const auto aMatrix=designs[0].To<ObjectMap>().at("matrix").To<VariantArray>();
        const auto seamPoint=[&](double localY,double localZ) {
            const double transverse=-std::sin(phi)*localY+std::cos(phi)*localZ;
            const double localX=length-halfSpan*slope-slope*transverse;
            return gp_Pnt(
                number(aMatrix[0])*localX+number(aMatrix[1])*localY+number(aMatrix[2])*localZ+number(aMatrix[3]),
                number(aMatrix[4])*localX+number(aMatrix[5])*localY+number(aMatrix[6])*localZ+number(aMatrix[7]),
                number(aMatrix[8])*localX+number(aMatrix[9])*localY+number(aMatrix[10])*localZ+number(aMatrix[11]));
        };
        if(gap>0.0)continue;
        for(const auto& [localY,localZ]:std::array<std::pair<double,double>,4>{{
                {width/2-wall/2,0.0},{-width/2+wall/2,0.0},
                {0.0,depth/2-wall/2},{0.0,-depth/2+wall/2}}}) {
            const auto point=seamPoint(localY,localZ);
            SCOPED_TRACE("seam wall sample y="+std::to_string(localY)+
                ", z="+std::to_string(localZ));
            EXPECT_EQ(BRepClass3d_SolidClassifier(a,point,0.05).State(),TopAbs_ON);
            EXPECT_EQ(BRepClass3d_SolidClassifier(b,point,0.05).State(),TopAbs_ON);
        }
      }
    }
}

TEST(AssemblyTemplateSDOTest, WrapConnectionFinishedPreviewUsesContactingNativeParts)
{
    struct Bounds { std::array<double,3> low{}, high{}; };
    struct NativePart { Bounds bounds; std::string geometryUrl; std::string baseUrl;
        TopoDS_Shape finished, base; };
    const auto nativeBounds=[](Scene& scene,const ObjectMap& planned,const std::string& key) {
        const auto preview=previewAssemblyManufacturingPart(scene,planned,key);
        if(preview.contains("resultError"))throw std::runtime_error(preview.at("resultError").To<std::string>());
        if(!preview.at("previewComputed").To<bool>())throw std::runtime_error("Wrap manufacturing preview did not compute");
        const auto model=scene.Resources().Get<BRepModel>(
            scene.Resources().MakeNamedResourceURL("tube-designer/punch-preview/assembly/"+key));
        const auto baseModel=scene.Resources().Get<BRepModel>(
            scene.Resources().MakeNamedResourceURL("tube-designer/punch-preview/assembly/"+key+"/base"));
        if(!model)throw std::runtime_error("Wrap manufacturing BRep is missing");
        if(!baseModel)throw std::runtime_error("Wrap base BRep is missing");
        const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*model);
        const auto rebuiltBase=iCAX::OpenCascade::BuildOpenCascadeShape(*baseModel);
        if(!rebuilt.bOK||rebuilt.Shape.IsNull())throw std::runtime_error("Wrap manufacturing BRep cannot rebuild");
        if(!rebuiltBase.bOK||rebuiltBase.Shape.IsNull())throw std::runtime_error("Wrap base BRep cannot rebuild");
        Bnd_Box box;
        BRepBndLib::AddOptimal(rebuilt.Shape,box,false,false);
        box.SetGap(0);
        double x0,y0,z0,x1,y1,z1;
        box.Get(x0,y0,z0,x1,y1,z1);
        return NativePart{Bounds{{x0,y0,z0},{x1,y1,z1}},
            preview.at("geometry").To<ObjectMap>().at("url").To<std::string>(),
            preview.at("baseGeometry").To<ObjectMap>().at("url").To<std::string>(),
            rebuilt.Shape,rebuiltBase.Shape};
    };
    const auto designPose=[](const ObjectMap& design) {
        const auto values=design.at("matrix").To<VariantArray>();
        gp_Trsf pose;
        pose.SetValues(values[0].To<double>(),values[1].To<double>(),values[2].To<double>(),values[3].To<double>(),
            values[4].To<double>(),values[5].To<double>(),values[6].To<double>(),values[7].To<double>(),
            values[8].To<double>(),values[9].To<double>(),values[10].To<double>(),values[11].To<double>());
        return pose;
    };
    const auto worldShape=[&](const TopoDS_Shape& shape,const ObjectMap& design) {
        return BRepBuilderAPI_Transform(shape,designPose(design),true).Shape();
    };
    const auto solidVolume=[](const TopoDS_Shape& shape) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(shape,properties);
        return properties.Mass();
    };
    const auto worldBounds=[](const Bounds& local,const ObjectMap& design) {
        const auto values=design.at("matrix").To<VariantArray>();
        std::array<double,16> matrix{};
        for(std::size_t index=0;index<matrix.size();++index)matrix[index]=values[index].To<double>();
        Bounds world{{1e100,1e100,1e100},{-1e100,-1e100,-1e100}};
        for(int x=0;x<2;++x)for(int y=0;y<2;++y)for(int z=0;z<2;++z) {
            const std::array<double,3> point{x?local.high[0]:local.low[0],
                y?local.high[1]:local.low[1],z?local.high[2]:local.low[2]};
            for(std::size_t axis=0;axis<3;++axis) {
                const auto value=matrix[axis*4]*point[0]+matrix[axis*4+1]*point[1]
                    +matrix[axis*4+2]*point[2]+matrix[axis*4+3];
                world.low[axis]=std::min(world.low[axis],value);
                world.high[axis]=std::max(world.high[axis],value);
            }
        }
        return world;
    };
    for(const auto& id:{"wrap-a-over-b"}) {
        const std::string longRole="memberA";
        const std::string shortRole="memberB";
        for(const auto& [male,pairs]:std::vector<std::pair<bool,std::string>>{
                {false,""},{true,""},{true,"two"},{true,"four"}}) {
            SCOPED_TRACE(id);
            SCOPED_TRACE(male?(pairs.empty()?"default-two":pairs):"plain");
            Scene scene;
            ObjectMap parameters{{"maleFemale",male}};
            if(!pairs.empty())parameters["pairCount"]=pairs;
            const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
                {"templateId",std::string(id)},
                {"parameters",parameters},
                {"processDrafts",ObjectMap{}},
            });
            const auto resolvedParameters=plan.at("parameters").To<ObjectMap>();
            std::set<std::string> returnedKeys;
            for(const auto& entry:resolvedParameters)returnedKeys.insert(entry.first);
            EXPECT_EQ(returnedKeys,(std::set<std::string>{"maleFemale","pairCount",
                "tabWidth","tabLength","sideClearance"}))
                << "Derived geometry values must not leak into normalized template parameters";
            EXPECT_FALSE(resolvedParameters.contains("setback"));
            EXPECT_FALSE(resolvedParameters.contains("memberA_depth"));
            EXPECT_FALSE(resolvedParameters.contains("memberB_depth"));
            EXPECT_EQ(resolvedParameters.at("maleFemale").To<bool>(),male);
            if(!pairs.empty())
                EXPECT_EQ(resolvedParameters.at("pairCount").To<std::string>(),pairs);
            std::map<std::string,ObjectMap> designs,blanks;
            for(const auto& value:plan.at("designParts").To<VariantArray>()) {
                const auto part=value.To<ObjectMap>();
                designs.emplace(part.at("role").To<std::string>(),part);
            }
            for(const auto& value:plan.at("manufacturingParts").To<VariantArray>()) {
                const auto part=value.To<ObjectMap>();
                blanks.emplace(part.at("sourceRole").To<std::string>(),part);
            }
            ASSERT_EQ(designs.size(),2u);
            ASSERT_EQ(blanks.size(),2u);
            ASSERT_TRUE(designs.contains(longRole));
            ASSERT_TRUE(designs.contains(shortRole));
            ASSERT_TRUE(blanks.contains(longRole));
            ASSERT_TRUE(blanks.contains(shortRole));
            const auto longRequest=blanks.at(longRole).at("request").To<ObjectMap>();
            const auto shortRequest=blanks.at(shortRole).at("request").To<ObjectMap>();
            const auto longNative=nativeBounds(scene,longRequest,"long-part");
            const auto shortNative=nativeBounds(scene,shortRequest,"short-part");
            EXPECT_NE(longNative.geometryUrl,shortNative.geometryUrl);
            EXPECT_NE(longNative.baseUrl,shortNative.baseUrl);
            const auto& longLocal=longNative.bounds;
            const auto& shortLocal=shortNative.bounds;
            const auto longer=worldBounds(longLocal,designs.at(longRole));
            const auto shorter=worldBounds(shortLocal,designs.at(shortRole));
            // These are the BRep solids shown by assemblyFinishedRows, not logical
            // uncut members or a purely declarative previewScene.
            EXPECT_GT(longLocal.high[0]-longLocal.low[0],
                1.5*(shortLocal.high[0]-shortLocal.low[0]));
            EXPECT_GT(longer.high[0]-longer.low[0],300.0);
            EXPECT_LT(longer.high[2]-longer.low[2],60.0);
            EXPECT_NEAR(shorter.high[2]-shorter.low[2],180.0,0.5)
                << "The short tube stock must not shrink by a virtual setback";
            EXPECT_LT(shorter.high[0]-shorter.low[0],60.0);
            EXPECT_GT(std::min(longer.high[0],shorter.high[0])
                -std::max(longer.low[0],shorter.low[0]),30.0);
            EXPECT_GT(std::min(longer.high[1],shorter.high[1])
                -std::max(longer.low[1],shorter.low[1]),30.0);
            EXPECT_NEAR(longer.high[0],shorter.high[0],0.5)
                << "An L corner must not leave a horizontal tail past the upright";
            EXPECT_GT(shorter.low[0]-longer.low[0],300.0)
                << "The upright must meet the terminal region of the horizontal member";
            const auto sideGap=shorter.low[2]-longer.high[2];
            if(male) {
                const auto expectedPairs=pairs.empty()?"two":pairs;
                const bool four=expectedPairs=="four";
                EXPECT_EQ(plan.at("parameters").To<ObjectMap>().at("pairCount").To<std::string>(),expectedPairs);
                EXPECT_LE(sideGap,0.5);
                EXPECT_GE(sideGap,-13.0); // nominal 12 mm tab insertion
                EXPECT_GT(solidVolume(longNative.base)-solidVolume(longNative.finished),1.0)
                    << "The long tube must contain actual cut side sockets";
                EXPECT_GT(solidVolume(shortNative.base)-solidVolume(shortNative.finished),1.0)
                    << "The short tube must retain rounded ears after end cutting";
                const auto features=longRequest.at("features").To<VariantArray>();
                ASSERT_EQ(features.size(),1u);
                const auto socket=features.front().To<ObjectMap>();
                ASSERT_EQ(socket.at("reference").To<std::string>(),"start");
                const auto number=[](const auto& value) -> double {
                    if(value.template Is<double>())return value.template To<double>();
                    if(value.template Is<float>())return value.template To<float>();
                    if(value.template Is<std::int64_t>())return static_cast<double>(value.template To<std::int64_t>());
                    if(value.template Is<std::uint64_t>())return static_cast<double>(value.template To<std::uint64_t>());
                    if(value.template Is<int>())return static_cast<double>(value.template To<int>());
                    return static_cast<double>(value.template To<unsigned int>());
                };
                const double station=longLocal.low[0]+number(socket.at("station"));
                const double faceZ=longLocal.high[2]-1.0; // 2 mm receiving wall
                const double across=(longLocal.low[1]+longLocal.high[1])/2;
                const double shortY=(shortLocal.low[1]+shortLocal.high[1])/2;
                const double shortZ=(shortLocal.low[2]+shortLocal.high[2])/2;
                const double earTipX=shortLocal.high[0]-3.0;
                const double tabLength=number(plan.at("parameters").To<ObjectMap>().at("tabLength"));
                const double earAtHostWallX=shortLocal.high[0]-tabLength+1.0;
                const auto classified=[](const TopoDS_Shape& shape,const gp_Pnt& p) {
                    return BRepClass3d_SolidClassifier(shape,p,1e-6).State();
                };
                const auto expectState=[&](const TopoDS_Shape& shape,const gp_Pnt& p,
                        TopAbs_State expected,const char* feature) {
                    SCOPED_TRACE(std::string(feature)+" ("+std::to_string(p.X())+","+
                        std::to_string(p.Y())+","+std::to_string(p.Z())+")");
                    EXPECT_EQ(classified(shape,p),expected);
                };
                std::vector<gp_Pnt> matingEars;
                // The default pair occupies opposite short-tube walls, one
                // ear per wall.  Four adds one ear to each remaining wall.
                for(double side:{-1.,1.}) {
                    const double z=shortZ+side*19.0;
                    const gp_Pnt tip(earTipX,shortY,z);
                    expectState(shortNative.base,tip,TopAbs_IN,"opposite wall before cutting");
                    expectState(shortNative.finished,tip,TopAbs_IN,"opposite wall ear");
                    matingEars.emplace_back(earAtHostWallX,shortY,z);
                    for(double shifted:{-9.,9.})
                        expectState(shortNative.finished,gp_Pnt(earTipX,shortY+shifted,z),
                            TopAbs_OUT,"no second ear on the same wall");
                }
                for(double side:{-1.,1.}) {
                    const double y=shortY+side*19.0;
                    const gp_Pnt tip(earTipX,y,shortZ);
                    expectState(shortNative.base,tip,TopAbs_IN,"other wall before cutting");
                    expectState(shortNative.finished,tip,
                        four?TopAbs_IN:TopAbs_OUT,
                        "remaining wall ear only in four mode");
                    if(four)
                        matingEars.emplace_back(earAtHostWallX,y,shortZ);
                }
                // The two receiving slots sit on opposite parallel edges of
                // the long tube face.  Four adds its other opposite edge pair.
                for(double side:{-1.,1.}) {
                    const gp_Pnt slotPoint(station,across+side*19.0,faceZ);
                    expectState(longNative.base,slotPoint,TopAbs_IN,"opposite edge before slotting");
                    expectState(longNative.finished,slotPoint,TopAbs_OUT,"opposite edge slot");
                    for(double shifted:{-9.,9.})
                        expectState(longNative.finished,
                            gp_Pnt(station+shifted,across+side*19.0,faceZ),TopAbs_IN,
                            "no second slot along the same edge");
                }
                for(double side:{-1.,1.}) {
                    const gp_Pnt slotPoint(station+side*19.0,across,faceZ);
                    expectState(longNative.base,slotPoint,TopAbs_IN,"other edge before slotting");
                    expectState(longNative.finished,slotPoint,
                        four?TopAbs_OUT:TopAbs_IN,
                        "remaining edge slot only in four mode");
                }
                expectState(longNative.finished,gp_Pnt(station,across,faceZ),TopAbs_IN,
                    "receiving face center between slots");
                const auto shortPose=designPose(designs.at(shortRole));
                const auto longInverse=designPose(designs.at(longRole)).Inverted();
                for(auto ear:matingEars) {
                    expectState(shortNative.finished,ear,TopAbs_IN,"retained ear at host wall");
                    ear.Transform(shortPose);
                    ear.Transform(longInverse);
                    expectState(longNative.base,ear,TopAbs_IN,"ear crosses uncut host wall");
                    expectState(longNative.finished,ear,TopAbs_OUT,"ear lies in its real socket");
                }
                const auto longFinished=worldShape(longNative.finished,designs.at(longRole));
                const auto shortFinished=worldShape(shortNative.finished,designs.at(shortRole));
                BRepAlgoAPI_Common overlap(longFinished,shortFinished);
                ASSERT_TRUE(overlap.IsDone());
                EXPECT_NEAR(solidVolume(overlap.Shape()),0.0,0.1)
                    << "The finished tabs must enter the sockets without colliding with the host";
            } else EXPECT_NEAR(sideGap,0.0,0.5);
            EXPECT_GT(shorter.high[2]-longer.high[2],90.0);
        }
    }
}
std::shared_ptr<CManufacturingPartComponent> part(Scene& scene,const std::string& id) {
    const auto entity=scene.Database().GetEntity(iCAX::Data::uuid::from_string(id).value());
    if(!entity)throw std::runtime_error("Saved part entity missing");
    const auto p=std::dynamic_pointer_cast<CManufacturingPartComponent>(entity->GetComponent(CManufacturingPartComponent::S_ClassName));
    if(!p)throw std::runtime_error("Saved manufacturing component missing");return p;
}
ObjectMap recipe(const std::shared_ptr<CManufacturingPartComponent>& p) {
    const auto component=std::dynamic_pointer_cast<CPunchWizardComponent>(
        p->GetEntity()->GetComponent(CPunchWizardComponent::S_ClassName));
    if(!component)throw std::runtime_error("Saved punch wizard component missing");
    return component->GetDefinition();
}
TopoDS_Shape shape(Scene& scene,const std::shared_ptr<CManufacturingPartComponent>& p) {
    const auto data=scene.Resources().Get<BRepModel>(p->GetManufacturingGeometryResourceID(),p->GetManufacturingGeometryResourceVersion());
    if(!data)throw std::runtime_error("Saved final BRep missing");
    const auto result=iCAX::OpenCascade::BuildOpenCascadeShape(*data);
    if(!result.bOK||result.Shape.IsNull())throw std::runtime_error("Saved final BRep cannot rebuild");return result.Shape;
}
double volume(const TopoDS_Shape& shape) { GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);return properties.Mass(); }
std::vector<uint8_t> persistedShapeBytes(Scene& scene,const std::shared_ptr<CManufacturingPartComponent>& p) {
    return iCAX::GeometryData::Persistence::Serialize(*scene.Resources().Get<BRepModel>(p->GetManufacturingGeometryResourceID(),p->GetManufacturingGeometryResourceVersion()));
}
void checkSameShape(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    ASSERT_TRUE(BRepCheck_Analyzer(b).IsValid());int solids=0;
    for(TopExp_Explorer e(b,TopAbs_SOLID);e.More();e.Next())++solids;EXPECT_EQ(solids,1);
    const auto expected=volume(a);EXPECT_GT(expected,0);EXPECT_NEAR(expected,volume(b),std::max(1.,expected)*1e-7);
    BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> args,tools;args.Append(a);tools.Append(b);
    common.SetArguments(args);common.SetTools(tools);common.SetNonDestructive(true);common.SetFuzzyValue(1e-6);common.Build();
    const auto intersection=volume(common.Shape());
    if(std::abs(expected-intersection)>std::max(1.,expected)*1e-6) {
        const auto self=volume(BRepAlgoAPI_Common(a,a).Shape());
        std::cout<<"[same-shape-kernel-diagnostic] volume="<<expected<<" common="<<intersection<<" selfCommon="<<self<<std::endl;
        // A failed self-intersection cannot be used as an equality oracle. Persistence
        // is independently checked byte-for-byte below; re-evaluation also checks
        // center of mass, surface area, and bounds plus scenario-specific hole points.
        // This diagnostic does not decide persistence correctness: independently
        // rebuilt coincident OCCT faces can defeat Common despite identical bytes.
    }
    GProp_GProps va,vb,sa,sb;BRepGProp::VolumeProperties(a,va);BRepGProp::VolumeProperties(b,vb);
    BRepGProp::SurfaceProperties(a,sa);BRepGProp::SurfaceProperties(b,sb);
    EXPECT_NEAR(sa.Mass(),sb.Mass(),std::max(1.,sa.Mass())*1e-7);
    EXPECT_NEAR(va.CentreOfMass().Distance(vb.CentreOfMass()),0,1e-5);
    Bnd_Box ba,bb;BRepBndLib::AddOptimal(a,ba,false,false);BRepBndLib::AddOptimal(b,bb,false,false);
    double aa[6],ab[6];ba.Get(aa[0],aa[1],aa[2],aa[3],aa[4],aa[5]);bb.Get(ab[0],ab[1],ab[2],ab[3],ab[4],ab[5]);
    for(int i=0;i<6;++i)EXPECT_NEAR(aa[i],ab[i],1e-5);
}
ObjectMap editPayload(Scene& scene,const std::string& id,const ObjectMap& config) {
    const auto p=part(scene,id);
    return {{"partEntityId",id},{"resourceId",p->GetManufacturingGeometryResourceID()},
        {"resourceVersion",p->GetManufacturingGeometryResourceVersion()},{"drawing",config.at("drawing")},
        {"features",config.at("features")},{"ends",config.at("ends")}};
}

TEST(ImportedPartDrawingRecoverySDO, PlainTubeGetsReplayableDrawingWithoutChangingOriginal)
{
    Scene scene;
    const auto imported=invoke(scene,"ImportNestingPart",{{"sourcePath",(std::filesystem::current_path()/
        "samples/tube-one/01_round_tube_plain.step").string()}});
    const auto id=imported.at("partEntityId").To<std::string>();
    const auto original=part(scene,id);
    const auto originalShape=shape(scene,original);
    const auto originalBytes=persistedShapeBytes(scene,original);
    const auto version=original->GetManufacturingGeometryResourceVersion();
    EXPECT_THROW(invoke(scene,"RecoverImportedPartDrawing",{{"partEntityId",id},
        {"resourceVersion",version+1}}),std::exception);
    EXPECT_THROW(invoke(scene,"GetPartDrawing",{{"partEntityId",id}}),std::exception);
    EXPECT_FALSE(original->GetEntity()->GetComponent(CPartDrawingComponent::S_ClassName));

    const auto recovered=invoke(scene,"RecoverImportedPartDrawing",{{"partEntityId",id},
        {"resourceVersion",version}});
    EXPECT_FALSE(recovered.contains("tubeDesigner"));
    EXPECT_TRUE(recovered.at("ready").To<bool>());
    EXPECT_EQ(recovered.at("partEntityId").To<std::string>(),id);
    const auto saved=part(scene,id);
    EXPECT_EQ(originalBytes,persistedShapeBytes(scene,saved));
    const auto component=std::dynamic_pointer_cast<CPartDrawingComponent>(
        saved->GetEntity()->GetComponent(CPartDrawingComponent::S_ClassName));
    ASSERT_TRUE(component);
    const auto config=component->GetDefinition();
    const auto fetched=invoke(scene,"GetPartDrawing",{{"partEntityId",id},
        {"resourceVersion",version}});
    EXPECT_THROW(invoke(scene,"GetPartDrawing",{{"partEntityId",id},
        {"resourceVersion",version+1}}),std::exception);
    EXPECT_FALSE(fetched.contains("tubeDesigner"));
    EXPECT_EQ(fetched.at("definition").To<ObjectMap>(),config);
    EXPECT_TRUE(config.at("features").To<VariantArray>().empty());
    EXPECT_NE(config.at("baseResourceId").To<std::string>(),
        saved->GetManufacturingGeometryResourceID());
    EXPECT_EQ(config.at("drawing").To<ObjectMap>().at("section").To<ObjectMap>()
        .at("profile").To<ObjectMap>().at("schema").To<std::string>(),
        "icax.imported-tube-profile");

    // The normal editor Apply path must accept the recovered base and section.
    EXPECT_NO_THROW(invoke(scene,"ApplyPartDrawing",editPayload(scene,id,config)));
    checkSameShape(originalShape,shape(scene,part(scene,id)));

    // Changing length rebuilds the blank but keeps the imported part's start
    // datum; it must not jump from its original -L/2 frame to X=0.
    auto resized=editPayload(scene,id,config);
    auto drawing=resized.at("drawing").To<ObjectMap>();
    drawing["length"]=drawing.at("length").To<double>()+10.0;
    resized["drawing"]=drawing;
    try { invoke(scene,"ApplyPartDrawing",resized); }
    catch(const std::exception& error) { FAIL()<<error.what(); }
    const auto changed=part(scene,id);
    const auto changedComponent=std::dynamic_pointer_cast<CPartDrawingComponent>(
        changed->GetEntity()->GetComponent(CPartDrawingComponent::S_ClassName));
    ASSERT_TRUE(changedComponent);
    EXPECT_TRUE(changedComponent->GetDefinition().contains("blankOrigin"));
    Bnd_Box beforeBox,afterBox;
    BRepBndLib::AddOptimal(originalShape,beforeBox,false,false);
    BRepBndLib::AddOptimal(shape(scene,changed),afterBox,false,false);
    double x0,y0,z0,x1,y1,z1, ax0,ay0,az0,ax1,ay1,az1;
    beforeBox.Get(x0,y0,z0,x1,y1,z1);
    afterBox.Get(ax0,ay0,az0,ax1,ay1,az1);
    EXPECT_NEAR(ax0,x0,1e-6);
    EXPECT_NEAR(ax1,x1+10.0,1e-6);
}
void assertFrozenRecords(const ObjectMap& config) {
    for(const auto& f:config.at("features").To<VariantArray>()) {
        const auto record=f.To<ObjectMap>();EXPECT_FALSE(record.contains("toolSnapshot"));
        EXPECT_TRUE(record.contains("frozenTool"));EXPECT_TRUE(record.contains("frozenCut"));
    }
    for(const auto& [key,value]:config.at("ends").To<ObjectMap>()) {
        const auto record=value.To<ObjectMap>();if(record.at("type").To<std::string>()=="keep")continue;
        EXPECT_FALSE(record.contains("toolSnapshot"));EXPECT_TRUE(record.contains("frozenTool"));EXPECT_TRUE(record.contains("frozenCut"));
    }
}
void acceptance(const std::string& name,const ObjectMap& create,const std::function<void(ObjectMap&)>& change,
    const std::function<void(Scene&,const std::string&)>& verify={}) {
    const auto dir=std::filesystem::current_path()/"tmp/punch-persistence-acceptance";std::filesystem::create_directories(dir);
    Scene scene;const auto added=invoke(scene,"AddNestingPunchPart",create);const auto id=added.at("partEntityId").To<std::string>();
    const auto initial=part(scene,id);EXPECT_EQ(initial->GetQuantity(),2ull);
    EXPECT_FALSE(initial->GetItemProperties().contains("tubeDesigner.punchWizard"));
    ASSERT_TRUE(std::dynamic_pointer_cast<CPunchWizardComponent>(
        initial->GetEntity()->GetComponent(CPunchWizardComponent::S_ClassName)));
    const auto initialRecipe=recipe(initial);const auto initialShape=shape(scene,initial);const auto initialBytes=persistedShapeBytes(scene,initial);assertFrozenRecords(initialRecipe);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName=name;
    const auto path=dir/(name+".ictd");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());
    EXPECT_EQ(initialRecipe,recipe(part(reopened,id)));EXPECT_EQ(initialBytes,persistedShapeBytes(reopened,part(reopened,id)));checkSameShape(initialShape,shape(reopened,part(reopened,id)));
    auto changed=initialRecipe;change(changed);
    invoke(reopened,"ApplyPunchWizard",editPayload(reopened,id,changed));
    const auto edited=part(reopened,id);const auto editedRecipe=recipe(edited);const auto editedShape=shape(reopened,edited);const auto editedBytes=persistedShapeBytes(reopened,edited);
    assertFrozenRecords(editedRecipe);EXPECT_EQ(editedRecipe.at("drawing"),changed.at("drawing"));
    EXPECT_EQ(editedRecipe.at("features").To<VariantArray>().size(),changed.at("features").To<VariantArray>().size());
    if(verify)verify(reopened,id);
    file.Save(path,info,reopened.Database(),reopened.Resources());
    Scene finalScene(scene.project,scene.GetSceneID(),false);file.Open(path,finalScene.Database(),finalScene.Resources());
    EXPECT_EQ(editedRecipe,recipe(part(finalScene,id)));EXPECT_EQ(editedBytes,persistedShapeBytes(finalScene,part(finalScene,id)));checkSameShape(editedShape,shape(finalScene,part(finalScene,id)));
    // Re-evaluate the persisted records through the same public edit SDO after a second open.
    invoke(finalScene,"ApplyPunchWizard",editPayload(finalScene,id,recipe(part(finalScene,id))));
    checkSameShape(editedShape,shape(finalScene,part(finalScene,id)));if(verify)verify(finalScene,id);
    file.Save(path,info,finalScene.Database(),finalScene.Resources());
    std::ofstream evidence(dir/(name+".json"));evidence<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
        {"name",name},{"projectFile",path.string()},{"partEntityId",id},{"createdRecipe",initialRecipe},{"editedRecipe",editedRecipe},
        {"createdVolume",volume(initialShape)},{"editedVolume",volume(editedShape)},{"reopenedVolume",volume(shape(finalScene,part(finalScene,id)))}});
    std::cout<<"[persistence-acceptance] "<<name<<" created="<<volume(initialShape)<<" edited="<<volume(editedShape)<<" file="<<path.string()<<std::endl;
}

TEST(PunchPersistenceSDO, ZeroHoleRoundTubeCreatesEditsAndReopens) {
    acceptance("01-round-straight",request("验收-零孔圆管","round"),[](ObjectMap& config){auto d=config.at("drawing").To<ObjectMap>();d["length"]=1200.;config["drawing"]=d;},
        [](Scene& scene,const std::string& id){EXPECT_TRUE(recipe(part(scene,id)).at("features").To<VariantArray>().empty());EXPECT_NEAR(part(scene,id)->GetLength(),1200,1e-4);});
}
TEST(PunchPersistenceSDO, RoundEndOnlyMaleAndZReopensAndRemainsEditable) {
    acceptance("02-round-end-only",request("验收-圆管仅端切","round",{{"start",end("end-key-joint",{{"gender",std::string("male")}})},
        {"end",end("end-step-z",{{"depth",20.}},5)}}),[](ObjectMap& config){auto ends=config.at("ends").To<ObjectMap>();
        auto e=ends.at("end").To<ObjectMap>();e["toolParameters"]=ObjectMap{{"depth",30.},{"hand",std::string("negative")}};ends["end"]=e;config["ends"]=ends;});
}
TEST(PunchPersistenceSDO, RoundedRectBranchAndDxfEndOnlyReopens) {
    const auto branch=systemProfile("round",{{"width",20.},{"wallThickness",2.}});
    acceptance("03-r2-end-profiles",request("验收-R2支管与DXF端切","rect",{{"start",end("end-profile",{},10,section(branch))},
        {"end",end("end-profile",{},5,section(dxfProfile(),"dxf"))}}),[](ObjectMap& config){auto ends=config.at("ends").To<ObjectMap>();
        auto e=ends.at("start").To<ObjectMap>();e["trim"]=15.;ends["start"]=e;config["ends"]=ends;});
}
void crossing(const std::string& profile) {
    ObjectMap feature{{"id",std::string("cross-end")},{"type",std::string("circle")},{"diameter",10.},{"station",992.},
        {"face",std::string(profile=="round"?"round":"top")},{"reference",std::string("start")},{"layoutDatum",std::string("base")},
        {"distributionMode",std::string("pitch")},{"arrayCount",3ull},{"arrayPitch",10.},{"arrayOffsets",VariantArray{0.,10.,20.}}};
    acceptance("04-"+profile+"-cross-end",request("验收-跨端阵列",profile,{},VariantArray{feature}),[](ObjectMap& config){auto features=config.at("features").To<VariantArray>();
        auto f=features[0].To<ObjectMap>();f["station"]=982.;f["arrayCount"]=4ull;f["arrayOffsets"]=VariantArray{0.,10.,20.,30.};features[0]=f;config["features"]=features;},
        [profile](Scene& scene,const std::string& id){const auto f=recipe(part(scene,id)).at("features").To<VariantArray>()[0].To<ObjectMap>();
            EXPECT_EQ(f.at("arrayOffsets").To<VariantArray>().size(),4u);EXPECT_EQ(f.at("distributionMode").To<std::string>(),"pitch");
            EXPECT_EQ(f.at("frozenCut").To<ObjectMap>().at("instanceCount").To<unsigned long long>(),3ull);
            const auto s=shape(scene,part(scene,id));const bool round=profile=="round";
            for(double x:{982.,992.,999.}) {
                EXPECT_EQ(BRepClass3d_SolidClassifier(s,gp_Pnt(x,round?19:0,round?0:9),1e-6).State(),TopAbs_OUT);
                EXPECT_EQ(BRepClass3d_SolidClassifier(s,gp_Pnt(x,round?-19:0,round?0:-9),1e-6).State(),TopAbs_IN);
            }
            EXPECT_EQ(BRepClass3d_SolidClassifier(s,gp_Pnt(970,round?19:0,round?0:9),1e-6).State(),TopAbs_IN);
        });
}
TEST(PunchPersistenceSDO, RoundCrossEndArrayReopensWithExactLayout) { crossing("round"); }
TEST(PunchPersistenceSDO, RoundedRectCrossEndArrayReopensWithExactLayout) { crossing("rect"); }
TEST(PunchPersistenceSDO, RoundedRectBranchAndDxfHolesReopenAndMove) {
    const auto branch=systemProfile("round",{{"width",10.},{"wallThickness",1.}});
    const auto feature=[](const std::string& id,double station,ObjectMap section){return ObjectMap{{"id",id},{"toolTarget",std::string("part")},
        {"recordKind",std::string(id=="branch"?"branch":"dxf")},{"station",station},{"reference",std::string("start")},{"layoutDatum",std::string("base")},
        {"toolRef",ObjectMap{{"id",std::string("branch-profile")}}},{"toolParameters",ObjectMap{}},{"section",section}};};
    acceptance("05-r2-branch-dxf-holes",request("验收-R2支管与DXF孔","rect",{},VariantArray{
        feature("branch",200,section(branch)),feature("dxf",700,section(dxfProfile(),"dxf"))}),[](ObjectMap& config){auto features=config.at("features").To<VariantArray>();
        for(int i=0;i<2;++i){auto f=features[i].To<ObjectMap>();f["station"]=i==0?250.:750.;features[i]=f;}config["features"]=features;},
        [](Scene& scene,const std::string& id){const auto s=shape(scene,part(scene,id));
            for(double x:{250.,750.})for(double z:{-9.,9.})EXPECT_EQ(BRepClass3d_SolidClassifier(s,gp_Pnt(x,0,z),1e-6).State(),TopAbs_OUT);
            for(double x:{200.,700.})for(double z:{-9.,9.})EXPECT_EQ(BRepClass3d_SolidClassifier(s,gp_Pnt(x,0,z),1e-6).State(),TopAbs_IN);});
}

TEST(MoldApplicabilitySDO, ActualTargetOverridesSpoofedRequestAndEnforcesTemplateGeometry) {
    for(const auto& toolId:std::vector<std::string>{"v-notch-sharp","edge-arc-groove"})
    for(const auto& profileId:std::vector<std::string>{"rect","round"}) {
        SCOPED_TRACE(profileId);
        SCOPED_TRACE(toolId);Scene scene;
        ObjectMap feature{{"toolTarget",std::string("part")},{"station",500.},
            {"toolRef",ObjectMap{{"id",toolId}}},{"toolParameters",ObjectMap{}}};
        auto payload=request("适用性机制测试",profileId,{},VariantArray{feature});
        payload["targetProfile"]=ObjectMap{{"status",std::string("verified")},{"typeId",std::string("rect")},
            {"capabilities",ObjectMap{{"opposedFlatFaces",true}}}};
        const auto drawing=payload.at("drawing").To<ObjectMap>();
        const auto expectedTarget=drawing.at("section").To<ObjectMap>().at("profile").To<ObjectMap>();
        const auto preview=invoke(scene,"PreviewPunchWizard",payload);
        ASSERT_TRUE(preview.contains("targetSection"));
        const auto target=preview.at("targetSection").To<ObjectMap>();
        ASSERT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(target),
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(expectedTarget));
        EXPECT_FALSE(target.contains("typeId"));
        EXPECT_TRUE(target.contains("contours"));
        if(profileId=="round" && toolId=="edge-arc-groove") {
            ASSERT_TRUE(preview.contains("resultError"));
            EXPECT_THROW(invoke(scene,"AddNestingPunchPart",payload),std::exception);
        } else {
            EXPECT_FALSE(preview.contains("resultError"));
            ASSERT_TRUE(preview.contains("sectionAnalyses"));
            ASSERT_EQ(preview.at("sectionAnalyses").To<VariantArray>().size(),1u);
            EXPECT_NO_THROW(invoke(scene,"AddNestingPunchPart",payload));
            feature["rotation"]=37.;
            payload["features"]=VariantArray{feature};
            if(profileId=="round") EXPECT_NO_THROW(invoke(scene,"AddNestingPunchPart",payload));
            else EXPECT_THROW(invoke(scene,"AddNestingPunchPart",payload),std::exception);
        }
    }
}

TEST(ToolLibraryPreviewSDO, NotchesUseLibraryPlacementAndResolvedSection) {
    Scene scene;
    for(const auto& id:std::vector<std::string>{
        "edge-arc-groove","embedded-arc-notch","segmented-bend","v-notch-sharp",
        "flexible-slit-bend"}) {
        SCOPED_TRACE(id);
        ObjectMap payload{{"profileRef",ObjectMap{{"scope",std::string("system")},{"id",std::string("rect")}}},
            {"parameters",ObjectMap{}},{"length",500.},{"toolsOnly",true}};
        // Resolve the real profile package without a personal-data store in this
        // headless scene. Cutter generation/placement uses the actual SDO path.
        payload["drawing"]=ObjectMap{{"length",500.},{"section",section(systemProfile("rect"))}};
        ObjectMap tool{{"toolRef",ObjectMap{{"id",id}}},{"toolParameters",ObjectMap{}}};
        if(id=="v-notch-sharp") tool["toolParameters"]=ObjectMap{
            {"bottomStrategy",std::string("relief")},{"reliefShape",std::string("circleWrap")},
            {"enclosedDiameter",16.},{"radialClearance",.1}};
        tool["id"]=std::string("library-preview");tool["enabled"]=true;tool["toolTarget"]=std::string("part");
        tool["face"]=std::string("top");tool["reference"]=std::string("center");tool["station"]=250.;
        tool["layoutDatum"]=std::string("base");tool["offset"]=0.;tool["rotation"]=0.;
        payload["features"]=VariantArray{tool};
        const auto preview=invoke(scene,"PreviewPunchWizard",payload);
        EXPECT_FALSE(preview.contains("resultError"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
        EXPECT_TRUE(preview.at("previewToolsComplete").To<bool>());
        EXPECT_TRUE(preview.contains("baseGeometry"));
        EXPECT_FALSE(preview.at("toolPreviews").To<VariantArray>().empty());
        const auto analyses=preview.at("sectionAnalyses").To<VariantArray>();
        ASSERT_EQ(1u,analyses.size());
        EXPECT_GT(analyses[0].To<ObjectMap>().at("parameters").To<ObjectMap>().at("wallThickness").To<double>(),0.);

        // A drawable cutter is not sufficient acceptance for a groove.  Run the
        // exact same default through the real subtraction path and require a
        // valid manufacturing solid before moving on to another profile.
        payload.erase("toolsOnly");
        payload["diagnosticBooleanPreview"]=true;
        const auto cutPreview=invoke(scene,"PreviewPunchWizard",payload);
        EXPECT_FALSE(cutPreview.contains("resultError"))
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(cutPreview);
        ASSERT_TRUE(cutPreview.contains("previewComputed"));
        EXPECT_TRUE(cutPreview.at("previewComputed").To<bool>());
        ASSERT_TRUE(cutPreview.contains("resultValid"));
        EXPECT_TRUE(cutPreview.at("resultValid").To<bool>());
        EXPECT_TRUE(cutPreview.contains("geometry"));

        payload["toolsOnly"]=true;
        payload.erase("diagnosticBooleanPreview");
        payload["drawing"]=ObjectMap{{"length",500.},{"section",section(systemProfile("round"))}};
        const auto roundPreview=invoke(scene,"PreviewPunchWizard",payload);
        if(id=="v-notch-sharp" || id=="segmented-bend" || id=="flexible-slit-bend") {
            EXPECT_FALSE(roundPreview.contains("resultError")) << (roundPreview.contains("resultError")
                ? roundPreview.at("resultError").To<std::string>() : std::string());
            EXPECT_TRUE(roundPreview.at("previewToolsComplete").To<bool>());

            payload.erase("toolsOnly");
            payload["diagnosticBooleanPreview"]=true;
            const auto roundCutPreview=invoke(scene,"PreviewPunchWizard",payload);
            EXPECT_FALSE(roundCutPreview.contains("resultError"))
                <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(roundCutPreview);
            ASSERT_TRUE(roundCutPreview.contains("resultValid"));
            EXPECT_TRUE(roundCutPreview.at("resultValid").To<bool>());
            EXPECT_TRUE(roundCutPreview.contains("geometry"));
        } else {
            EXPECT_TRUE(roundPreview.contains("resultError"));
            EXPECT_TRUE(roundPreview.at("toolPreviews").To<VariantArray>().empty());
            EXPECT_TRUE(roundPreview.contains("baseGeometry"));
        }
    }
}

TEST(ToolLibraryPreviewSDO, AllEndResourcesProduceRealStartAndEndCuts) {
    const std::vector<std::string> ids{"end-miter","end-key-joint","end-step-z","end-profile"};
    for(const auto& id:ids) for(const auto& profileId:std::vector<std::string>{"rect","round"})
    for(const auto& endKey:std::vector<std::string>{"start","end"}) {
        SCOPED_TRACE(id+"/"+profileId+"/"+endKey);Scene scene;
        ObjectMap tool{{"type",id},{"toolRef",ObjectMap{{"id",id}}},{"toolParameters",ObjectMap{}},
            {"trim",10.},{"rotation",0.}};
        if(id=="end-miter")tool["datum"]=std::string("long");
        if(id=="end-profile")tool["section"]=section(systemProfile("round"));
        auto payload=request("断面修整回归",profileId,{{endKey,tool}});
        payload["toolsOnly"]=true;
        const auto preview=invoke(scene,"PreviewPunchWizard",payload);
        ASSERT_FALSE(preview.contains("resultError"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
        ASSERT_TRUE(preview.at("previewToolsComplete").To<bool>());
        ASSERT_TRUE(preview.contains("baseGeometry"));
        ASSERT_FALSE(preview.at("toolPreviews").To<VariantArray>().empty());
        // Saving executes the real subtraction/contact/retained-material checks.
        payload.erase("toolsOnly");
        EXPECT_NO_THROW(invoke(scene,"AddNestingPunchPart",payload));
    }
}

TEST(ToolLibraryPreviewSDO, EveryDeclaredEndDefaultPassesNativePreviewAndSave) {
    std::vector<std::pair<std::string,ObjectMap>> descriptors;
    const auto root=std::filesystem::current_path()/"src/apps/tube-designer/templates/mold";
    for(const auto& entry:std::filesystem::directory_iterator(root)) {
        const auto path=entry.path()/"tool.json";
        if(!entry.is_directory() || !std::filesystem::exists(path))continue;
        std::ifstream file(path);
        const std::string json((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        const auto descriptor=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(json).To<ObjectMap>();
        if(descriptor.at("target").To<std::string>()=="end")
            descriptors.emplace_back(descriptor.at("id").To<std::string>(),descriptor);
    }
    ASSERT_EQ(4u,descriptors.size());
    for(const auto& [id,descriptor]:descriptors) {
        SCOPED_TRACE(id);Scene scene;
        ObjectMap tool{{"type",id},{"toolRef",ObjectMap{{"id",id}}},{"toolParameters",ObjectMap{}}};
        for(const auto& value:descriptor.at("operationParameters").To<VariantArray>()) {
            const auto definition=value.To<ObjectMap>();tool[definition.at("key").To<std::string>()]=definition.at("defaultValue");
        }
        if(descriptor.contains("requiresSection"))tool["section"]=section(systemProfile("round"));
        const auto profileId=descriptor.at("preview").To<ObjectMap>().at("profileRef").To<ObjectMap>().at("id").To<std::string>();
        auto payload=request("端切默认值回归",profileId,{{"start",tool}});
        payload["toolsOnly"]=true;
        const auto preview=invoke(scene,"PreviewPunchWizard",payload);
        ASSERT_FALSE(preview.contains("resultError"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
        ASSERT_TRUE(preview.at("previewToolsComplete").To<bool>());
        ASSERT_FALSE(preview.at("toolPreviews").To<VariantArray>().empty());
        payload.erase("toolsOnly");
        EXPECT_NO_THROW(invoke(scene,"AddNestingPunchPart",payload));
    }
}

TEST(PunchTargetSectionSDO, GenericProfileContoursDriveThroughOppositeAndBlindHoles) {
    for(const auto& profileId:std::vector<std::string>{"round","racetrack","oval"})
    for(const auto& mode:std::vector<std::string>{"through","opposite","blind"}) {
        SCOPED_TRACE(profileId+" / "+mode);Scene scene;
        ObjectMap feature{{"id",std::string("generic-hole")},{"toolTarget",std::string("side")},
            {"toolRef",ObjectMap{{"id",std::string("circle")}}},{"toolParameters",ObjectMap{{"diameter",6.}}},
            {"station",500.},{"face",std::string("round")},{"offset",0.}};
        if(mode=="opposite")feature["opposite"]=true;
        if(mode=="blind") {feature["blindHole"]=true;feature["cutDepth"]=1.;}
        auto payload=request("通用轮廓冲孔",profileId,{},VariantArray{feature});
        payload["diagnosticBooleanPreview"]=true;
        const auto drawing=payload.at("drawing").To<ObjectMap>();
        const auto expectedTarget=drawing.at("section").To<ObjectMap>().at("profile").To<ObjectMap>();
        const auto preview=invoke(scene,"PreviewPunchWizard",payload);
        EXPECT_FALSE(preview.contains("resultError")) << (preview.contains("resultError")
            ? preview.at("resultError").To<std::string>() : std::string());
        ASSERT_TRUE(preview.contains("targetSection"));
        const auto target=preview.at("targetSection").To<ObjectMap>();
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(target),
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(expectedTarget));
        EXPECT_FALSE(target.at("contours").To<VariantArray>().empty());
        EXPECT_TRUE(preview.at("resultValid").To<bool>());
    }
}

TEST(PunchPreviewDiagnosticsSDO, FullWidthPolygonBranchesDisplayCurrentToolsButCannotBeCreatedOrApplied) {
    const auto polygon=[](double size){return systemProfile("polygon",{{"sideCount",8},
        {"radius",size/2},{"wallThickness",2.}});};
    for(const auto& mother:std::vector<std::string>{"round","rect","polygon"}) {
        SCOPED_TRACE(mother);Scene scene;
        auto profile=mother=="polygon"?polygon(40):systemProfile(mother,mother=="round"?ObjectMap{}:
            ObjectMap{{"width",40.},{"depth",40.},{"wallThickness",2.},{"cornerRadius",2.}});
        ObjectMap feature{{"id",std::string("current-polygon")},{"toolTarget",std::string("part")},{"recordKind",std::string("branch")},
            {"toolRef",ObjectMap{{"id",std::string("branch-profile")}}},{"toolParameters",ObjectMap{}},
            {"angle",90.},{"azimuth",0.},{"direction",std::string("through")},{"section",section(polygon(20))},
            {"reference",std::string("center")},{"layoutReference",std::string("center")},{"station",100.},{"face",std::string("round")},
            {"layoutDatum",std::string("base")},{"arrayCount",2ull},{"arrayPitch",100.},{"arrayOffsets",VariantArray{0.,100.}},
            {"rowCount",1ull},{"rowOffsets",VariantArray{0.}}};
        ObjectMap payload{{"name",std::string("诊断隔离测试")},{"quantity",1ull},{"drawing",ObjectMap{{"length",1000.},{"section",section(profile)}}},
            {"features",VariantArray{feature}},{"ends",ObjectMap{}},{"diagnosticBooleanPreview",true}};
        const auto valid=invoke(scene,"PreviewPunchWizard",payload);ASSERT_TRUE(valid.at("resultValid").To<bool>());ASSERT_TRUE(valid.contains("geometry"));
        auto blank=payload;blank["features"]=VariantArray{};
        const auto id=invoke(scene,"AddNestingPunchPart",blank).at("partEntityId").To<std::string>();
        const auto initialRecipe=recipe(part(scene,id));const auto initialBytes=persistedShapeBytes(scene,part(scene,id));
        feature["section"]=section(polygon(60));payload["features"]=VariantArray{feature};
        const auto invalid=invoke(scene,"PreviewPunchWizard",payload);
        EXPECT_FALSE(invalid.at("resultValid").To<bool>());EXPECT_TRUE(invalid.contains("geometry"));
        EXPECT_TRUE(invalid.at("previewComputed").To<bool>());EXPECT_TRUE(invalid.at("previewToolsComplete").To<bool>());
        EXPECT_EQ(invalid.at("solidCount").To<unsigned long long>(),3ull);
        EXPECT_FALSE(invalid.contains("resultError"));EXPECT_FALSE(invalid.at("resultWarning").To<std::string>().empty());
        EXPECT_TRUE(invalid.at("toolCountExact").To<bool>());
        const auto actualResult=scene.Resources().Get<BRepModel>(scene.Resources().MakeNamedResourceURL("tube-designer/punch-preview"));
        ASSERT_TRUE(actualResult);const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*actualResult);ASSERT_TRUE(rebuilt.bOK);
        EXPECT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());std::size_t fragmentCount=0;
        for(TopExp_Explorer e(rebuilt.Shape,TopAbs_SOLID);e.More();e.Next())++fragmentCount;EXPECT_EQ(fragmentCount,3u);
        EXPECT_EQ(invalid.at("appliedPunchToolCount").To<unsigned long long>(),2ull);
        for(const auto* key:{"geometry","baseGeometry","toolGeometry"}) {
            const auto ref=invalid.at(key).To<ObjectMap>();const auto bytes=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(
                ref.at("url").To<std::string>(),ref.at("version").To<unsigned long long>());
            ASSERT_TRUE(bytes);EXPECT_GT(bytes->Size(),0u);
        }
        EXPECT_GT(invalid.at("toolGeometry").To<ObjectMap>().at("version").To<unsigned long long>(),
            valid.at("toolGeometry").To<ObjectMap>().at("version").To<unsigned long long>());
        try{invoke(scene,"AddNestingPunchPart",payload);FAIL()<<"Disconnected part was accepted";}
        catch(const std::exception& error){EXPECT_NE(std::string(error.what()).find("3 段"),std::string::npos);}
        auto apply=payload;apply["partEntityId"]=id;
        try{invoke(scene,"ApplyPunchWizard",apply);FAIL()<<"Disconnected part was applied";}
        catch(const std::exception& error){EXPECT_NE(std::string(error.what()).find("3 段"),std::string::npos);}
        EXPECT_EQ(initialRecipe,recipe(part(scene,id)));EXPECT_EQ(initialBytes,persistedShapeBytes(scene,part(scene,id)));
        feature["section"]=section(polygon(20));payload["features"]=VariantArray{feature};
        const auto corrected=invoke(scene,"PreviewPunchWizard",payload);EXPECT_TRUE(corrected.at("resultValid").To<bool>());
        EXPECT_TRUE(corrected.contains("geometry"));EXPECT_FALSE(corrected.contains("resultError"));
    }
}

TEST(PunchPreviewDiagnosticsSDO, EarlyEndFailureIsExplicitlyPartialAndIdentifiesTheEnd) {
    Scene scene;const auto branch=systemProfile("round",{{"width",10.},{"wallThickness",1.}});
    auto payload=request("端切诊断","round",{{"start",end("end-miter",{{"angle",45.}},10)},
        {"end",end("end-profile",{{"offsetY",1000.}},0,section(branch))}});
    payload["diagnosticBooleanPreview"]=true;
    const auto preview=invoke(scene,"PreviewPunchWizard",payload);
    EXPECT_FALSE(preview.at("resultValid").To<bool>());EXPECT_FALSE(preview.contains("geometry"));
    EXPECT_FALSE(preview.at("previewToolsComplete").To<bool>());EXPECT_FALSE(preview.at("toolCountExact").To<bool>());
    EXPECT_EQ(preview.at("failureStage").To<std::string>(),"ends");const auto target=preview.at("failureTarget").To<ObjectMap>();
    EXPECT_EQ(target.at("target").To<std::string>(),"end");EXPECT_EQ(target.at("key").To<std::string>(),"end");
    EXPECT_TRUE(preview.contains("baseGeometry"));EXPECT_TRUE(preview.contains("toolGeometry"));
    EXPECT_THROW(invoke(scene,"AddNestingPunchPart",payload),std::exception);
}

TEST(PunchPreviewDiagnosticsSDO, FilledOuterEndCutReceivesSubsequentHole) {
    Scene scene;const auto branch=systemProfile("round",{{"width",20.},{"wallThickness",2.}});
    const ObjectMap feature{{"id",std::string("after-end")},{"toolTarget",std::string("side")},
        {"toolRef",ObjectMap{{"id",std::string("circle")}}},{"toolParameters",ObjectMap{{"diameter",10.}}},
        {"station",700.},{"face",std::string("top")},{"reference",std::string("start")},{"layoutDatum",std::string("base")}};
    auto cutter=end("end-profile",{},0,section(branch));cutter["angle"]=90.;
    auto payload=request("端切后继续加工","round",{{"start",cutter}},VariantArray{feature});
    payload["diagnosticBooleanPreview"]=true;
    const auto preview=invoke(scene,"PreviewPunchWizard",payload);
    ASSERT_TRUE(preview.at("previewComputed").To<bool>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
    EXPECT_TRUE(preview.at("resultValid").To<bool>());
    EXPECT_EQ(preview.at("solidCount").To<unsigned long long>(),1ull);EXPECT_TRUE(preview.at("previewToolsComplete").To<bool>());
    EXPECT_EQ(preview.at("appliedPunchToolCount").To<unsigned long long>(),1ull);EXPECT_TRUE(preview.contains("geometry"));
    const auto model=scene.Resources().Get<BRepModel>(scene.Resources().MakeNamedResourceURL("tube-designer/punch-preview"));
    ASSERT_TRUE(model);const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*model);ASSERT_TRUE(rebuilt.bOK);
    EXPECT_EQ(BRepClass3d_SolidClassifier(rebuilt.Shape,gp_Pnt(700,0,19),1e-6).State(),TopAbs_OUT);
    EXPECT_EQ(BRepClass3d_SolidClassifier(rebuilt.Shape,gp_Pnt(680,0,19),1e-6).State(),TopAbs_IN);
    EXPECT_NO_THROW(invoke(scene,"AddNestingPunchPart",payload));
}

TEST(PunchPreviewDiagnosticsSDO, CompleteRemovalIsEditableWithoutAFalseResultMesh) {
    Scene scene;const auto largeSection=section(systemProfile("round",{{"width",100.},{"wallThickness",2.}}));
    auto payload=request("全部移除预览","round",{{"start",end("end-profile",{{"angle",0.}},0,largeSection)}});
    payload["diagnosticBooleanPreview"]=true;
    const auto preview=invoke(scene,"PreviewPunchWizard",payload);
    ASSERT_TRUE(preview.at("previewComputed").To<bool>());EXPECT_FALSE(preview.at("resultValid").To<bool>());
    EXPECT_EQ(preview.at("solidCount").To<unsigned long long>(),0ull);EXPECT_FALSE(preview.contains("geometry"));
    EXPECT_FALSE(preview.contains("resultError"));EXPECT_TRUE(preview.contains("baseGeometry"));EXPECT_TRUE(preview.contains("toolGeometry"));
    EXPECT_FALSE(preview.at("resultWarning").To<std::string>().empty());
    try{invoke(scene,"AddNestingPunchPart",payload);FAIL()<<"An empty part was accepted";}
    catch(const std::exception& error){EXPECT_NE(std::string(error.what()).find("0 段"),std::string::npos);}
}

TEST(PunchPreviewDiagnosticsSDO, EditingOneRecordPreservesOtherToolAndBlankResourceVersions) {
    Scene scene;
    const auto feature=[](const std::string& id,double x){return ObjectMap{{"id",id},{"type",std::string("circle")},
        {"diameter",10.},{"station",x},{"face",std::string("top")},{"reference",std::string("start")},{"layoutDatum",std::string("base")}};};
    auto payload=request("局部预览资源","rect",{{"start",end("end-miter",{{"angle",45.}},10)}},
        VariantArray{feature("edit-this",200),feature("keep-this",700)});
    const auto readTools=[](const ObjectMap& preview){std::map<std::string,ObjectMap> result;
        for(const auto& value:preview.at("toolPreviews").To<VariantArray>()){const auto item=value.To<ObjectMap>();
            result[item.at("target").To<std::string>()+":"+item.at("key").To<std::string>()]=item.at("geometry").To<ObjectMap>();}return result;};
    const auto first=invoke(scene,"PreviewPunchWizard",payload);const auto firstTools=readTools(first);ASSERT_EQ(firstTools.size(),3u);
    payload["features"]=VariantArray{feature("edit-this",250),feature("keep-this",700)};
    const auto second=invoke(scene,"PreviewPunchWizard",payload);const auto secondTools=readTools(second);ASSERT_EQ(secondTools.size(),3u);
    EXPECT_EQ(first.at("baseGeometry"),second.at("baseGeometry"));
    EXPECT_EQ(firstTools.at("feature:keep-this"),secondTools.at("feature:keep-this"));
    EXPECT_EQ(firstTools.at("end:start"),secondTools.at("end:start"));
    EXPECT_EQ(firstTools.at("feature:edit-this").at("url"),secondTools.at("feature:edit-this").at("url"));
    EXPECT_GT(secondTools.at("feature:edit-this").at("version").To<unsigned long long>(),firstTools.at("feature:edit-this").at("version").To<unsigned long long>());
    const auto repeated=invoke(scene,"PreviewPunchWizard",payload);EXPECT_EQ(readTools(repeated),secondTools);
    EXPECT_EQ(repeated.at("baseGeometry"),second.at("baseGeometry"));
}

TEST(PunchPlacementPreviewSDO, DefaultPreviewDoesNotCutOrRejectOutsideCandidates) {
    Scene scene;const auto branch=systemProfile("polygon",{{"shapeMode",std::string("regular")},{"sideCount",8},{"width",40.},{"depth",40.},{"wallThickness",2.}});
    ObjectMap f{{"id",std::string("all-candidates")},{"toolTarget",std::string("part")},{"recordKind",std::string("branch")},
        {"toolRef",ObjectMap{{"id",std::string("branch-profile")}}},{"toolParameters",ObjectMap{}},{"section",section(branch)},
        {"station",600.},{"arrayCount",2ull},{"arrayPitch",50.},{"reference",std::string("start")},{"face",std::string("round")}};
    auto payload=request("只摆放刀具","round",{},VariantArray{f});
    for(double x:{600.,1500.}) {
        f["station"]=x;payload["features"]=VariantArray{f};const auto p=invoke(scene,"PreviewPunchWizard",payload);
        EXPECT_TRUE(p.at("toolsOnly").To<bool>());EXPECT_TRUE(p.at("previewToolsComplete").To<bool>());
        EXPECT_FALSE(p.contains("geometry"));EXPECT_FALSE(p.contains("previewComputed"));EXPECT_FALSE(p.contains("resultValid"));
        EXPECT_FALSE(p.contains("resultError"));EXPECT_FALSE(p.contains("outsideToolCount"));EXPECT_FALSE(p.at("toolCountExact").To<bool>());
        EXPECT_EQ(p.at("candidateToolCount").To<unsigned long long>(),2ull);EXPECT_EQ(p.at("placedToolCount").To<unsigned long long>(),2ull);
        EXPECT_EQ(p.at("toolPreviews").To<VariantArray>().size(),1u);
        const auto data=scene.Resources().Get<BRepModel>(scene.Resources().MakeNamedResourceURL("tube-designer/punch-preview/tool/side/all-candidates"));
        ASSERT_TRUE(data);const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*data);ASSERT_TRUE(rebuilt.bOK);
        Bnd_Box box;BRepBndLib::AddOptimal(rebuilt.Shape,box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        EXPECT_NEAR(x0,x-20,1e-4);EXPECT_NEAR(x1,x+70,1e-4);
        EXPECT_THROW(invoke(scene,"AddNestingPunchPart",payload),std::exception);
    }
}

VariantArray rigidMatrix(double x=0,double y=0,double z=0) {return {1.,0.,0.,x,0.,1.,0.,y,0.,0.,1.,z,0.,0.,0.,1.};}
ObjectMap arrayFeature(VariantArray transforms,unsigned long long candidates=1) {
    return {{"id",std::string("matrix-array")},{"type",std::string("circle")},{"diameter",3.},{"station",200.},
        {"face",std::string("top")},{"reference",std::string("start")},{"layoutDatum",std::string("base")},
        {"arrayGroups",VariantArray{ObjectMap{{"id",std::string("x")},{"type",std::string("linear")},{"axis",std::string("X")},{"count",3ull},{"spacing",100.}},
            ObjectMap{{"id",std::string("y")},{"type",std::string("linear")},{"axis",std::string("Y")},{"count",2ull},{"spacing",5.}}}},
        {"arrayCandidateCount",candidates},{"arrayTransforms",transforms},
        {"arrayCount",99ull},{"arrayPitch",77.},{"rowCount",99ull},{"rowPitch",77.},
        {"arrayOffsets",VariantArray{999.}},{"rowOffsets",VariantArray{999.}}};
}

TEST(PunchArrayGroupsSDO, SideMatricesReplaceLegacyArraysAndSurviveSaveReopenAndEdit) {
    auto f=arrayFeature({rigidMatrix(),rigidMatrix(100),rigidMatrix(0,5),rigidMatrix(100,5)},6);
    f["arraySkips"]=VariantArray{ObjectMap{{"x",2ull}}};
    acceptance("06-array-groups",request("多层阵列原生验收","rect",{},VariantArray{f}),[](ObjectMap& config){
        auto values=config.at("features").To<VariantArray>();auto feature=values[0].To<ObjectMap>();
        feature["station"]=250.;values[0]=feature;config["features"]=values;
    },[](Scene& scene,const std::string& id){const auto saved=recipe(part(scene,id)).at("features").To<VariantArray>()[0].To<ObjectMap>();
        EXPECT_EQ(saved.at("arrayCandidateCount").To<unsigned long long>(),6ull);EXPECT_EQ(saved.at("arrayCount").To<unsigned long long>(),1ull);
        EXPECT_EQ(saved.at("rowCount").To<unsigned long long>(),1ull);EXPECT_EQ(saved.at("arrayTransforms").To<VariantArray>().size(),4u);
        EXPECT_EQ(saved.at("arraySkips").To<VariantArray>().size(),1u);const auto s=shape(scene,part(scene,id));
        for(double x:{250.,350.})for(double y:{0.,5.})EXPECT_EQ(BRepClass3d_SolidClassifier(s,gp_Pnt(x,y,9),1e-6).State(),TopAbs_OUT);
        for(double x:{200.,400.})EXPECT_EQ(BRepClass3d_SolidClassifier(s,gp_Pnt(x,0,9),1e-6).State(),TopAbs_IN);
    });
}

TEST(PunchArrayGroupsSDO, BranchAndDxfUseWorldRotationWithoutSecondEndReversal) {
    for(bool dxf:{false,true}) {
        SCOPED_TRACE(dxf);Scene scene;auto f=arrayFeature({rigidMatrix(),VariantArray{-1.,0.,0.,1000.,0.,1.,0.,0.,0.,0.,-1.,0.,0.,0.,0.,1.}},2);
        f["toolTarget"]=std::string("part");f["recordKind"]=std::string(dxf?"dxf":"branch");f["toolRef"]=ObjectMap{{"id",std::string("branch-profile")}};
        f["toolParameters"]=ObjectMap{};f["section"]=section(dxf?dxfProfile():systemProfile("round",{{"width",8.},{"wallThickness",1.}}),dxf?"dxf":"library");
        f["arrayGroups"]=VariantArray{ObjectMap{{"id",std::string("polar-y")},{"type",std::string("polar")},{"axis",std::string("Y")},
            {"count",2ull},{"angleMode",std::string("pitch")},{"angleStep",180.},{"origin",VariantArray{500.,0.,0.}}}};
        f["reference"]=std::string("end");f["face"]=std::string("round");
        const auto payload=request("全局旋转阵列","round",{},VariantArray{f});const auto p=invoke(scene,"PreviewPunchWizard",payload);
        EXPECT_EQ(p.at("placedToolCount").To<unsigned long long>(),2ull);
        const auto id=invoke(scene,"AddNestingPunchPart",payload).at("partEntityId").To<std::string>();const auto s=shape(scene,part(scene,id));
        for(double x:{200.,800.})for(double z:{-19.,19.})EXPECT_EQ(BRepClass3d_SolidClassifier(s,gp_Pnt(x,0,z),1e-6).State(),TopAbs_OUT);
        EXPECT_EQ(BRepClass3d_SolidClassifier(s,gp_Pnt(500,0,19),1e-6).State(),TopAbs_IN);
    }
}

TEST(PunchArrayGroupsSDO, RejectsMalformedTransformsAndCandidateBudgetButAllowsDisabledDraft) {
    Scene scene;const auto invokeFeature=[&](const ObjectMap& f){return invoke(scene,"PreviewPunchWizard",request("阵列安全检查","round",{},VariantArray{f}));};
    auto f=arrayFeature({rigidMatrix()},1001);EXPECT_THROW(invokeFeature(f),std::exception);
    f=arrayFeature({rigidMatrix(),rigidMatrix()},2);EXPECT_THROW(invokeFeature(f),std::exception);
    for(const auto& invalid:std::vector<VariantArray>{VariantArray{1.,2.},VariantArray{2.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.},
        VariantArray{-1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.},VariantArray{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,1.,0.,0.,1.}})
        EXPECT_THROW(invokeFeature(arrayFeature({invalid})),std::exception);
    f=arrayFeature({});EXPECT_THROW(invokeFeature(f),std::exception);f["enabled"]=false;f["arrayCandidateCount"]=0ull;
    EXPECT_NO_THROW(invoke(scene,"AddNestingPunchPart",request("停用阵列草稿","round",{},VariantArray{f})));
}

TEST(PartDrawingIndependentSDO, OwnRecipeAllowsMultipleSolidsAndReopensIndependently) {
    Scene scene;auto branch=systemProfile("polygon",{{"shapeMode",std::string("regular")},{"sideCount",8},{"width",40.},{"depth",40.},{"wallThickness",2.}});
    ObjectMap f{{"id",std::string("drawing-cutter")},{"toolTarget",std::string("part")},{"recordKind",std::string("branch")},
        {"toolRef",ObjectMap{{"id",std::string("branch-profile")}}},{"section",section(branch)},{"station",600.},{"arrayCount",2ull},{"arrayPitch",50.}};
    auto payload=request("独立三维绘制多实体","round",{},VariantArray{f});
    EXPECT_FALSE(invoke(scene,"GetPartDrawingTools",{}).empty());const auto preview=invoke(scene,"PreviewPartDrawing",payload);
    EXPECT_TRUE(preview.at("toolsOnly").To<bool>());EXPECT_FALSE(preview.contains("geometry"));
    const auto id=invoke(scene,"AddPartDrawing",payload).at("partEntityId").To<std::string>();const auto initial=part(scene,id);
    EXPECT_FALSE(initial->GetItemProperties().contains("tubeDesigner.partDrawing"));EXPECT_FALSE(initial->GetItemProperties().contains("tubeDesigner.punchWizard"));
    const auto drawingComponent=std::dynamic_pointer_cast<CPartDrawingComponent>(
        initial->GetEntity()->GetComponent(CPartDrawingComponent::S_ClassName));
    ASSERT_TRUE(drawingComponent);EXPECT_FALSE(drawingComponent->GetDefinition().empty());
    int n=0;for(TopExp_Explorer e(shape(scene,initial),TopAbs_SOLID);e.More();e.Next())++n;EXPECT_EQ(n,3);
    const auto bytes=persistedShapeBytes(scene,initial);const auto original=drawingComponent->GetDefinition();
    const auto path=std::filesystem::current_path()/"tmp/punch-persistence-acceptance/07-independent-part-drawing.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="三维独立存储验收";
    file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());
    EXPECT_EQ(bytes,persistedShapeBytes(reopened,part(reopened,id)));EXPECT_EQ(original,
        std::dynamic_pointer_cast<CPartDrawingComponent>(part(reopened,id)->GetEntity()->GetComponent(CPartDrawingComponent::S_ClassName))->GetDefinition());
    payload["partEntityId"]=id;payload["features"]=VariantArray{};invoke(reopened,"ApplyPartDrawing",payload);
    const auto edited=part(reopened,id);EXPECT_FALSE(edited->GetItemProperties().contains("tubeDesigner.partDrawing"));EXPECT_FALSE(edited->GetItemProperties().contains("tubeDesigner.punchWizard"));
    const auto editedDrawingComponent=std::dynamic_pointer_cast<CPartDrawingComponent>(
        edited->GetEntity()->GetComponent(CPartDrawingComponent::S_ClassName));
    ASSERT_TRUE(editedDrawingComponent);
    EXPECT_TRUE(editedDrawingComponent->GetDefinition().at("features").To<VariantArray>().empty());
    EXPECT_THROW(invoke(scene,"AddNestingPunchPart",request("冲孔仍须单段","round",{},VariantArray{f})),std::exception);
}

TEST(ProductTemplatePreviewSDO, EveryCurrentProductDefaultPassesNativePreview) {
    auto source = std::filesystem::path(__FILE__).parent_path();
    while (!source.empty() && !std::filesystem::is_directory(source / "apps/tube-designer/templates/product")) {
        const auto parent = source.parent_path();
        if (parent == source) break;
        source = parent;
    }
    const auto root = source / "apps/tube-designer/templates/product";
    ASSERT_TRUE(std::filesystem::is_directory(root));
    std::map<std::string, ObjectMap> descriptors;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const auto path = entry.path() / "template.json";
        if (!entry.is_directory() || !std::filesystem::is_regular_file(path)) continue;
        std::ifstream file(path);
        const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto descriptor = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(json).To<ObjectMap>();
        descriptors.emplace(descriptor.at("id").To<std::string>(), descriptor);
    }
    ASSERT_FALSE(descriptors.empty());
    Scene scene;
    for (const auto& [id, descriptor] : descriptors) {
        SCOPED_TRACE(id);
        try {
            const auto started = std::chrono::steady_clock::now();
            const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{{"templateId", id}});
            EXPECT_EQ(response.at("templateId").To<std::string>(), id);
            const auto items = response.at("items").To<VariantArray>();
            EXPECT_FALSE(items.empty());
            const auto parameters = response.at("parameters").To<ObjectMap>();
            for (const auto& field : descriptor.at("parameters").To<VariantArray>())
                EXPECT_TRUE(parameters.contains(field.To<ObjectMap>().at("key").To<std::string>()));
            std::set<std::string> resources;
            for (const auto& value : items) {
                const auto item = value.To<ObjectMap>();
                EXPECT_EQ(16u, item.at("transform").To<VariantArray>().size());
                resources.insert(item.at("geometry").To<ObjectMap>().at("url").To<std::string>());
            }
            std::cout << "[product-audit] " << id << " items=" << items.size()
                << " meshes=" << resources.size() << " seconds="
                << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << '\n';
        } catch (const std::exception& error) {
            ADD_FAILURE() << id << ": " << error.what();
        }
    }
}

TEST(ProductTemplatePreviewSDO, InactiveDraftValuesDoNotBlockNativePreview) {
    const std::vector<std::tuple<std::string, std::string, double, ObjectMap>> cases{
        {"modular-guardrail-glass-straight", "infillWidth", 300.0, {}},
        {"modular-guardrail-glass-straight", "infillWallThickness", 50.0, {}},
        {"single-face-security-window", "horizontalWallThickness", 2.0,
            {{"infillPattern", std::string("vertical")}}},
        {"single-face-security-window", "verticalWidth", 21.85,
            {{"infillPattern", std::string("horizontal")}}},
    };
    for (const auto& [id, key, value, extra] : cases) {
        SCOPED_TRACE(id + ":" + key);
        Scene scene;
        ObjectMap parameters{{key, value}};
        parameters.insert(extra.begin(), extra.end());
        ObjectMap request{{"templateId", id}, {"parameters", parameters}};
        const auto response = invoke(scene, "GenerateProductTemplatePreview", request);
        EXPECT_EQ(response.at("templateId").To<std::string>(), id);
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
        EXPECT_EQ(response.at("parameters").To<ObjectMap>().at(key).To<double>(), value);
    }
}

TEST(ProductTemplatePreviewSDO, ReturnsRuntimeGeometryWithoutCreatingProductRecords) {
    Scene scene;
    const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("straight-steel-staircase")},
    });
    const auto items = response.at("items").To<VariantArray>();
    ASSERT_FALSE(items.empty());
    EXPECT_EQ(response.at("templateId").To<std::string>(), "straight-steel-staircase");
    EXPECT_TRUE(response.at("material").To<ObjectMap>().contains("url"));
    EXPECT_FALSE(response.contains("productEntityId"));
    EXPECT_FALSE(response.contains("generationRunId"));
    std::set<std::string> geometryResources;
    for (const auto& value : items) {
        const auto item = value.To<ObjectMap>();
        ASSERT_TRUE(item.contains("transform"));
        EXPECT_EQ(16u, item.at("transform").To<VariantArray>().size());
        const auto geometry = item.at("geometry").To<ObjectMap>();
        geometryResources.insert(geometry.at("url").To<std::string>());
    }
    EXPECT_LT(geometryResources.size(), items.size())
        << "repeated template members should reference shared prototype meshes";
}

TEST(ProductTemplatePreviewSDO, DISABLED_FiveFaceEscapeWindowPreviewBenchmark) {
    Scene scene;
    const auto startupStart = std::chrono::steady_clock::now();
    const auto startup = invoke(scene, "List", {});
    const auto startupSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startupStart).count();
    const auto startupTemplates = startup.at("tubeDesigner").To<ObjectMap>()
        .at("templates").To<VariantArray>();
    const auto startupTemplate = std::find_if(startupTemplates.begin(), startupTemplates.end(),
        [](const auto& value) { return value.To<ObjectMap>().at("id").To<std::string>()
            == "single-face-security-window"; });
    ASSERT_NE(startupTemplate, startupTemplates.end());
    ASSERT_FALSE(startupTemplate->To<ObjectMap>().at("parameters").To<VariantArray>().empty());
    std::cout << "[five-face escape startup] templates=" << startupTemplates.size()
        << " seconds=" << startupSeconds << '\n';
    const auto start = std::chrono::steady_clock::now();
    const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"parameters", ObjectMap{
            {"faceType", std::string("five")},
            {"accessDoorEnabled", true},
            {"accessDoorFace5", std::string("front")},
        }},
    });
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    const auto items = response.at("items").To<VariantArray>();
    const auto parameters = response.at("parameters").To<ObjectMap>();
    ASSERT_FALSE(items.empty());
    EXPECT_EQ("five", parameters.at("faceType").To<std::string>());
    EXPECT_TRUE(parameters.at("accessDoorEnabled").To<bool>());
    EXPECT_EQ("front", parameters.at("accessDoorFace5").To<std::string>());
    std::set<std::string> resources;
    for (const auto& value : items) {
        const auto item = value.To<ObjectMap>();
        resources.insert(item.at("geometry").To<ObjectMap>().at("url").To<std::string>());
    }
    std::cout << "[five-face escape preview] items=" << items.size()
        << " mesh_resources=" << resources.size()
        << " seconds=" << elapsed << '\n';
    if (response.contains("profilingMs"))
        for (const auto& [stage, value] : response.at("profilingMs").To<ObjectMap>())
            std::cout << "[five-face escape preview] " << stage << "="
                << value.To<double>() << "ms\n";
    const auto repeatedStart = std::chrono::steady_clock::now();
    const auto repeated = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"parameters", ObjectMap{
            {"faceType", std::string("five")},
            {"accessDoorEnabled", true},
            {"accessDoorFace5", std::string("front")},
        }},
    });
    const auto repeatedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - repeatedStart).count();
    std::map<std::string, std::pair<std::string, std::uint64_t>> firstResources;
    for (const auto& value : items) {
        const auto item = value.To<ObjectMap>();
        const auto geometry = item.at("geometry").To<ObjectMap>();
        firstResources.emplace(item.at("key").To<std::string>(), std::make_pair(
            geometry.at("url").To<std::string>(), geometry.at("version").To<std::uint64_t>()));
    }
    std::size_t unchanged = 0;
    for (const auto& value : repeated.at("items").To<VariantArray>()) {
        const auto item = value.To<ObjectMap>();
        const auto geometry = item.at("geometry").To<ObjectMap>();
        const auto previous = firstResources.find(item.at("key").To<std::string>());
        if (previous != firstResources.end() && previous->second == std::make_pair(
            geometry.at("url").To<std::string>(), geometry.at("version").To<std::uint64_t>()))
            ++unchanged;
    }
    EXPECT_GE(unchanged, 70u);
    std::cout << "[five-face escape repeat] seconds=" << repeatedSeconds
        << " unchanged_mesh_references=" << unchanged << '\n';
    if (repeated.contains("profilingMs"))
        for (const auto& [stage, value] : repeated.at("profilingMs").To<ObjectMap>())
            std::cout << "[five-face escape repeat] " << stage << "="
                << value.To<double>() << "ms\n";
    const auto changedStart = std::chrono::steady_clock::now();
    const auto changed = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"parameters", ObjectMap{
            {"faceType", std::string("five")},
            {"accessDoorEnabled", true},
            {"accessDoorFace5", std::string("front")},
            {"width", 1300.0},
        }},
    });
    const auto changedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - changedStart).count();
    EXPECT_EQ(1300.0, changed.at("parameters").To<ObjectMap>().at("width").To<double>());
    std::size_t changedUnchanged = 0;
    for (const auto& value : changed.at("items").To<VariantArray>()) {
        const auto item = value.To<ObjectMap>();
        const auto geometry = item.at("geometry").To<ObjectMap>();
        const auto previous = firstResources.find(item.at("key").To<std::string>());
        if (previous != firstResources.end() && previous->second == std::make_pair(
            geometry.at("url").To<std::string>(), geometry.at("version").To<std::uint64_t>()))
            ++changedUnchanged;
    }
    EXPECT_GT(changedUnchanged, 0u);
    EXPECT_LT(changedUnchanged, items.size());
    std::cout << "[five-face escape width change] seconds=" << changedSeconds
        << " unchanged_mesh_references=" << changedUnchanged << '\n';
    if (changed.contains("profilingMs"))
        for (const auto& [stage, value] : changed.at("profilingMs").To<ObjectMap>())
            std::cout << "[five-face escape width change] " << stage << "="
                << value.To<double>() << "ms\n";
}

TEST(ProductTemplatePreviewSDO, DISABLED_FiveFaceEscapeWindowFirstAddBenchmark) {
    Scene scene;
    const auto startupStart = std::chrono::steady_clock::now();
    const auto startup = invoke(scene, "List", {});
    ASSERT_FALSE(startup.at("tubeDesigner").To<ObjectMap>()
        .at("templates").To<VariantArray>().empty());
    const auto startupSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startupStart).count();
    const auto addStart = std::chrono::steady_clock::now();
    const auto added = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"faceType", std::string("five")},
        {"accessDoorEnabled", true},
        {"accessDoorFace5", std::string("front")},
        {"receiptOnly", true},
    });
    const auto addSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - addStart).count();
    const auto designer = added.at("tubeDesigner").To<ObjectMap>();
    for (const auto& [key, value] : designer)
        std::cout << "[five-face snapshot] " << key << "="
            << iCAX::Data::VariantSerializer::Serialize(value).size() << " bytes\n";
    const auto members = designer.at("members").To<VariantArray>();
    ASSERT_FALSE(members.empty());
    EXPECT_TRUE(designer.at("receiptOnly").To<bool>());
    const auto detailStart = std::chrono::steady_clock::now();
    const auto details = invoke(scene, "List", {}).at("tubeDesigner").To<ObjectMap>();
    const auto detailSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - detailStart).count();
    EXPECT_EQ(members.size(), details.at("members").To<VariantArray>().size());
    std::cout << "[five-face escape first add] startup=" << startupSeconds
        << "s add=" << addSeconds << "s detail=" << detailSeconds
        << "s members=" << members.size() << '\n';
}

TEST(ProductInstanceSDO, AddedProductsShareMeshesAndKeepWorldPlacements) {
    Scene catalogScene;
    const auto templates = invoke(catalogScene, "List", {}).at("tubeDesigner").To<ObjectMap>()
        .at("templates").To<VariantArray>();
    ASSERT_FALSE(templates.empty());
    for (const auto& descriptor : templates) {
        const auto id = descriptor.To<ObjectMap>().at("id").To<std::string>();
        SCOPED_TRACE(id);
        Scene scene;
        ObjectMap request{{"templateId", id}};
        if (id == "single-face-security-window") request["faceType"] = std::string("five");
        const auto added = invoke(scene, "GeneratePreview", request).at("tubeDesigner").To<ObjectMap>();
        const auto members = added.at("members").To<VariantArray>();
        ASSERT_FALSE(members.empty());
        const auto productID = *iCAX::Data::uuid::from_string(
            added.at("product").To<ObjectMap>().at("entityId").To<std::string>());
        const auto product = std::dynamic_pointer_cast<iCAX::TubeDesigner::CProductInstanceComponent>(
            scene.Database().GetEntity(productID)->GetComponent("CProductInstanceComponent"));
        ASSERT_TRUE(product);
        const auto run = std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
            scene.Database().GetEntity(product->GetActiveGenerationRunID())->GetComponent("CGenerationRunComponent"));
        ASSERT_TRUE(run);
        const auto model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
            iCAX::Data::Variant(run->GetNeutralModel()));
        const auto geometry = iCAX::OpenCascade::EvaluateNeutralModel(model);
        const auto verify = [&](Scene& current, const VariantArray& rows) {
            std::set<std::string> resources;
            for (const auto& row : rows) {
                const auto member = row.To<ObjectMap>();
                const auto key = member.at("stableKey").To<std::string>();
                SCOPED_TRACE(key);
                const auto item = std::find_if(model.Items.begin(), model.Items.end(),
                    [&](const auto& candidate) { return candidate.Key == key; });
                ASSERT_NE(item, model.Items.end());
                const auto shape = geometry.At(item->Representations.at("result"));
                const auto entity = current.Database().GetEntity(*iCAX::Data::uuid::from_string(
                    member.at("entityId").To<std::string>()));
                const auto transform = entity->GetComponent("CTransformComponent");
                ASSERT_TRUE(transform);
                const auto matrix = transform->GetProperty("LocalToWorldMatrix").To<iCAX::Data::Double4x4>();
                const auto expected = shape.Location().Transformation();
                const auto snapshotMatrix = member.at("transform").To<VariantArray>();
                ASSERT_EQ(snapshotMatrix.size(), 16u);
                for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) {
                    EXPECT_NEAR(matrix(r,c), expected.Value(r+1,c+1), 1e-8);
                    EXPECT_NEAR(matrix(r,c), snapshotMatrix[r*4+c].To<double>(), 1e-8);
                }
                const auto url = member.at("previewGeometryResourceId").To<std::string>();
                resources.insert(url);
                const auto mesh = current.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(
                    url, member.at("previewGeometryResourceVersion").To<std::uint64_t>());
                ASSERT_TRUE(mesh);
                ASSERT_FALSE(mesh->Mesh.Vertices.empty());
                Bnd_Box meshBounds, shapeBounds;
                for (const auto& v : mesh->Mesh.Vertices)
                    meshBounds.Add(gp_Pnt(matrix(0,0)*v.X+matrix(0,1)*v.Y+matrix(0,2)*v.Z+matrix(0,3),
                        matrix(1,0)*v.X+matrix(1,1)*v.Y+matrix(1,2)*v.Z+matrix(1,3),
                        matrix(2,0)*v.X+matrix(2,1)*v.Y+matrix(2,2)*v.Z+matrix(2,3)));
                BRepBndLib::AddOptimal(shape, shapeBounds, false, false);
                double actual[6], reference[6];
                meshBounds.Get(actual[0],actual[1],actual[2],actual[3],actual[4],actual[5]);
                shapeBounds.Get(reference[0],reference[1],reference[2],reference[3],reference[4],reference[5]);
                // Display tessellation has a 1 mm chord tolerance, unlike production topology.
                for (int i=0;i<6;++i) EXPECT_NEAR(actual[i],reference[i],1.1);
            }
            if (rows.size() > 2) EXPECT_LT(resources.size(), rows.size());
            std::cout << "[product-instance-sharing] " << id << " members=" << rows.size()
                << " meshes=" << resources.size() << '\n';
        };
        verify(scene, members);
        const auto dir=std::filesystem::current_path()/"tmp/product-template-rectification";
        std::filesystem::create_directories(dir);
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",
            .CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;
        info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName=id;
        const auto path=dir/(id+"-shared-display.ictd");
        file.Save(path,info,scene.Database(),scene.Resources());
        Scene reopened(scene.project,scene.GetSceneID(),false);
        file.Open(path,reopened.Database(),reopened.Resources());
        const auto restored=invoke(reopened,"List",{}).at("tubeDesigner").To<ObjectMap>();
        verify(reopened,restored.at("members").To<VariantArray>());
    }
}

TEST(ProductInstanceSDO, DisplayMeshRestoresAndSideSketchBuildsSolidOnlyOnDemand) {
    Scene scene;
    const auto added = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto members = added.at("members").To<VariantArray>();
    ASSERT_FALSE(members.empty());
    std::map<std::string, std::size_t> sharedCounts;
    for (const auto& value : members)
        ++sharedCounts[value.To<ObjectMap>().at("previewGeometryResourceId").To<std::string>()];
    ObjectMap sketchTarget;
    for (const auto& value : members) {
        const auto member = value.To<ObjectMap>();
        const auto properties = member.at("properties").To<ObjectMap>();
        EXPECT_EQ("triangle-mesh", properties.at("tubeDesigner.previewGeometryKind").To<std::string>());
        const auto url = member.at("previewGeometryResourceId").To<std::string>();
        const auto version = member.at("previewGeometryResourceVersion").To<std::uint64_t>();
        EXPECT_TRUE(scene.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(url, version));
        if (sketchTarget.empty() && sharedCounts[url] > 1 && member.at("length").To<double>() > 0.0
            && !member.at("profile").To<ObjectMap>().empty()) sketchTarget = member;
    }
    ASSERT_FALSE(sketchTarget.empty());

    scene.Resources().Clear();
    const auto restored = invoke(scene, "List", {}).at("tubeDesigner").To<ObjectMap>();
    for (const auto& value : restored.at("members").To<VariantArray>()) {
        const auto member = value.To<ObjectMap>();
        EXPECT_TRUE(scene.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(
            member.at("previewGeometryResourceId").To<std::string>(),
            member.at("previewGeometryResourceVersion").To<std::uint64_t>()));
    }

    const auto saved = invoke(scene, "SaveSketch", ObjectMap{
        {"productEntityId", added.at("product").To<ObjectMap>().at("entityId")},
        {"targetMemberId", sketchTarget.at("entityId")},
        {"sketch", ObjectMap{
            {"schema", std::string("icax.tube-sketch")}, {"schemaVersion", 1ull},
            {"kind", std::string("side")}, {"unit", std::string("mm")},
            {"length", sketchTarget.at("length")}, {"faceHeight", 40.0},
            {"entities", VariantArray{}},
        }},
    }).at("tubeDesigner").To<ObjectMap>();
    bool foundSolid = false;
    for (const auto& value : saved.at("members").To<VariantArray>()) {
        const auto member = value.To<ObjectMap>();
        if (member.at("entityId") != sketchTarget.at("entityId")) {
            const auto initial=std::find_if(members.begin(),members.end(),[&](const auto& candidate){
                return candidate.To<ObjectMap>().at("entityId")==member.at("entityId");});
            ASSERT_NE(initial,members.end());
            for(const auto* key:{"previewGeometryResourceId","previewGeometryResourceVersion","transform"})
                EXPECT_EQ(initial->To<ObjectMap>().at(key),member.at(key));
            continue;
        }
        foundSolid = true;
        EXPECT_NE(member.at("previewGeometryResourceId"),sketchTarget.at("previewGeometryResourceId"));
        const auto entity=scene.Database().GetEntity(*iCAX::Data::uuid::from_string(
            member.at("entityId").To<std::string>()));
        const auto matrix=entity->GetComponent("CTransformComponent")
            ->GetProperty("LocalToWorldMatrix").To<iCAX::Data::Double4x4>();
        for(int r=0;r<4;++r)for(int c=0;c<4;++c)EXPECT_NEAR(matrix(r,c),r==c?1.0:0.0,1e-10);
        EXPECT_EQ("brep", member.at("properties").To<ObjectMap>()
            .at("tubeDesigner.previewGeometryKind").To<std::string>());
        EXPECT_TRUE(scene.Resources().Get<iCAX::GeometryData::BRepModel>(
            member.at("previewGeometryResourceId").To<std::string>(),
            member.at("previewGeometryResourceVersion").To<std::uint64_t>()));
    }
    EXPECT_TRUE(foundSolid);
}

TEST(ProductInstanceSDO, DisassemblyKeepsActiveProductRenderInstancesVisible) {
    Scene scene;
    const auto added = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"faceType", std::string("five")},
        {"accessDoorEnabled", true},
        {"accessDoorFace5", std::string("front")},
        {"receiptOnly", true},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto initialMembers = added.at("members").To<VariantArray>();
    ASSERT_FALSE(initialMembers.empty());
    const auto productID = added.at("product").To<ObjectMap>()
        .at("entityId").To<std::string>();
    const auto disassembled = invoke(scene, "DisassembleSelected", ObjectMap{
        {"productEntityIds", VariantArray{productID}},
    }).at("tubeDesigner").To<ObjectMap>();
    ASSERT_FALSE(disassembled.at("parts").To<VariantArray>().empty());
    char* shapeDumpEnv = nullptr;
    std::size_t shapeDumpEnvLength = 0;
    (void)_dupenv_s(&shapeDumpEnv, &shapeDumpEnvLength, "ICAX_DUMP_DISASSEMBLY_SHAPES");
    const bool dumpShapes = shapeDumpEnv != nullptr;
    std::free(shapeDumpEnv);
    if (dumpShapes) {
        for (const auto& value : disassembled.at("parts").To<VariantArray>()) {
            const auto partData = value.To<ObjectMap>();
            const auto key = partData.at("stableKey").To<std::string>();
            const auto resource = scene.Resources().Get<BRepModel>(
                partData.at("manufacturingGeometryResourceId").To<std::string>(),
                partData.at("manufacturingGeometryResourceVersion").To<std::uint64_t>());
            ASSERT_TRUE(resource) << key;
            const auto built = iCAX::OpenCascade::BuildOpenCascadeShape(*resource);
            ASSERT_TRUE(built.bOK) << key;
            GProp_GProps properties;
            BRepGProp::VolumeProperties(built.Shape, properties);
            GProp_GProps surface;
            BRepGProp::SurfaceProperties(built.Shape, surface);
            int faceCount = 0;
            for (TopExp_Explorer face(built.Shape, TopAbs_FACE); face.More(); face.Next())
                ++faceCount;
            Bnd_Box bounds;
            BRepBndLib::AddOptimal(built.Shape, bounds, false, false);
            double xmin, ymin, zmin, xmax, ymax, zmax;
            bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            std::cout << std::setprecision(15) << "PartGeometry key=" << key
                << " volume=" << properties.Mass() << " bounds="
                << xmin << ',' << ymin << ',' << zmin << ','
                << xmax << ',' << ymax << ',' << zmax
                << " area=" << surface.Mass()
                << " center=" << properties.CentreOfMass().X() << ','
                << properties.CentreOfMass().Y() << ','
                << properties.CentreOfMass().Z()
                << " faces=" << faceCount << '\n';
        }
    }
    ASSERT_EQ(productID, disassembled.at("activeProductId").To<std::string>());
    const auto members = disassembled.at("members").To<VariantArray>();
    ASSERT_EQ(initialMembers.size(), members.size());
    for (const auto& value : members) {
        const auto member = value.To<ObjectMap>();
        const auto memberID = iCAX::Data::uuid::from_string(
            member.at("entityId").To<std::string>());
        ASSERT_TRUE(memberID.has_value());
        const auto entity = scene.Database().GetEntity(*memberID);
        ASSERT_TRUE(entity);
        const auto render = std::dynamic_pointer_cast<
            iCAX::RenderInteraction::CRenderInstanceComponent>(
                entity->GetComponent(iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName));
        ASSERT_TRUE(render);
        EXPECT_TRUE(render->GetVisible());
        EXPECT_TRUE(scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(
            render->GetGeometryResourceID(), render->GetGeometryResourceVersion()));
    }
}

TEST(ProductInstanceSDO, SingleFaceDisassemblyProducesParts) {
    Scene scene;
    const auto added = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"faceType", std::string("single")},
        {"receiptOnly", true},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto productID = added.at("product").To<ObjectMap>()
        .at("entityId").To<std::string>();
    const auto result = invoke(scene, "DisassembleSelected", ObjectMap{
        {"productEntityIds", VariantArray{productID}},
    }).at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(productID, result.at("activeProductId").To<std::string>());
    EXPECT_FALSE(result.at("parts").To<VariantArray>().empty());
}

TEST(ProductInstanceSDO, DeleteRemovesOnlyTheActiveInstanceAndUndoRestoresIt) {
    Scene scene;
    const auto first = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"instanceName", std::string("第一樘防盗窗")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto firstId = first.at("product").To<ObjectMap>()
        .at("entityId").To<std::string>();
    const auto second = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"instanceName", std::string("第二樘防盗窗")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto secondId = second.at("product").To<ObjectMap>()
        .at("entityId").To<std::string>();
    ASSERT_NE(firstId, secondId);
    ASSERT_EQ(2u, second.at("instances").To<VariantArray>().size());

    const auto deleted = invoke(scene, "DeleteProduct", ObjectMap{
        {"productEntityId", secondId},
    });
    EXPECT_TRUE(deleted.at("deleted").To<bool>());
    EXPECT_EQ(secondId, deleted.at("deletedProductEntityId").To<std::string>());
    const auto remaining = deleted.at("tubeDesigner").To<ObjectMap>();
    ASSERT_EQ(1u, remaining.at("instances").To<VariantArray>().size());
    EXPECT_EQ(firstId, remaining.at("activeProductId").To<std::string>());
    EXPECT_EQ(firstId, remaining.at("product").To<ObjectMap>()
        .at("entityId").To<std::string>());
    const auto secondUuid = iCAX::Data::uuid::from_string(secondId);
    ASSERT_TRUE(secondUuid.has_value());
    EXPECT_FALSE(scene.Database().GetEntity(*secondUuid));

    ASSERT_TRUE(scene.Database().CanUndo());
    ASSERT_TRUE(scene.Database().Undo());
    const auto restored = invoke(scene, "List", {}).at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(2u, restored.at("instances").To<VariantArray>().size());
    EXPECT_EQ(secondId, restored.at("activeProductId").To<std::string>());
    EXPECT_TRUE(scene.Database().GetEntity(*secondUuid));
}

TEST(ProductInstanceSDO, EveryProductSeparatesManufacturingEditsFromDisplayGeometry) {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"single-face-security-window", "assemblyClearance"},
        {"modular-guardrail", "baseBoltDiameter"},
        {"modular-guardrail-cross-straight", "baseBoltDiameter"},
        {"modular-guardrail-diamond-straight", "baseBoltDiameter"},
        {"modular-guardrail-glass-straight", "baseBoltDiameter"},
        {"straight-steel-staircase", "boltHoleDiameter"},
    };
    const auto displayState = [](const ObjectMap& designer) {
        ObjectMap result;
        for (const auto& value : designer.at("members").To<VariantArray>()) {
            const auto member = value.To<ObjectMap>();
            ObjectMap state{
                {"resource", member.at("previewGeometryResourceId")},
                {"version", member.at("previewGeometryResourceVersion")},
            };
            if (member.contains("transform")) state["transform"] = member.at("transform");
            result[member.at("entityId").To<std::string>()] = state;
        }
        return result;
    };
    for (const auto& [id, independentField] : cases) {
        SCOPED_TRACE(id);
        Scene scene;
        ObjectMap request{{"templateId", id}};
        if (id == "straight-steel-staircase") {
            request["totalRiserCount"] = 4;
            request["floorHeight"] = 680.0;
            request["railingSide"] = std::string("none");
        }
        if (id.starts_with("modular-guardrail")) request["sideLength1"] = 800.0;
        const auto added = invoke(scene, "GeneratePreview", request).at("tubeDesigner").To<ObjectMap>();
        const auto originalDisplay = displayState(added);
        ASSERT_FALSE(originalDisplay.empty());
        const auto product = added.at("product").To<ObjectMap>();
        const auto productId = product.at("entityId").To<std::string>();
        auto parameters = product.at("parameters").To<ObjectMap>();
        const auto code = std::string("AUDIT-") + id;
        parameters["productCode"] = code;
        parameters[independentField] = parameters.at(independentField).To<double>() + 0.2;
        const auto updated = invoke(scene, "UpdateProductParameters", ObjectMap{
            {"productEntityId", productId}, {"parameters", parameters},
        }).at("tubeDesigner").To<ObjectMap>();
        const auto updatedProduct = updated.at("product").To<ObjectMap>();
        EXPECT_FALSE(updatedProduct.at("modelOutdated").To<bool>());
        EXPECT_TRUE(updatedProduct.at("partsOutdated").To<bool>());
        EXPECT_EQ(code, updatedProduct.at("productCode").To<std::string>());
        EXPECT_EQ(originalDisplay, displayState(updated));

        const ObjectMap disassembly{
            {"productEntityIds", VariantArray{productId}},
            {"productParametersByEntityId", ObjectMap{{productId, parameters}}},
        };

            const auto manufactured = invoke(scene, "DisassembleSelected", disassembly)
                .at("tubeDesigner").To<ObjectMap>();
            EXPECT_FALSE(manufactured.at("parts").To<VariantArray>().empty());
            EXPECT_FALSE(manufactured.at("product").To<ObjectMap>().at("partsOutdated").To<bool>());
            EXPECT_EQ(originalDisplay, displayState(manufactured));

        const auto dimension = parameters.contains("width") ? "width"
            : parameters.contains("sideLength1") ? "sideLength1" : "floorHeight";
        parameters[dimension] = parameters.at(dimension).To<double>() + 10.0;
        const auto resized = invoke(scene, "UpdateProductParameters", ObjectMap{
            {"productEntityId", productId}, {"parameters", parameters},
        }).at("tubeDesigner").To<ObjectMap>();
        EXPECT_TRUE(resized.at("product").To<ObjectMap>().at("modelOutdated").To<bool>());
        EXPECT_EQ(originalDisplay, displayState(resized));
        EXPECT_THROW(invoke(scene, "DisassembleSelected", ObjectMap{
            {"productEntityIds", VariantArray{productId}},
        }), std::exception);
        EXPECT_THROW(invoke(scene, "DisassembleSelected", ObjectMap{
            {"productEntityIds", VariantArray{productId}},
            {"productParametersByEntityId", ObjectMap{{productId, parameters}}},
        }), std::exception);
    }
}

TEST(ProductInstanceSDO, MaterialsRequireRegenerationBeforeNativeDisassembly) {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"single-face-security-window", "frameWidth"},
        {"modular-guardrail", "handrailWidth"},
        {"modular-guardrail-cross-straight", "handrailWidth"},
        {"modular-guardrail-diamond-straight", "handrailWidth"},
        {"modular-guardrail-glass-straight", "handrailWidth"},
        {"straight-steel-staircase", "stringerWidth"},
        {"assembly-cross-fixture", "hostWidth"},
        {"assembly-frame-lt", "frameWidth"},
        {"assembly-orthogonal-corner", "aWidth"},
    };
    for (const auto& [id, material] : cases) {
        SCOPED_TRACE(id);
        Scene scene;
        const auto added = invoke(scene, "GeneratePreview", ObjectMap{{"templateId", id}})
            .at("tubeDesigner").To<ObjectMap>();
        const auto product = added.at("product").To<ObjectMap>();
        const auto productId = product.at("entityId").To<std::string>();
        auto parameters = product.at("parameters").To<ObjectMap>();
        parameters[material] = parameters.at(material).To<double>() + 1.0;
        const auto updated = invoke(scene, "UpdateProductParameters", ObjectMap{
            {"productEntityId", productId}, {"parameters", parameters},
        }).at("tubeDesigner").To<ObjectMap>();
        EXPECT_TRUE(updated.at("product").To<ObjectMap>().at("modelOutdated").To<bool>());
        EXPECT_EQ(added.at("members"), updated.at("members"));
        EXPECT_THROW(invoke(scene, "DisassembleSelected", ObjectMap{
            {"productEntityIds", VariantArray{productId}},
        }), std::exception);
        EXPECT_THROW(invoke(scene, "DisassembleSelected", ObjectMap{
            {"productEntityIds", VariantArray{productId}},
            {"productParametersByEntityId", ObjectMap{{productId, parameters}}},
        }), std::exception);
        parameters["templateId"] = id;
        parameters["productEntityId"] = productId;
        const auto regenerated = invoke(scene, "GeneratePreview", parameters)
            .at("tubeDesigner").To<ObjectMap>();
        EXPECT_FALSE(regenerated.at("product").To<ObjectMap>().at("modelOutdated").To<bool>());
        EXPECT_NE(added.at("generationRun").To<ObjectMap>().at("entityId"),
            regenerated.at("generationRun").To<ObjectMap>().at("entityId"));
        const auto manufactured = invoke(scene, "DisassembleSelected", ObjectMap{
            {"productEntityIds", VariantArray{productId}},
        }).at("tubeDesigner").To<ObjectMap>();
        EXPECT_FALSE(manufactured.at("parts").To<VariantArray>().empty());
    }
}

TEST(ProductManufacturingPlanSDO, ReturnsManufacturingTablesWithoutCreatingGeometryResources) {
    Scene scene;
    const auto resourcesBefore = scene.Resources().GetManifest(true).size();
    const auto response = invoke(scene, "GetProductManufacturingPlan", ObjectMap{
        {"templateId", std::string("straight-steel-staircase")},
        {"instanceQuantity", 3ull},
    });
    const auto tables = response.at("tables").To<VariantArray>();
    ASSERT_FALSE(tables.empty());
    EXPECT_EQ(response.at("templateId").To<std::string>(), "straight-steel-staircase");
    EXPECT_EQ(response.at("instanceQuantity").To<unsigned long long>(), 3ull);
    EXPECT_GT(response.at("partCount").To<unsigned long long>(), 0ull);
    EXPECT_FALSE(response.contains("items"));
    EXPECT_FALSE(response.contains("productEntityId"));
    EXPECT_FALSE(response.contains("generationRunId"));
    EXPECT_EQ(scene.Resources().GetManifest(true).size(), resourcesBefore);
}

TEST(TubeDesignerLibrarySDO, MainSceneListAndPunchCatalogueLoad) {
    Scene scene;
    const auto start = std::chrono::steady_clock::now();
    const auto tools = invoke(scene, "GetPunchTools", {});
    const auto afterTools = std::chrono::steady_clock::now();
    const auto snapshot = invoke(scene, "List", {});
    const auto end = std::chrono::steady_clock::now();
    ASSERT_TRUE(tools.contains("tools"));
    ASSERT_TRUE(snapshot.contains("tubeDesigner"));
    const auto toolCount = tools.at("tools").To<VariantArray>().size();
    const auto templates = snapshot.at("tubeDesigner").To<ObjectMap>().at("templates").To<VariantArray>();
    ASSERT_GE(toolCount, 10u);
    ASSERT_GE(templates.size(), 30u);
    const auto toolsMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterTools - start).count();
    const auto listMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - afterTools).count();
    std::cout << "[library-sdo-timing] GetPunchTools=" << toolsMs << "ms List=" << listMs << "ms\n";
    EXPECT_LT(toolsMs, 30000);
    EXPECT_LT(listMs, 30000);
}

TEST(ProductTemplatePreviewSDO, MergedGuardrailsAndWindowKeepNormalizedParameters) {
    Scene scene;
    for (const auto* id : {"modular-guardrail-glass-straight", "modular-guardrail-cross-straight",
                           "modular-guardrail-diamond-straight", "modular-guardrail"}) {
        SCOPED_TRACE(id);
        const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string(id)},
        });
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
    }
    for (const auto* face : {"single", "two", "three", "five"}) {
        SCOPED_TRACE(face);
        const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("single-face-security-window")},
            {"parameters", ObjectMap{{"faceType", std::string(face)}}},
        });
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
        ASSERT_TRUE(response.contains("specificationAnnotations"));
        const auto annotations = response.at("specificationAnnotations").To<VariantArray>();
        EXPECT_FALSE(annotations.empty());
        EXPECT_NE(std::find_if(annotations.begin(), annotations.end(), [](const auto& value) {
            return value.Is<ObjectMap>()
                && value.To<ObjectMap>().at("parameter").To<std::string>() == "width";
        }), annotations.end());
    }
    for (const auto& [face, openingParameter, openingFace] : {
             std::tuple{"two", "accessDoorFace2", "side"},
             std::tuple{"three", "accessDoorFace3", "left"},
             std::tuple{"five", "accessDoorFace5", "right"},
             std::tuple{"five", "accessDoorFace5", "bottom"},
         }) {
        SCOPED_TRACE(face);
        SCOPED_TRACE(openingFace);
        ObjectMap parameters{{"faceType", std::string(face)}, {"accessDoorEnabled", true},
            {openingParameter, std::string(openingFace)}};
        // A side opening needs a side that is wider than the default 600 mm
        // external projection; use real, feasible face dimensions here.
        if (std::string(face) == "two") parameters["sideWidth"] = 1600.0;
        if (std::string(face) == "three") parameters["leftWidth"] = 1600.0;
        if (std::string(face) == "five") parameters["depth"] = 1600.0;
        const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("single-face-security-window")},
            {"parameters", parameters},
        });
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
        const auto returned = response.at("parameters").To<ObjectMap>();
        EXPECT_EQ(returned.at(openingParameter).To<std::string>(), openingFace);
    }
}

TEST(ProductManufacturingPlanSDO, SecurityWindowContinuousFramesUseSelectedLibraryGroove) {
    Scene scene;
    const ObjectMap grooveBinding{
        {"schema",std::string("icax.product-resource-binding")},{"schemaVersion",1},
        {"resourceKind",std::string("punch-tool")},{"role",std::string("outerFrameGroove")},
        {"selectionKey",std::string("system:v-notch-sharp")},
        {"ref",ObjectMap{{"scope",std::string("system")},{"id",std::string("v-notch-sharp")}}},
        {"parameters",ObjectMap{}},
        {"snapshot",ObjectMap{{"displayName",std::string("V槽")},{"kind",std::string("programmatic")},
            {"target",std::string("part")},{"category",std::string("slot")},
            {"targetProfileRole",std::string("frame")}}},
    };
    const auto response=invoke(scene,"GetProductManufacturingPlan",ObjectMap{
        {"templateId",std::string("single-face-security-window")},
        {"parameters",ObjectMap{{"frameLayout",std::string("four_sides")},
            {"frameJoinType",std::string("v_groove_90:tool_library")},{"accessDoorEnabled",false},
            {"tubeDesignerToolBindings",ObjectMap{{"outerFrameGroove",grooveBinding}}}}}});
    EXPECT_FALSE(response.at("tables").To<VariantArray>().empty());
    EXPECT_TRUE(response.at("parameters").To<ObjectMap>().contains("tubeDesignerToolBindings"));

    // The selector binding must also survive the native preview interface.
    // Display geometry intentionally remains a clean assembly preview while
    // the manufacturing evaluation above applies the selected cutter.
    const auto preview=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
        {"templateId",std::string("single-face-security-window")},
        {"parameters",ObjectMap{{"frameLayout",std::string("four_sides")},
            {"frameJoinType",std::string("v_groove_90:tool_library")},{"accessDoorEnabled",false},
            {"tubeDesignerToolBindings",ObjectMap{{"outerFrameGroove",grooveBinding}}}}}});
    EXPECT_FALSE(preview.at("items").To<VariantArray>().empty());
    EXPECT_TRUE(preview.at("parameters").To<ObjectMap>().contains("tubeDesignerToolBindings"));

}

TEST(ProductTemplatePreviewSDO, AllGuardrailFamiliesGenerateSlopedCorners) {
    Scene scene;
    for (const auto* id : {"modular-guardrail-glass-straight", "modular-guardrail-cross-straight",
                           "modular-guardrail-diamond-straight", "modular-guardrail"}) {
        for (const auto* mode : {"continuous", "stepped"}) {
            SCOPED_TRACE(id);
            SCOPED_TRACE(mode);
            const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
                {"templateId", std::string(id)},
                {"parameters", ObjectMap{{"pathMode", std::string(mode)}, {"layout", std::string("u")},
                    {"slopeAngle", 30.0}, {"slopeAngle2", -20.0}, {"slopeAngle3", 15.0},
                    {"sideBayCount1", 2}, {"sideBayCount2", 2}, {"sideBayCount3", 2}}},
            });
            EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
        }
    }
}

TEST(ProductTemplatePreviewSDO, SideMountsGenerateOnSlopedCorners) {
    Scene scene;
    for (const auto* id : {"modular-guardrail-glass-straight", "modular-guardrail-cross-straight",
                           "modular-guardrail-diamond-straight", "modular-guardrail"}) {
        SCOPED_TRACE(id);
        const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string(id)},
            {"parameters", ObjectMap{{"installation", std::string("side_plate")},
                {"pathMode", std::string("continuous")}, {"layout", std::string("u")},
                {"slopeAngle", 30.0}, {"slopeAngle2", -20.0}, {"slopeAngle3", 15.0}}},
        });
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
    }
}

TEST(ProductTemplatePreviewSDO, DiamondOddBaysWithMiddlePosts) {
    Scene scene;
    const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("modular-guardrail-diamond-straight")},
        {"parameters", ObjectMap{{"layout", std::string("u")}, {"largePostMode", std::string("middle")},
            {"sideBayCount1",3}, {"sideBayCount2",2}, {"sideBayCount3",2}}},
    });
    EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
}

TEST(ProductTemplatePreviewSDO, UnifiedContinuousHandrailAndHorizontalInfill) {
    Scene scene;
    for (const auto* id : {"modular-guardrail", "modular-guardrail-diamond-straight"}) {
        ObjectMap parameters{{"layout", std::string("u")}, {"pathMode",std::string("continuous")},
            {"handrailMode",std::string("continuous")}, {"startExtension",100.0}, {"finishExtension",200.0}};
        if (std::string(id) == "modular-guardrail") {
            parameters["barOrientation"] = std::string("horizontal");
            parameters["horizontalRailCount"] = 4;
        }
        const auto response = invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string(id)}, {"parameters",parameters}});
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
    }
}

TEST(ProductTemplatePreviewSDO, DISABLED_DeferredReference_DecorativeDoorSixPatterns) {
    Scene scene;
    for (const auto* pattern : {"lines","diamond","octagon","round_scene","panels","glass_lattice"}) {
        const auto response = invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("decorative-door")},
            {"parameters",ObjectMap{{"pattern",std::string(pattern)},{"columns",1},{"rows",2},{"lineCount",3}}}});
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
    }
}

TEST(ProductTemplatePreviewSDO, DISABLED_DeferredReference_DecorativeDoorVGroovePassesNativePreview) {
    Scene scene;
    const auto response = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("decorative-door")},
        {"parameters", ObjectMap{
            {"pattern", std::string("lines")}, {"lineTool", std::string("v")},
            {"vAngle", 90.0}, {"cutDepth", 6.0},
            {"columns", 1}, {"rows", 2}, {"lineCount", 3},
        }},
    });
    EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
    EXPECT_EQ(response.at("parameters").To<ObjectMap>().at("lineTool").To<std::string>(), "v");
}

TEST(ProductTemplatePreviewSDO, UnifiedStairRoutesAndCatalogue) {
    Scene scene;
    for(const auto* route:{"straight","straight_landing","l_turn","u_turn"}){
        SCOPED_TRACE(route);
        const auto response=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("straight-steel-staircase")},
            {"parameters",ObjectMap{{"stairRoute",std::string(route)},{"floorHeight",1080.0},
                {"totalRiserCount",6},{"firstFlightRiserCount",3}}}});
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
        EXPECT_EQ(response.at("parameters").To<ObjectMap>().at("stairRoute").To<std::string>(),route);
    }
    auto templates=invoke(scene,"List",{}).at("tubeDesigner").To<ObjectMap>().at("templates").To<VariantArray>();
    int count=0;
    for(const auto& t:templates){
        const auto id=t.To<ObjectMap>().at("id").To<std::string>();
        if(id=="straight-steel-staircase")++count;
        EXPECT_NE(id,"l-turn-steel-staircase");EXPECT_NE(id,"u-turn-steel-staircase");
    }
    EXPECT_EQ(count,1);
}

TEST(ProductTemplatePreviewSDO, StairManufacturingOptions) {
    Scene scene;
    for(const auto* mode:{"plate","channel","zigzag","round","oval"}){
        SCOPED_TRACE(mode);
        ObjectMap p{{"stairRoute",std::string("l_turn")},{"floorHeight",1080.0},
            {"totalRiserCount",6},{"firstFlightRiserCount",3},{"handrailConnection",std::string("continuous")}};
        const std::string m(mode);
        if(m=="plate")p["bracketType"]=std::string("plate");
        if(m=="channel")p["stringerProfileType"]=std::string("channel");
        if(m=="zigzag")p["stringerConstruction"]=std::string("zigzag");
        if(m=="round" || m=="oval"){
            p["handrailProfileType"]=m;p["postProfileType"]=m;p["infillProfileType"]=m;
        }
        const auto response=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("straight-steel-staircase")},{"parameters",p}});
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
        const auto values=response.at("parameters").To<ObjectMap>();
        for(const auto& [key,value]:p){
            SCOPED_TRACE(key);
            if(value.Is<std::string>())EXPECT_EQ(values.at(key).To<std::string>(),value.To<std::string>());
            else {
                auto number=[](const auto& v)->double{
                    if(v.template Is<double>())return v.template To<double>();
                    if(v.template Is<std::int64_t>())return static_cast<double>(v.template To<std::int64_t>());
                    return static_cast<double>(v.template To<int>());
                };
                EXPECT_DOUBLE_EQ(number(values.at(key)),number(value));
            }
        }
    }
}

TEST(ProductTemplatePreviewSDO, StainlessProfilesInHandrailFamilies) {
    Scene scene;
    for(const auto* id:{"modular-guardrail","modular-guardrail-glass-straight",
                       "modular-guardrail-cross-straight","modular-guardrail-diamond-straight"}){
        const auto response=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"parameters",ObjectMap{
                {"materialGrade",std::string("304 stainless steel")},
                {"handrailProfileType",std::string("oval")},{"handrailWidth",60.0},{"handrailDepth",30.0},
                {"postProfileType",std::string("round")},{"postWidth",38.0},
                {"railProfileType",std::string("round")},{"railWidth",25.0},
                {"infillProfileType",std::string("round")},{"infillWidth",19.0}}}});
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
    }
    const auto snapshot=invoke(scene,"List",{});
    for(const auto& item:snapshot.at("tubeDesigner").To<ObjectMap>().at("templates").To<VariantArray>())
        EXPECT_NE(item.To<ObjectMap>().at("id").To<std::string>(),"stainless-steel-guardrail");
}

TEST(ProductTemplatePreviewSDO, GuardrailBoardsKeepTranslucencyAndPlateIdentityAcrossReopen) {
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);
    const auto preview=invoke(scene,"GenerateProductTemplatePreview",{
        {"templateId",std::string("modular-guardrail-glass-straight")}});
    const auto opaque=preview.at("material").To<ObjectMap>().at("url").To<std::string>();
    std::string panelMaterial;
    int previewPanels=0;
    for(const auto& value:preview.at("items").To<VariantArray>()) {
        const auto item=value.To<ObjectMap>();
        const auto material=item.at("material").To<ObjectMap>().at("url").To<std::string>();
        if(item.at("name").To<std::string>().find("挡板")!=std::string::npos) {
            EXPECT_NE(material,opaque); panelMaterial=material; ++previewPanels;
        } else EXPECT_EQ(material,opaque);
    }
    ASSERT_EQ(previewPanels,3);
    const auto translucent=scene.Resources().Get<iCAX::Render::SRenderMaterialData>(
        scene.Resources().MakeNamedResourceURL("tube-designer/material/glass"));
    ASSERT_TRUE(translucent);
    EXPECT_EQ(translucent->nColorRGBA & 0xFFu,0x80u);
    const auto normalized=preview.at("parameters").To<ObjectMap>();
    const auto verify=[&](Scene& current,const ObjectMap& snapshot) {
        int panels=0,frames=0;
        for(const auto& value:snapshot.at("members").To<VariantArray>()) {
            const auto member=value.To<ObjectMap>();
            const auto entity=current.Database().GetEntity(*iCAX::Data::uuid::from_string(
                member.at("entityId").To<std::string>()));
            const auto render=std::dynamic_pointer_cast<iCAX::RenderInteraction::CRenderInstanceComponent>(
                entity->GetComponent("CRenderInstanceComponent"));
            ASSERT_TRUE(render);
            const auto properties=member.at("properties").To<ObjectMap>();
            if(properties.contains("displayMaterial")) {
                EXPECT_EQ(properties.at("displayMaterial").To<std::string>(),"translucent-panel");
                EXPECT_EQ(properties.at("manufacturing.partKind").To<std::string>(),"plate");
                EXPECT_EQ(properties.at("manufacturing.materialCategory").To<std::string>(),"plate");
                EXPECT_EQ(render->GetMaterialResourceID(),panelMaterial); ++panels;
            } else { EXPECT_EQ(render->GetMaterialResourceID(),opaque); ++frames; }
            const auto bytes=current.Resources().Get<iCAX::Resource::CFlatBufferResource>(
                render->GetMaterialResourceID(),render->GetMaterialResourceVersion());
            ASSERT_TRUE(bytes); EXPECT_GT(bytes->Size(),0u);
        }
        EXPECT_EQ(panels,3); EXPECT_GT(frames,0);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(snapshot.at("product").To<ObjectMap>().at("parameters")),
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(normalized));
    };
    const auto added=invoke(scene,"GeneratePreview",{{"templateId",std::string("modular-guardrail-glass-straight")}})
        .at("tubeDesigner").To<ObjectMap>();
    verify(scene,added);
    auto regenerate=normalized;
    regenerate["templateId"]=std::string("modular-guardrail-glass-straight");
    regenerate["productEntityId"]=added.at("product").To<ObjectMap>().at("entityId");
    const auto regenerated=invoke(scene,"GeneratePreview",regenerate).at("tubeDesigner").To<ObjectMap>();
    verify(scene,regenerated);
    const auto directory=std::filesystem::current_path()/"tmp/guardrail-appearance-acceptance";
    std::filesystem::create_directories(directory);
    const auto path=directory/"translucent-board.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info; info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Guardrail appearance acceptance";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false,true);
    file.Open(path,reopened.Database(),reopened.Resources());
    verify(reopened,invoke(reopened,"List",{}).at("tubeDesigner").To<ObjectMap>());
}

TEST(ProductTemplatePreviewSDO, DISABLED_DeferredReference_DoorAndWindowGlassUseTranslucentMaterial) {
    for(const auto* id:{"decorative-door","aluminium-window"}){
        Scene scene;
        SCOPED_TRACE(id);
        const auto response=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},
            {"parameters",std::string(id)=="decorative-door"
                ? ObjectMap{{"pattern",std::string("glass_lattice")},{"columns",1},{"rows",2}}
                : ObjectMap{}}});
        const auto glass=scene.Resources().Get<iCAX::Render::SRenderMaterialData>(
            scene.Resources().MakeNamedResourceURL("tube-designer/material/glass"));
        ASSERT_TRUE(glass);
        EXPECT_EQ(glass->nColorRGBA & 0xFFu,0x80u);
        const auto opaque=response.at("material").To<ObjectMap>().at("url").To<std::string>();
        int glassCount=0,opaqueCount=0;
        for(const auto& value:response.at("items").To<VariantArray>()){
            const auto item=value.To<ObjectMap>();
            const auto key=item.at("key").To<std::string>();
            const auto material=item.at("material").To<ObjectMap>().at("url").To<std::string>();
            if(key.ends_with(".glass")){EXPECT_NE(material,opaque);++glassCount;}
            else{EXPECT_EQ(material,opaque);++opaqueCount;}
        }
        EXPECT_GT(glassCount,0);EXPECT_GT(opaqueCount,0);
    }
}

TEST(ProductTemplatePreviewSDO, DISABLED_DeferredReference_AluminiumWindowPanelsAndMovableScreen) {
    Scene scene;
    for (const auto* type : {"fixed", "sliding", "hinged", "mixed"}) {
        const auto response = invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("aluminium-window")},
            {"parameters",ObjectMap{{"windowType",std::string(type)}, {"width",1800.0},
                {"height",1800.0},{"columns",2},{"rows",2}, {"hingedCount",2},{"mergeTopLight",true},
                {"cell11",std::string("sliding")},{"cell12",std::string("hinged")}}}});
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
    }
}

TEST(ProductTemplatePreviewSDO, DISABLED_DeferredReference_LouverTubeAnglesSlotsAndSupports) {
    Scene scene;
    for (const auto* connection : {"face_weld", "slot_insert", "through_insert"}) {
        const auto response = invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("louver-window")},
            {"parameters",ObjectMap{{"width",600.0},{"height",600.0},
                {"bladeDirectionAngle",30.0},{"bladeRollAngle",45.0},
                {"arrayMode",std::string("count")},{"bladeCount",4},
                {"frameJoint",std::string("miter")},
                {"middlePostCount",1},{"supportMode",std::string("through")},
                {"bladeConnection",std::string(connection)}}}});
        EXPECT_FALSE(response.at("items").To<VariantArray>().empty());
    }
}

TEST(TubeDesignerLibrarySDO, TemplateListLoadsFullDescriptorAtStartup) {
    Scene scene;
    const auto snapshot = invoke(scene, "List", {});
    const auto templates = snapshot.at("tubeDesigner").To<ObjectMap>()
        .at("templates").To<VariantArray>();
    const auto listed = std::find_if(templates.begin(), templates.end(), [](const auto& value) {
        return value.Is<ObjectMap>() && value.To<ObjectMap>().at("id").To<std::string>() == "single-face-security-window";
    });
    ASSERT_NE(listed, templates.end());
    EXPECT_TRUE(listed->To<ObjectMap>().at("descriptorLoaded").To<bool>());
    EXPECT_FALSE(listed->To<ObjectMap>().at("parameters").To<VariantArray>().empty());
    EXPECT_FALSE(listed->To<ObjectMap>().contains("descriptorJson"));

    const auto detail = invoke(scene, "GetTemplateDescriptor", ObjectMap{
        { "templateId", std::string("single-face-security-window") },
    });
    ASSERT_TRUE(detail.contains("template"));
    const auto descriptor = detail.at("template").To<ObjectMap>();
    EXPECT_TRUE(descriptor.at("descriptorLoaded").To<bool>());
    EXPECT_FALSE(descriptor.at("parameters").To<VariantArray>().empty());
    EXPECT_EQ(listed->To<ObjectMap>().at("packageDigest").To<std::string>(),
        descriptor.at("packageDigest").To<std::string>());
    const auto extensions = descriptor.at("extensions").To<ObjectMap>();
    ASSERT_TRUE(extensions.contains("productDiagram"));
    const auto productDiagram = extensions.at("productDiagram").To<ObjectMap>();
    ASSERT_TRUE(productDiagram.contains("sceneBindings"));
    ASSERT_TRUE(productDiagram.contains("profileRoles"));
    const auto sceneBindings = productDiagram.at("sceneBindings").To<ObjectMap>();
    const auto profileRoles = productDiagram.at("profileRoles").To<ObjectMap>();
    EXPECT_TRUE(sceneBindings.contains("verticalMaximumCenterSpacing"));
    ASSERT_TRUE(profileRoles.contains("vertical"));
    const auto vertical = profileRoles.at("vertical").To<ObjectMap>();
    const auto verticalParameters = vertical.at("parameters").To<VariantArray>();
    EXPECT_NE(std::find_if(verticalParameters.begin(), verticalParameters.end(), [](const auto& value) {
        return value.Is<std::string>() && value.To<std::string>() == "verticalWidth";
    }), verticalParameters.end());
    ASSERT_TRUE(descriptor.contains("display"));
    const auto display = descriptor.at("display").To<ObjectMap>();
    EXPECT_EQ(display.at("schema").To<std::string>(), "icax.template-display");
    const auto views = display.at("views").To<ObjectMap>();
    const auto right = views.at("right").To<ObjectMap>();
    const auto sceneView = views.at("scene").To<ObjectMap>();
    const auto annotations = sceneView.at("annotations").To<ObjectMap>();
    EXPECT_TRUE(annotations.contains("width"));
    EXPECT_TRUE(annotations.contains("horizontalMaximumCenterSpacing"));
    EXPECT_TRUE(annotations.contains("doorVerticalMaximumCenterSpacing"));
    const auto fields = right.at("fields").To<ObjectMap>();
    const auto productCode = fields.at("productCode").To<ObjectMap>();
    EXPECT_EQ(productCode.at("line").To<std::string>(), "full");
    const auto width = productCode.at("width").To<ObjectMap>();
    EXPECT_TRUE(width.contains("min"));
    EXPECT_TRUE(width.contains("preferred"));
    EXPECT_TRUE(width.contains("max"));
}
TEST(AssemblyTemplateSDOTest, TContactFitMarkProducesShallowNativeContourOnlyWhenEnabled)
{
    Scene scene;
    const auto planFor=[&](bool enabled) {
        return invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string("t-contact-fit")},
            {"parameters",ObjectMap{{"markContact",enabled}}},
            {"processDrafts",ObjectMap{}},
        });
    };
    const auto plain=planFor(false), marked=planFor(true);
    ASSERT_EQ(plain.at("designParts").To<VariantArray>().size(),2u);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plain.at("designParts")),
              iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(marked.at("designParts")));
    const auto hostRequest=[](const ObjectMap& plan) {
        for(const auto& item:plan.at("manufacturingParts").To<VariantArray>()) {
            const auto part=item.To<ObjectMap>();
            if(part.at("sourceRole").To<std::string>()=="host")return part.at("request").To<ObjectMap>();
        }
        throw std::runtime_error("T contact host blank missing");
    };
    const auto unmarkedRequest=hostRequest(plain), markedRequest=hostRequest(marked);
    EXPECT_TRUE(unmarkedRequest.at("features").To<VariantArray>().empty());
    const auto markedFeatures=markedRequest.at("features").To<VariantArray>();
    ASSERT_EQ(markedFeatures.size(),1u);
    const auto feature=markedFeatures.front().To<ObjectMap>();
    EXPECT_EQ(feature.at("toolRef").To<ObjectMap>().at("id").To<std::string>(),"contact-outline-mark");
    EXPECT_EQ(feature.at("face").To<std::string>(),"top");
    EXPECT_TRUE(feature.at("blindHole").To<bool>());
    EXPECT_NEAR(feature.at("cutDepth").To<double>(),0.2,1e-9);
    const auto previewFor=[&](const ObjectMap& request,const std::string& key,bool plainBlank) {
        const auto preview=previewAssemblyManufacturingPart(scene,request,key);
        if(preview.contains("resultError"))
            throw std::runtime_error(preview.at("resultError").To<std::string>());
        if(!preview.at("previewComputed").To<bool>() || !preview.at("resultValid").To<bool>())
            throw std::runtime_error("T contact preview did not produce valid manufacturing material");
        // A no-tool assembly blank reuses its base BRep instead of storing a
        // second manufactured resource at the plain preview key.
        EXPECT_EQ(preview.at("geometry")==preview.at("baseGeometry"),plainBlank);
        const auto model=scene.Resources().Get<BRepModel>(scene.Resources().MakeNamedResourceURL(
            "tube-designer/punch-preview/assembly/"+key+(plainBlank?"/base":"")));
        if(!model)throw std::runtime_error("T contact manufacturing BRep missing");
        const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*model);
        if(!rebuilt.bOK || rebuilt.Shape.IsNull() || !BRepCheck_Analyzer(rebuilt.Shape).IsValid())
            throw std::runtime_error("T contact manufacturing BRep invalid");
        GProp_GProps properties;
        BRepGProp::VolumeProperties(rebuilt.Shape,properties);
        return std::make_pair(properties.Mass(),rebuilt.Shape);
    };
    const auto unmarked=previewFor(unmarkedRequest,"t-contact-unmarked",true);
    const auto cut=previewFor(markedRequest,"t-contact-marked",false);
    EXPECT_GT(unmarked.first-cut.first,5.0);
    EXPECT_LT(unmarked.first-cut.first,100.0);
    const auto materialAt=[&](const TopoDS_Shape& shape,double x,double y,double z) {
        return BRepClass3d_SolidClassifier(shape,gp_Pnt(x,y,z),1e-5).State();
    };
    bool shallowContourFound=false;
    for(double x : {200.25,239.75}) {
        if(shallowContourFound)break;
        for(double y=-19.75;y<=19.75;y+=0.5) {
            if(materialAt(unmarked.second,x,y,19.9)==TopAbs_IN
               && materialAt(cut.second,x,y,19.9)==TopAbs_OUT
               && materialAt(cut.second,x,y,19.5)==TopAbs_IN) {
                shallowContourFound=true;
                break;
            }
        }
    }
    EXPECT_TRUE(shallowContourFound) << "mark must remove material only near the host contact face";
    EXPECT_EQ(materialAt(cut.second,220,0,19.9),TopAbs_IN);
}

}

namespace punch_persistence_acceptance {
#include "ProductDisassemblyProgressSDOTests.inc"
#include "NestingProfileKeyExportSDOTests.inc"
#include "FinishedProductInputSDOTests.inc"
#include "ProductDisassemblyCountSDOTests.inc"
#include "ProductAssemblyConnectionsTests.inc"
#include "AssemblyBindingPersistenceTests.inc"
#include "ProductAssemblyBindingSDOTests.inc"
#include "ProductAssemblyCrossThroughSDOTests.inc"
#include "ProductAssemblyFlatContactSDOTests.inc"
#include "ProductAssemblyObliqueInsertSDOTests.inc"
#include "ProductAssemblyWholeAuditSDOTests.inc"
#include "ProductAssemblyOrthogonalCornerSDOTests.inc"
#include "AssemblyProcessPlanSDOTests.inc"
#include "ProductGeneratedAssemblyProcessSDOTests.inc"
#include "ProductDisassemblyOwnedModelTests.inc"
#include "ProductIdenticalManufacturingPartsSDOTests.inc"
#include "ProductExportUnitQuantitySDOTests.inc"
#include "ProductManufacturingConcurrencySDOTests.inc"
#include "NestingPartFilePickerSDOTests.inc"
#include "NestingSideSketchSDOTests.inc"
}
