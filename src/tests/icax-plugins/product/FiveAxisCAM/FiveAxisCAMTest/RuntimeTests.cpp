#include "pch.h"
#include <ApplicationContext/ApplicationContext.h>
#include <ApplicationContext/UserDataStore.h>
#include <Product/ProductManifest.h>
#include <Product/ProductRuntime.h>
#include <Data/VariantSerializer.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <STEPControl_Writer.hxx>
#include <Windows.h>

namespace
{
    using Map=iCAX::Data::ObjectMap;
    class MemoryData final : public iCAX::Product::IProductDataStore
    {
    public:
        iCAX::Product::CProductData Load(const std::string&) const override { return {}; }
        void Save(const std::string&,const iCAX::Product::CProductData&) const override {}
    };
    Map Call(const iCAX::Interaction::CSDOEndpoint& endpoint,const std::string& group,const std::string& method,const Map& payload={})
    {
        static std::uint64_t sequence=0;
        iCAX::Interaction::CSDOFrame frame;
        frame.nCallID=++sequence;frame.nMethodCode=iCAX::Interaction::MakeSDOMethodCode(group.c_str(),method.c_str());
        frame.nKind=iCAX::Interaction::ESDOFrameKind::Request;
        const auto text=iCAX::Data::VariantSerializer::Serialize(payload);frame.Payload.assign(text.begin(),text.end());
        endpoint.Send(frame);
        for(int i=0;i<1000;++i)
        {
            for(const auto& response:endpoint.Receive()) if(response.nCallID==sequence&&response.nKind==iCAX::Interaction::ESDOFrameKind::Response)
            {
                if(response.nStatus!=iCAX::Interaction::EInvocationStatus::Ok) throw std::runtime_error("Runtime SDO failed: "+group+"."+method);
                return iCAX::Data::VariantSerializer::Deserialize(std::string(response.Payload.begin(),response.Payload.end())).To<Map>();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("Runtime SDO response timeout: "+group+"."+method);
    }
}
TEST(FiveAxisRuntime, RealManifestBootstrapsAndBehaviourPublishesImportedWorkpiece)
{
    auto src=std::filesystem::absolute(__FILE__).parent_path();
    while(!std::filesystem::is_directory(src/"apps")&&src!=src.root_path())src=src.parent_path();
    auto manifest=iCAX::Product::LoadProductManifest((src/"apps/five-axis-cam/product.manifest.json").string());
    // Use the modules built with this test executable, including isolated builds.
    // The source manifest's default output directory can contain an older product.
    wchar_t executable[32768]{};
    const auto length=GetModuleFileNameW(nullptr,executable,32768);
    ASSERT_GT(length,0u);ASSERT_LT(length,32768u);
    const auto binaryDirectory=std::filesystem::path(executable).parent_path();
    const auto localModule=[&](std::string& module) {
        if(module.empty())return;
        const auto path=binaryDirectory/std::filesystem::path(module).filename();
        ASSERT_TRUE(std::filesystem::is_regular_file(path))<<path;
        module=path.string();
    };
    auto& modules=manifest.Definition.Modules;
    for(auto* list:{&modules.DependencyModules,&modules.ComponentModules,&modules.BehaviourModules,&modules.ServiceModules,&modules.SDOModules})
        for(auto& module:*list)localModule(module);
    for(auto& group:modules.ModuleGroups)
        for(auto* module:{&group.strComponentPath,&group.strBehaviourPath,&group.strServicePath,&group.strSDOPath})localModule(*module);
    for(auto& handler:manifest.Definition.ResourceHandlers)localModule(handler.ModulePath);
    iCAX::Application::CApplicationDescriptor descriptor;descriptor.AppID="five-axis-runtime-test";descriptor.AppName="Five axis test";
    iCAX::Application::CApplicationPaths paths;paths.InstallDirectory=src.string();
    const auto app=std::make_shared<iCAX::Application::CApplicationContext>(descriptor,paths,iCAX::Data::PropertyBag{});
    const auto runtime=std::make_shared<iCAX::Product::CProductRuntime>(manifest.Definition,app,
        std::make_shared<iCAX::Interaction::CSDOChannelRegistry>(),
        std::make_shared<iCAX::Application::CProductUserDataStore>(std::make_shared<iCAX::Application::CSqliteUserDataStore>(":memory:"),manifest.Definition.ProductID),
        std::make_shared<MemoryData>());
    runtime->Start();
    auto catalog=runtime->OpenProjectCatalog("Five axis test",{},"Five axis test",{});
    const auto project=catalog->GetMainProject();
    const auto endpoint=runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(),project->GetMainSceneID());
    const auto state=Call(endpoint,"CamScene","ReadState");
    ASSERT_FALSE(state.at("activeSetupId").To<std::string>().empty());
    EXPECT_FALSE(runtime->GetMetaRegistry().HasTypeByName("CWorkpieceComponent"));
    auto source=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".step");
    STEPControl_Writer writer;ASSERT_EQ(IFSelect_RetDone,writer.Transfer(BRepPrimAPI_MakeBox(10,20,30).Shape(),STEPControl_AsIs));
    {std::ofstream stream(source,std::ios::binary);ASSERT_EQ(IFSelect_RetDone,writer.WriteStream(stream));}
    const auto utf8=source.u8string();
    const auto task=Call(endpoint,"CamWorkpiece","Import",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
        {"expected",state.at("expected")},{"setupId",state.at("activeSetupId")},{"sourcePath",std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size())}});
    bool published=false;
    for(int i=0;i<1000;++i)
    {
        // ReadState never pumps tasks: only the real Scene Behaviour can publish.
        const auto read=Call(endpoint,"CamScene","ReadState");
        if(!read.at("workpieces").To<std::vector<iCAX::Data::Variant>>().empty()){published=true;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(published);
    runtime->Stop();
    std::error_code error;std::filesystem::remove(source,error);
}

TEST(FiveAxisRuntime, DISABLED_OpenSavedProjectTimingProbe)
{
    char projectPath[32768]{};ASSERT_GT(GetEnvironmentVariableA("ICAX_OPEN_PROBE_PATH",projectPath,32768),0u);
    auto src=std::filesystem::absolute(__FILE__).parent_path();while(!std::filesystem::is_directory(src/"apps")&&src!=src.root_path())src=src.parent_path();
    auto manifest=iCAX::Product::LoadProductManifest((src/"apps/five-axis-cam/product.manifest.json").string());
    wchar_t executable[32768]{};ASSERT_GT(GetModuleFileNameW(nullptr,executable,32768),0u);const auto binaries=std::filesystem::path(executable).parent_path();
    const auto local=[&](std::string& module){if(!module.empty())module=(binaries/std::filesystem::path(module).filename()).string();};auto& modules=manifest.Definition.Modules;
    for(auto* list:{&modules.DependencyModules,&modules.ComponentModules,&modules.BehaviourModules,&modules.ServiceModules,&modules.SDOModules})for(auto& module:*list)local(module);
    for(auto& group:modules.ModuleGroups)for(auto* module:{&group.strComponentPath,&group.strBehaviourPath,&group.strServicePath,&group.strSDOPath})local(*module);
    for(auto& handler:manifest.Definition.ResourceHandlers)local(handler.ModulePath);
    iCAX::Application::CApplicationDescriptor descriptor;descriptor.AppID="background-file-open-probe";descriptor.AppName="Background file open probe";iCAX::Application::CApplicationPaths paths;paths.InstallDirectory=src.string();
    auto app=std::make_shared<iCAX::Application::CApplicationContext>(descriptor,paths,iCAX::Data::PropertyBag{});
    auto runtime=std::make_shared<iCAX::Product::CProductRuntime>(manifest.Definition,app,std::make_shared<iCAX::Interaction::CSDOChannelRegistry>(),std::make_shared<iCAX::Application::CProductUserDataStore>(std::make_shared<iCAX::Application::CSqliteUserDataStore>(":memory:"),manifest.Definition.ProductID),std::make_shared<MemoryData>());
    runtime->Start();const auto started=std::chrono::steady_clock::now();std::cout<<"OPEN_PROBE begin "<<projectPath<<std::endl;
    auto catalog=runtime->OpenProjectFile(projectPath);const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();std::cout<<"OPEN_PROBE opened seconds="<<seconds<<std::endl;RecordProperty("openSeconds",std::to_string(seconds));
    const auto project=catalog->GetMainProject();const auto endpoint=runtime->GetSceneFrontendSDOEndpoint(project->GetProjectID(),project->GetMainSceneID());const auto state=Call(endpoint,"CamScene","ReadState");
    EXPECT_FALSE(state.at("workpieces").To<std::vector<iCAX::Data::Variant>>().empty());EXPECT_FALSE(state.at("fixtures").To<std::vector<iCAX::Data::Variant>>().empty());
    std::cout<<"OPEN_PROBE read totalSeconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<std::endl;runtime->Stop();
}

