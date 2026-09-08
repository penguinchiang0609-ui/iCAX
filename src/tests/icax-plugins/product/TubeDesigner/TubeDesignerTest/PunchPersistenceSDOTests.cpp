#include "pch.h"
#include <ApplicationContext/IApplicationContext.h>
#include <Data/VariantSerializer.h>
#include <Database/IRepository.h>
#include <Database/MetaRegistrationCatalog.h>
#include <GeometryData/BRepPersistence.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <ProjectContext/ISceneContext.h>
#include <ProjectFile/ProjectFile.h>
#include <Resources/ResourceLibrary.h>
#include <Resources/FlatBufferResource.h>
#include <SDO/SDORegistrationCatalog.h>
#include <TemplateRuntime/PythonTemplateHost.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <TubeDesigner/TubeDesigner.h>
#include <TubeDesigner/TubeDesignerComponents.h>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <filesystem>
#include <fstream>
#include <iostream>

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
    Scene(iCAX::Data::uuid projectId=iCAX::Data::GenerateNewUUID(),iCAX::Data::uuid sceneId=iCAX::Data::GenerateNewUUID(),bool root=true)
        : project(projectId),id(sceneId) {
        (void)iCAX::TubeDesigner::GetTubeDesignerContractVersion();
        auto registry=iCAX::Database::CreateMetaRegistry();
        wchar_t exe[32768]{},dbModule[32768]{};
        GetModuleFileNameW(nullptr,exe,32768);GetModuleFileNameW(GetModuleHandleW(L"Database.dll"),dbModule,32768);
        iCAX::Database::CMetaRegistrationCatalog::ReplayByModulePaths(*registry,
            {std::filesystem::path(dbModule).string(),std::filesystem::path(exe).string()});
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

ObjectMap invoke(Scene& scene,const std::string& method,const ObjectMap& payload,iCAX::Product::IProductContext* product=nullptr) {
    Application application;iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(registry);
    iCAX::Interaction::CInvocation request;request.nCallID=1;
    request.Method=iCAX::Interaction::MakeSDOMethod("TubeDesigner",method);
    const auto json=iCAX::Data::VariantSerializer::Serialize(payload);request.Payload.assign(json.begin(),json.end());
    const auto sdo=registry.Find(request.Method.nSDOCode);
    if(!sdo||!sdo->HasMethod(request.Method.nMethodCode))throw std::runtime_error("Missing real SDO: "+method);
    if(logInvocations)std::cout<<"[persistence-sdo] "<<method<<std::endl;
    const auto result=sdo->Invoke(request,application,product,nullptr,&scene);
    if(!result.IsOK())throw std::runtime_error(method+": "+result.strError);
    return iCAX::Data::VariantSerializer::Deserialize(std::string(result.Payload.begin(),result.Payload.end())).To<ObjectMap>();
}
ObjectMap runtime(const std::string& file,const ObjectMap& input) {
    const auto root=std::filesystem::current_path();
    const auto worker=root/"src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py";
    iCAX::TemplateRuntime::CPythonTemplateHost host({root/"src/x64/Debug/runtime/python/python312.dll",worker,worker.parent_path()});
    return host.Invoke({{"protocol",std::string("icax.template-runtime")},{"protocolVersion",1ull},{"operation",std::string("evaluate")},
        {"templatePath",(root/"src/apps/tube-designer/templates/_shared"/file).string()},
        {"template",ObjectMap{{"id",std::string("persistence-test")},{"version",std::string("1.0.0")},{"packageDigest",std::string("test")}}},
        {"parameters",input},{"context",ObjectMap{}}});
}
ObjectMap systemProfile(const std::string& id,ObjectMap values={}) {
    return runtime("profile_package_runtime.py",{{"action",std::string("evaluate-system")},{"systemProfileId",id},{"values",values},
        {"profileRoot",(std::filesystem::current_path()/"src/apps/tube-designer/templates/_shared/profiles").string()}}).at("profile").To<ObjectMap>();
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

TEST(PunchPreviewDiagnosticsSDO, FullWidthPolygonBranchesDisplayCurrentToolsButCannotBeCreatedOrApplied) {
    const auto polygon=[](double size){return systemProfile("polygon",{{"shapeMode",std::string("regular")},{"sideCount",8},
        {"width",size},{"depth",size},{"wallThickness",2.}});};
    for(const auto& mother:std::vector<std::string>{"round","rect","polygon"}) {
        SCOPED_TRACE(mother);Scene scene;
        auto profile=mother=="polygon"?polygon(40):systemProfile(mother,mother=="round"?ObjectMap{}:
            ObjectMap{{"width",40.},{"depth",40.},{"wallThickness",2.},{"cornerRadius",2.}});
        ObjectMap feature{{"id",std::string("current-polygon")},{"toolTarget",std::string("part")},{"recordKind",std::string("branch")},
            {"toolRef",ObjectMap{{"id",std::string("branch-profile")}}},{"toolParameters",ObjectMap{{"angle",90.},{"azimuth",0.},
                {"direction",std::string("through")},{"cutRegion",std::string("outer")}}},{"section",section(polygon(20))},
            {"reference",std::string("center")},{"layoutReference",std::string("center")},{"station",100.},{"face",std::string("round")},
            {"layoutDatum",std::string("base")},{"arrayCount",2ull},{"arrayPitch",50.},{"arrayOffsets",VariantArray{0.,50.}},
            {"rowCount",1ull},{"rowOffsets",VariantArray{0.}}};
        ObjectMap payload{{"name",std::string("诊断隔离测试")},{"quantity",1ull},{"drawing",ObjectMap{{"length",1000.},{"section",section(profile)}}},
            {"features",VariantArray{feature}},{"ends",ObjectMap{}},{"diagnosticBooleanPreview",true}};
        const auto valid=invoke(scene,"PreviewPunchWizard",payload);ASSERT_TRUE(valid.at("resultValid").To<bool>());ASSERT_TRUE(valid.contains("geometry"));
        auto blank=payload;blank["features"]=VariantArray{};
        const auto id=invoke(scene,"AddNestingPunchPart",blank).at("partEntityId").To<std::string>();
        const auto initialRecipe=recipe(part(scene,id));const auto initialBytes=persistedShapeBytes(scene,part(scene,id));
        feature["section"]=section(polygon(40));payload["features"]=VariantArray{feature};
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

TEST(PunchPreviewDiagnosticsSDO, DisconnectedEndCutStillReceivesSubsequentHole) {
    Scene scene;const auto branch=systemProfile("round",{{"width",20.},{"wallThickness",2.}});
    const ObjectMap feature{{"id",std::string("after-end")},{"type",std::string("circle")},{"diameter",10.},
        {"station",700.},{"face",std::string("top")},{"reference",std::string("start")},{"layoutDatum",std::string("base")}};
    auto payload=request("多段端切后继续加工","round",{{"start",end("end-profile",{{"cutRegion",std::string("material")}},0,section(branch))}},VariantArray{feature});
    payload["diagnosticBooleanPreview"]=true;
    const auto preview=invoke(scene,"PreviewPunchWizard",payload);
    ASSERT_TRUE(preview.at("previewComputed").To<bool>());EXPECT_FALSE(preview.at("resultValid").To<bool>());
    EXPECT_GT(preview.at("solidCount").To<unsigned long long>(),1ull);EXPECT_TRUE(preview.at("previewToolsComplete").To<bool>());
    EXPECT_EQ(preview.at("appliedPunchToolCount").To<unsigned long long>(),1ull);EXPECT_TRUE(preview.contains("geometry"));
    const auto model=scene.Resources().Get<BRepModel>(scene.Resources().MakeNamedResourceURL("tube-designer/punch-preview"));
    ASSERT_TRUE(model);const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*model);ASSERT_TRUE(rebuilt.bOK);
    EXPECT_EQ(BRepClass3d_SolidClassifier(rebuilt.Shape,gp_Pnt(700,0,19),1e-6).State(),TopAbs_OUT);
    EXPECT_EQ(BRepClass3d_SolidClassifier(rebuilt.Shape,gp_Pnt(680,0,19),1e-6).State(),TopAbs_IN);
    EXPECT_THROW(invoke(scene,"AddNestingPunchPart",payload),std::exception);
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

TEST(PartDrawingIndependentSDO, VNotchSevenStylesHaveRealCurvesAndPreserveTheSpecifiedBridge) {
    VariantArray evidence;
    for(const auto* style:{"sharp_v","asymmetric_v","rounded_v","left_arc","right_arc","flat_v","relief_v"}) {
        SCOPED_TRACE(style);Scene scene;ObjectMap feature{{"id",std::string("v-")+style},{"toolTarget",std::string("part")},
            {"toolRef",ObjectMap{{"id",std::string("v-notch")}}},{"toolParameters",ObjectMap{{"style",std::string(style)},{"bridge",1.}}},
            {"station",500.},{"reference",std::string("start")}};
        const auto payload=request(std::string("V槽真实几何-")+style,"rect",{},VariantArray{feature});
        const auto preview=invoke(scene,"PreviewPartDrawing",payload);ASSERT_TRUE(preview.at("toolsOnly").To<bool>());
        const auto toolModel=scene.Resources().Get<BRepModel>(scene.Resources().MakeNamedResourceURL(std::string("tube-designer/part-drawing-preview/tool/side/v-")+style));
        ASSERT_TRUE(toolModel);const auto tool=iCAX::OpenCascade::BuildOpenCascadeShape(*toolModel);ASSERT_TRUE(tool.bOK);
        std::size_t circleEdges=0;for(TopExp_Explorer e(tool.Shape,TopAbs_EDGE);e.More();e.Next())if(BRepAdaptor_Curve(TopoDS::Edge(e.Current())).GetType()==GeomAbs_Circle)++circleEdges;
        const bool curved=std::string(style)=="rounded_v"||std::string(style)=="left_arc"||std::string(style)=="right_arc"||std::string(style)=="relief_v";
        EXPECT_EQ(circleEdges>0,curved);
        const auto id=invoke(scene,"AddPartDrawing",payload).at("partEntityId").To<std::string>();const auto result=shape(scene,part(scene,id));
        ASSERT_TRUE(BRepCheck_Analyzer(result).IsValid());std::size_t solids=0;for(TopExp_Explorer e(result,TopAbs_SOLID);e.More();e.Next())++solids;EXPECT_EQ(solids,1u);
        const auto material=[&](double x,double y,double z){return BRepClass3d_SolidClassifier(result,gp_Pnt(x,y,z),1e-6).State()==TopAbs_IN;};
        EXPECT_TRUE(material(500,0,-9.5));EXPECT_FALSE(material(500,0,9));EXPECT_TRUE(material(450,0,9));
        if(std::string(style)=="left_arc"){EXPECT_FALSE(material(499,19,-8.5));EXPECT_TRUE(material(501,19,-8.5));}
        if(std::string(style)=="right_arc"){EXPECT_TRUE(material(499,19,-8.5));EXPECT_FALSE(material(501,19,-8.5));}
        if(std::string(style)=="asymmetric_v"){EXPECT_TRUE(material(490,19,0));EXPECT_FALSE(material(510,19,0));}
        if(std::string(style)=="flat_v")EXPECT_FALSE(material(500.5,19,-8.8));
        if(std::string(style)=="relief_v")EXPECT_FALSE(material(501,19,-8.5));
        evidence.emplace_back(ObjectMap{{"style",std::string(style)},{"resultValid",true},{"solidCount",static_cast<unsigned long long>(solids)},
            {"analyticCircleEdgesInTool",static_cast<unsigned long long>(circleEdges)},{"volume",volume(result)},{"bottomBridgePointRetained",material(500,0,-9.5)}});
    }
    Scene scene;const ObjectMap feature{{"id",std::string("bottom-cut")},{"toolTarget",std::string("part")},{"station",500.},
        {"toolRef",ObjectMap{{"id",std::string("v-notch")}}},{"toolParameters",ObjectMap{{"style",std::string("sharp_v")},{"bottomCut",true},{"bottomCutWidth",2.}}}};
    const auto payload=request("V槽主动切断","rect",{},VariantArray{feature});const auto id=invoke(scene,"AddPartDrawing",payload).at("partEntityId").To<std::string>();
    std::size_t solids=0;for(TopExp_Explorer e(shape(scene,part(scene,id)),TopAbs_SOLID);e.More();e.Next())++solids;EXPECT_EQ(solids,2u);
    EXPECT_THROW(invoke(scene,"AddNestingPunchPart",payload),std::exception);
    std::ofstream report(std::filesystem::current_path()/"tmp/native-layout-tests/v-notch-seven-native-evidence.json");report<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(evidence);
}
}
