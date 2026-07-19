#include "pch.h"


#include <ApplicationContext/ApplicationContext.h>
#include <Behaviour/IBehaviourRegistry.h>
#include <Facades/FacadeMethod.h>
#include <Facades/Facade.h>
#include <Database/IMetaRegistry.h>
#include <Product/ProductFacades.h>
#include <Product/ProductManifest.h>
#include <Product/ProductRuntime.h>
#include <Project/ProjectRuntime.h>
#include <Resources/ResourceLoaderRegistry.h>
#include <Facades/FacadeChannelRegistry.h>
#include <Services/ServiceProvider.h>
#include <Data/Variant.h>
#include <Facades/FacadeFrame.h>
#include <Facades/FacadePayload.h>


using namespace iCAX::Product;

namespace
{
    class CTestProductFacade final : public iCAX::Interaction::CFacade
    {
    public:
        explicit CTestProductFacade(IN std::string strFacadeName_)
            : CFacade(std::move(strFacadeName_))
        {
        }

        using CFacade::ExposeMethod;
    };

    class CMemoryProductDataStore final : public IProductDataStore
    {
    public:
        CProductData Load(IN const std::string& strProductID_) const override
        {
            auto _Iter = m_Data.find(strProductID_);
            if (_Iter == m_Data.end())
            {
                return CProductData();
            }
            return _Iter->second;
        }

        void Save(IN const std::string& strProductID_, IN const CProductData& Data_) const override
        {
            m_Data[strProductID_] = Data_;
        }

    private:
        mutable std::map<std::string, CProductData> m_Data;
    };

    void ClearFrames(IN OUT std::vector<iCAX::Interaction::CFacadeFrame>& Frames_) noexcept
    {
        Frames_.clear();
    }

    iCAX::Interaction::CFacadeFrame MakeRequestFrame(
        IN uint64_t nCallID_,
        IN uint64_t nMethodCode_,
        IN const std::optional<iCAX::Data::Variant>& Payload_ = std::nullopt)
    {
        iCAX::Interaction::CFacadeFrame _Frame;
        _Frame.nCallID = nCallID_;
        _Frame.nMethodCode = nMethodCode_;
        _Frame.nKind = iCAX::Interaction::EFacadeFrameKind::Request;

        if (Payload_)
        {
            _Frame.Payload = EncodeProductPayload(*Payload_);
        }

        return _Frame;
    }

    std::vector<iCAX::Interaction::CFacadeFrame> WaitForFrames(IN const iCAX::Interaction::CFacadeEndpoint& Endpoint_)
    {
        for (int _Index = 0; _Index < 200; ++_Index)
        {
            auto _Frames = Endpoint_.Receive();
            if (!_Frames.empty())
            {
                return _Frames;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return {};
    }

    std::vector<iCAX::Interaction::CFacadeFrame> WaitForProductFrames(
        IN const std::shared_ptr<CProductRuntime>& pRuntime_,
        IN const iCAX::Interaction::CFacadeEndpoint& Endpoint_)
    {
        for (int _Index = 0; _Index < 200; ++_Index)
        {
            pRuntime_->DispatchProductFacadeFrames();
            auto _Frames = Endpoint_.Receive();
            if (!_Frames.empty())
            {
                return _Frames;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return {};
    }

    iCAX::Data::ObjectMap DecodeObjectPayload(IN const iCAX::Interaction::CFacadeFrame& Frame_)
    {
        auto _Variant = DecodeProductPayload(Frame_.Payload);
        if (!_Variant.Is<iCAX::Data::ObjectMap>())
        {
            throw std::runtime_error("Expected object payload");
        }
        return _Variant.To<iCAX::Data::ObjectMap>();
    }

    std::shared_ptr<CProductRuntime> MakeRuntime(
        IN const std::string& strProductID_ = "robot",
        IN std::shared_ptr<IProductDataStore> pProductDataStore_ = nullptr)
    {
        iCAX::Application::CApplicationDescriptor _Descriptor;
        _Descriptor.AppID = "icax-test";
        _Descriptor.AppName = "iCAX Test";

        iCAX::Application::CApplicationPaths _Paths;
        _Paths.InstallDirectory = ".";

        iCAX::Data::PropertyBag _Settings;
        auto _pContext = std::make_shared<iCAX::Application::CApplicationContext>(_Descriptor, _Paths, _Settings);
        auto _pFacadeChannelRegistry = std::make_shared<iCAX::Interaction::CFacadeChannelRegistry>();

        CProductDefinition _Definition;
        _Definition.ProductID = strProductID_;
        _Definition.ProductName = "Robot Product";
        _Definition.ProductVersion = "1.0";
        _Definition.FrontendEntry = "h5://robot/index.html";
        _Definition.ProjectFile.Magic = "ICAX_ROBOT";
        _Definition.ProjectFile.FormatVersion = "1.0";
        _Definition.ProjectFile.FileExtensions.push_back(".robot");

        auto _pProductDataStore = pProductDataStore_
            ? std::move(pProductDataStore_)
            : std::make_shared<CMemoryProductDataStore>();
        CProductData _ProductData;
        _ProductData.RecentProjects.push_back(CRecentProjectItem{
            "D:/projects/recent-robot.robot",
            "Recent Robot",
            "2026-06-20T00:00:00Z" });
        _pProductDataStore->Save(strProductID_, _ProductData);

        return std::make_shared<CProductRuntime>(
            _Definition,
            _pContext,
            _pFacadeChannelRegistry,
            _pProductDataStore);
    }

    std::filesystem::path MakeTempProjectPath()
    {
        auto _Path = std::filesystem::temp_directory_path()
            / ("icax-product-" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()) + ".robot");
        std::ofstream(_Path.string(), std::ios::binary).close();
        return _Path;
    }

    void RemoveProjectFiles(IN const std::filesystem::path& ProjectPath_)
    {
        std::error_code _Error;
        std::filesystem::remove(ProjectPath_, _Error);
        std::filesystem::remove(ProjectPath_.string() + ".log", _Error);
    }

    iCAX::Coroutines::CCoroutine<std::thread::id> ResumeThreadAfterNextProductFrame()
    {
        co_await iCAX::Coroutines::NextFrame();
        co_return std::this_thread::get_id();
    }

    iCAX::Coroutines::CCoroutine<> RunUntilProductStops(std::atomic_int& ResumeCount_)
    {
        while (true)
        {
            ++ResumeCount_;
            co_await iCAX::Coroutines::NextFrame();
        }
    }
}

TEST(ProductDataStoreTest, SavesAndLoadsProductDataPerProduct)
{
    auto _Root = std::filesystem::current_path()
        / "Temp"
        / ("ProductDataStoreTest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(_Root);

    CFileProductDataStore _Store(_Root.string());

    CProductData _ProductData;
    _ProductData.RecentProjects.push_back(CRecentProjectItem{
        "D:/projects/robot.robot",
        "Robot Cell",
        "2026-06-20T00:00:00Z" });
    _ProductData.Settings.Set("theme", std::string("dark"));

    _Store.Save("robot", _ProductData);

    auto _Loaded = _Store.Load("robot");
    ASSERT_EQ(1u, _Loaded.RecentProjects.size());
    EXPECT_EQ("D:/projects/robot.robot", _Loaded.RecentProjects[0].ProjectPath);
    EXPECT_EQ("Robot Cell", _Loaded.RecentProjects[0].DisplayName);
    EXPECT_EQ("2026-06-20T00:00:00Z", _Loaded.RecentProjects[0].LastOpenedTime);
    EXPECT_EQ("dark", _Loaded.Settings.Get("theme", iCAX::Data::Variant()).To<std::string>());

    EXPECT_TRUE(std::filesystem::exists(_Store.GetProductDataPath("robot")));
    EXPECT_TRUE(_Store.Load("weld").RecentProjects.empty());

    std::filesystem::remove_all(_Root);
}

TEST(ProductDataStoreTest, RejectsUnsafeProductIDInDataPath)
{
    auto _Root = std::filesystem::current_path()
        / "Temp"
        / ("ProductDataStoreUnsafeIDTest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(_Root);

    CFileProductDataStore _Store(_Root.string());

    EXPECT_NO_THROW((void)_Store.GetProductDataPath("robot.cam_1"));
    EXPECT_THROW((void)_Store.GetProductDataPath("../robot"), std::invalid_argument);
    EXPECT_THROW((void)_Store.GetProductDataPath("robot/cam"), std::invalid_argument);
    EXPECT_THROW((void)_Store.GetProductDataPath("robot..cam"), std::invalid_argument);

    std::filesystem::remove_all(_Root);
}

TEST(ProductManifestTest, ParsesResourceHandlerBindings)
{
    auto _Root = std::filesystem::current_path()
        / "Temp"
        / ("ProductManifestResourceHandlerTest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto _PluginPath = _Root / "plugins" / "Occ.dll";
    const auto _DependencyPath = _Root / "plugins" / "RenderService.dll";
    std::filesystem::create_directories(_PluginPath.parent_path());
    std::ofstream(_PluginPath.string(), std::ios::binary).close();
    std::ofstream(_DependencyPath.string(), std::ios::binary).close();

    const auto _ManifestPath = _Root / "product.manifest.json";
    std::ofstream _Output(_ManifestPath.string(), std::ios::binary);
    _Output << R"json({
  "schema": "icax.product.manifest",
  "schemaVersion": 1,
  "productId": "icax.test",
  "productName": "Test Product",
  "productVersion": "1.0",
  "projectFile": {
    "magic": "ICAX_TEST",
    "formatVersion": "1.0",
    "fileExtensions": [".test"]
  },
  "backend": {
    "pdo": {
      "enabled": true,
      "arenaSize": 67108864,
      "slotCapacity": 128
    },
    "modules": {
      "dependencies": ["plugins/RenderService.dll"]
    },
    "resources": {
      "handlers": [
        {
          "kind": "importer",
          "resourceType": "geometry.brep",
          "formatId": "cad.step",
          "extensions": [".step", ".stp"],
          "module": "plugins/Occ.dll",
          "provider": "occ.opencascade",
          "priority": 100
        }
      ]
    }
  },
  "webpage": {
    "entry": "apps/test/webpage/entry.mjs"
  }
})json";
    _Output.close();

    auto _Manifest = LoadProductManifest(_ManifestPath.string());

    ASSERT_EQ(1u, _Manifest.Definition.Modules.DependencyModules.size());
    EXPECT_EQ(std::filesystem::weakly_canonical(_DependencyPath), std::filesystem::path(_Manifest.Definition.Modules.DependencyModules.front()));
    EXPECT_TRUE(_Manifest.Definition.bEnablePDOHub);
    EXPECT_EQ(67108864ull, _Manifest.Definition.PDOHubCreateInfo.nArenaSize);
    EXPECT_EQ(128u, _Manifest.Definition.PDOHubCreateInfo.nSlotCapacity);

    ASSERT_EQ(1u, _Manifest.Definition.ResourceHandlers.size());
    const auto& _Handler = _Manifest.Definition.ResourceHandlers.front();
    EXPECT_EQ("importer", _Handler.Kind);
    EXPECT_EQ("geometry.brep", _Handler.ResourceType);
    EXPECT_EQ("cad.step", _Handler.FormatID);
    EXPECT_EQ((std::vector<std::string>{ ".step", ".stp" }), _Handler.Extensions);
    EXPECT_EQ("occ.opencascade", _Handler.ProviderID);
    EXPECT_EQ(100, _Handler.Priority);
    EXPECT_EQ(std::filesystem::weakly_canonical(_PluginPath), std::filesystem::path(_Handler.ModulePath));

    std::filesystem::remove_all(_Root);
}

TEST(ProductRuntimeTest, OpensAndClosesProjectCatalogDirectly)
{
    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();

    auto _pCatalog = _pRuntime->OpenProjectCatalog(
        "Robot Catalog",
        "memory://robot-catalog",
        "Robot Cell",
        "memory://robot-cell");
    ASSERT_NE(nullptr, _pCatalog);

    auto _pProject = _pCatalog->GetMainProject();
    ASSERT_NE(nullptr, _pProject);
    EXPECT_EQ("Robot Cell", _pProject->GetProjectName());
    EXPECT_EQ("memory://robot-cell", _pProject->GetProjectPath());
    EXPECT_TRUE(_pProject->IsRunning());
    EXPECT_TRUE(_pRuntime->GetSceneFrontendFacadeEndpoint(
        _pProject->GetProjectID(),
        _pProject->GetMainSceneID()).IsValid());

    EXPECT_TRUE(_pRuntime->CloseProjectCatalog(_pCatalog->GetCatalogID()));
    EXPECT_EQ(nullptr, _pRuntime->FindProjectCatalog(_pCatalog->GetCatalogID()));
    EXPECT_THROW(
        _pRuntime->GetSceneFrontendFacadeEndpoint(_pProject->GetProjectID(), _pProject->GetMainSceneID()),
        std::runtime_error);

    _pRuntime->Stop();
}

TEST(ProductRuntimeTest, OwnsIndependentProductServiceEnvironment)
{
    auto _pFirstRuntime = MakeRuntime("robot-a");
    auto _pSecondRuntime = MakeRuntime("robot-b");

    EXPECT_NE(&_pFirstRuntime->GetServiceProvider(), &_pSecondRuntime->GetServiceProvider());
}

TEST(ProductRuntimeTest, LocalProjectPathStartsQuickSaveLog)
{
    auto _ProjectPath = MakeTempProjectPath();
    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();

    auto _pCatalog = _pRuntime->OpenProjectCatalog(
        "Robot Catalog",
        _ProjectPath.string(),
        "Robot Cell",
        _ProjectPath.string());
    ASSERT_NE(nullptr, _pCatalog);

    auto _pProject = _pCatalog->GetMainProject();
    ASSERT_NE(nullptr, _pProject);
    EXPECT_TRUE(_pProject->IsQuickSaveLogOpen());
    EXPECT_EQ(_ProjectPath.string() + ".log", _pProject->GetQuickSaveLogPath());
    EXPECT_TRUE(std::filesystem::exists(_pProject->GetQuickSaveLogPath()));

    _pRuntime->Stop();
    RemoveProjectFiles(_ProjectPath);
}

TEST(ProductRuntimeTest, SettingsAreSavedInProductData)
{
    auto _pProductDataStore = std::make_shared<CMemoryProductDataStore>();
    auto _pRuntime = MakeRuntime("laser-3d-cam", _pProductDataStore);
    _pRuntime->Start();

    iCAX::Data::PropertyBag _Settings;
    _Settings.Set("ui.defaultLayer", std::string("outer-cut"));
    _Settings.Set("tool.defaultLibrary", std::string("D:/tools/laser"));
    _pRuntime->ReplaceSettings(_Settings);

    EXPECT_EQ(
        "outer-cut",
        _pRuntime->GetSettings()
            .Get("ui.defaultLayer", iCAX::Data::Variant())
            .To<std::string>());

    _pRuntime->Stop();

    const auto _SavedData = _pProductDataStore->Load("laser-3d-cam");
    EXPECT_EQ(
        "D:/tools/laser",
        _SavedData.Settings
            .Get("tool.defaultLibrary", iCAX::Data::Variant())
            .To<std::string>());
}

TEST(ProductRuntimeFacadeTest, ProductFacadeCanOpenAndListProjectCatalogs)
{
    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();
    auto _FrontendEndpoint = _pRuntime->GetProductFrontendFacadeEndpoint();

    iCAX::Data::ObjectMap _OpenPayload;
    _OpenPayload["catalogName"] = std::string("Robot Catalog");
    _OpenPayload["catalogPath"] = std::string("memory://robot-catalog");
    _OpenPayload["projectName"] = std::string("Robot Cell");
    _OpenPayload["projectPath"] = std::string("memory://robot-cell");
    auto _OpenRequest = MakeRequestFrame(2001, kProductOpenProjectCatalogMethodCode, iCAX::Data::Variant(_OpenPayload));
    _FrontendEndpoint.Send(_OpenRequest);

    auto _OpenResponses = WaitForProductFrames(_pRuntime, _FrontendEndpoint);
    ASSERT_EQ(1u, _OpenResponses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _OpenResponses[0].nStatus);

    auto _OpenPayloadObject = DecodeObjectPayload(_OpenResponses[0]);
    ASSERT_TRUE(_OpenPayloadObject.contains("catalog"));
    auto _Catalog = _OpenPayloadObject.at("catalog").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("Robot Catalog", _Catalog.at("catalogName").To<std::string>());
    EXPECT_TRUE(_Catalog.at("hasMainProject").To<bool>());
    auto _MainProject = _Catalog.at("mainProject").To<iCAX::Data::ObjectMap>();
    EXPECT_FALSE(_MainProject.at("mainSceneChannelId").To<iCAX::Data::uuid>().is_nil());
    auto _MainScene = _MainProject.at("mainScene").To<iCAX::Data::ObjectMap>();
    EXPECT_FALSE(_MainScene.at("sceneChannelId").To<iCAX::Data::uuid>().is_nil());

    auto _State = _OpenPayloadObject.at("state").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("robot", _State.at("productId").To<std::string>());
    auto _ProjectFile = _State.at("projectFile").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("ICAX_ROBOT", _ProjectFile.at("magic").To<std::string>());
    auto _RecentProjects = _State.at("recentProjects").To<iCAX::Data::VariantArray>();
    ASSERT_EQ(1u, _RecentProjects.size());
    auto _RecentProject = _RecentProjects[0].To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("D:/projects/recent-robot.robot", _RecentProject.at("path").To<std::string>());
    EXPECT_EQ("Recent Robot", _RecentProject.at("displayName").To<std::string>());
    EXPECT_EQ(1ull, _State.at("catalogCount").To<unsigned long long>());
    EXPECT_EQ(1u, _State.at("catalogs").To<iCAX::Data::VariantArray>().size());

    const auto _CatalogID = _Catalog.at("catalogId").To<iCAX::Data::uuid>();
    ClearFrames(_OpenResponses);

    iCAX::Data::ObjectMap _ClosePayload;
    _ClosePayload["catalogId"] = _CatalogID;
    auto _CloseRequest = MakeRequestFrame(2002, kProductCloseProjectCatalogMethodCode, iCAX::Data::Variant(_ClosePayload));
    _FrontendEndpoint.Send(_CloseRequest);

    auto _CloseResponses = WaitForProductFrames(_pRuntime, _FrontendEndpoint);
    ASSERT_EQ(1u, _CloseResponses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _CloseResponses[0].nStatus);

    auto _CloseState = DecodeObjectPayload(_CloseResponses[0]);
    EXPECT_EQ(0ull, _CloseState.at("catalogCount").To<unsigned long long>());
    EXPECT_TRUE(_CloseState.at("catalogs").To<iCAX::Data::VariantArray>().empty());
    ClearFrames(_CloseResponses);

    _pRuntime->Stop();
    EXPECT_FALSE(_FrontendEndpoint.IsValid());
}

TEST(ProductRuntimeFacadeTest, ProductRuntimeCanSendFrontendEvent)
{
    constexpr uint64_t kProductStateChangedEvent = iCAX::Interaction::MakeFacadeMethodCode("Product", "StateChanged");

    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();
    auto _FrontendEndpoint = _pRuntime->GetProductFrontendFacadeEndpoint();

    _pRuntime->SendFrontendEvent(kProductStateChangedEvent, "product-event");

    auto _Events = WaitForFrames(_FrontendEndpoint);
    ASSERT_EQ(1u, _Events.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _Events[0].nStatus);
    EXPECT_EQ(iCAX::Interaction::EFacadeFrameKind::Event, _Events[0].nKind);
    EXPECT_EQ(0u, _Events[0].nCallID);
    EXPECT_EQ(kProductStateChangedEvent, _Events[0].nMethodCode);
    EXPECT_EQ("product-event", iCAX::Interaction::GetFacadePayloadText(_Events[0]));

    ClearFrames(_Events);
    _pRuntime->Stop();
}

TEST(ProductRuntimeFacadeTest, ProductFacadeCallSentToSceneFacadeReturnsInvalidInvocation)
{
    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();
    auto _pCatalog = _pRuntime->OpenProjectCatalog("Project Context", {}, "Project Context");
    ASSERT_NE(nullptr, _pCatalog);
    auto _pProject = _pCatalog->GetMainProject();
    ASSERT_NE(nullptr, _pProject);

    auto _SceneEndpoint = _pRuntime->GetSceneFrontendFacadeEndpoint(
        _pProject->GetProjectID(),
        _pProject->GetMainSceneID());
    auto _Request = MakeRequestFrame(3001, kProductGetStateMethodCode);
    _SceneEndpoint.Send(_Request);

    auto _Results = WaitForFrames(_SceneEndpoint);
    ASSERT_EQ(1u, _Results.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::InvalidInvocation, _Results[0].nStatus);
    EXPECT_NE(
        std::string::npos,
        iCAX::Interaction::GetFacadePayloadText(_Results[0]).find(
            "Product Facade invocation requires the product scope"));
    EXPECT_FALSE(_pProject->GetLastFault().has_value());
    ClearFrames(_Results);
    _pRuntime->Stop();
}

TEST(ProductRuntimeFacadeTest, SceneFacadeProvidesProjectAndSceneContext)
{
    constexpr uint64_t kInspectProjectContextMethod =
        iCAX::Interaction::MakeFacadeMethodCode("InspectProject", "ProjectContext");

    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();
    auto _pCatalog = _pRuntime->OpenProjectCatalog("Project Context", {}, "Project Context");
    ASSERT_NE(nullptr, _pCatalog);
    auto _pProject = _pCatalog->GetMainProject();
    ASSERT_NE(nullptr, _pProject);

    const auto _ProjectID = _pProject->GetProjectID();
    auto _pFacade = std::make_shared<CTestProductFacade>("InspectProject");
    _pFacade->ExposeMethod(
        "ProjectContext",
        [_ProjectID](
            IN const iCAX::Interaction::CInvocation&,
            IN const iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            iCAX::Data::ObjectMap _Payload;
            _Payload["projectId"] = pProjectContext_ ? pProjectContext_->GetProjectID() : iCAX::Data::GenerateNilUUID();
            _Payload["sceneChannelId"] = pSceneContext_ ? pSceneContext_->GetSceneChannelID() : iCAX::Data::GenerateNilUUID();
            _Payload["projectName"] = pProjectContext_ ? pProjectContext_->GetProjectName() : std::string();
            _Payload["hasProductContext"] = pProductContext_ != nullptr;
            _Payload["hasProjectContext"] = pProjectContext_ != nullptr;
            _Payload["hasSceneContext"] = pSceneContext_ != nullptr;
            _Payload["sameProject"] = pProjectContext_
                && pProjectContext_->GetProjectID() == _ProjectID;

            iCAX::Interaction::CInvocationResult _Response;
            _Response.Payload = EncodeProductPayload(iCAX::Data::Variant(_Payload));
            return _Response;
        });
    ASSERT_TRUE(_pRuntime->GetFacadeRegistry().Register(_pFacade));

    auto _SceneEndpoint = _pRuntime->GetSceneFrontendFacadeEndpoint(_ProjectID, _pProject->GetMainSceneID());
    auto _Request = MakeRequestFrame(4001, kInspectProjectContextMethod);
    _SceneEndpoint.Send(_Request);

    auto _Responses = WaitForFrames(_SceneEndpoint);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _Responses[0].nStatus);

    auto _Payload = DecodeObjectPayload(_Responses[0]);
    EXPECT_EQ(_ProjectID, _Payload.at("projectId").To<iCAX::Data::uuid>());
    EXPECT_FALSE(_Payload.at("sceneChannelId").To<iCAX::Data::uuid>().is_nil());
    EXPECT_EQ("Project Context", _Payload.at("projectName").To<std::string>());
    EXPECT_TRUE(_Payload.at("hasProductContext").To<bool>());
    EXPECT_TRUE(_Payload.at("hasProjectContext").To<bool>());
    EXPECT_TRUE(_Payload.at("sameProject").To<bool>());

    ClearFrames(_Responses);
    _pRuntime->Stop();
}

TEST(ProductRuntimeCoroutineTest, RunsOnProductWorkerAndCancelsWithProduct)
{
    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();

    EXPECT_THROW(
        (void)_pRuntime->StartCoroutine(ResumeThreadAfterNextProductFrame()),
        std::logic_error);

    std::atomic_int _ResumeCount = 0;
    iCAX::Tasks::TaskCompletionSource<std::thread::id> _StartThreadSource;
    iCAX::Tasks::TaskCompletionSource<iCAX::Coroutines::CCoroutineHandle<std::thread::id>>
        _ResultHandleSource;
    iCAX::Tasks::TaskCompletionSource<iCAX::Coroutines::CCoroutineHandle<>>
        _LifetimeHandleSource;

    _pRuntime->GetProductTaskScheduler()->Schedule([&]() {
        _StartThreadSource.SetResult(std::this_thread::get_id());
        _ResultHandleSource.SetResult(
            _pRuntime->StartCoroutine(ResumeThreadAfterNextProductFrame()));
        _LifetimeHandleSource.SetResult(
            _pRuntime->StartCoroutine(RunUntilProductStops(_ResumeCount)));
    });

    ASSERT_TRUE(_ResultHandleSource.GetTask().WaitFor(std::chrono::seconds(2)));
    ASSERT_TRUE(_LifetimeHandleSource.GetTask().WaitFor(std::chrono::seconds(2)));
    const auto _ResultHandle = _ResultHandleSource.GetTask().Result();
    const auto _LifetimeHandle = _LifetimeHandleSource.GetTask().Result();
    const auto _ProductThreadID = _StartThreadSource.GetTask().Result();
    ASSERT_TRUE(_ResultHandle.Completion().WaitFor(std::chrono::seconds(2)));
    EXPECT_EQ(_ProductThreadID, _ResultHandle.Completion().Result());

    auto _CancellationContinuation = _LifetimeHandle.Completion().ContinueWith(
        [](iCAX::Tasks::Task<void> Completed_) {
            if (!Completed_.IsCanceled())
            {
                throw std::logic_error("Product coroutine should be canceled during product shutdown");
            }
            return std::this_thread::get_id();
        });

    for (int _Index = 0; _Index < 200 && _ResumeCount.load() < 2; ++_Index)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_GE(_ResumeCount.load(), 2);

    _pRuntime->Stop();
    EXPECT_TRUE(_LifetimeHandle.Completion().IsCanceled());
    ASSERT_TRUE(_CancellationContinuation.IsCompletedSuccessfully());
    EXPECT_EQ(_ProductThreadID, _CancellationContinuation.Result());

    _pRuntime->Start();
    iCAX::Tasks::TaskCompletionSource<iCAX::Coroutines::CCoroutineHandle<std::thread::id>>
        _RestartHandleSource;
    _pRuntime->GetProductTaskScheduler()->Schedule([&]() {
        _RestartHandleSource.SetResult(
            _pRuntime->StartCoroutine(ResumeThreadAfterNextProductFrame()));
    });
    ASSERT_TRUE(_RestartHandleSource.GetTask().WaitFor(std::chrono::seconds(2)));
    const auto _RestartHandle = _RestartHandleSource.GetTask().Result();
    ASSERT_TRUE(_RestartHandle.Completion().WaitFor(std::chrono::seconds(2)));
    EXPECT_TRUE(_RestartHandle.Completion().IsCompletedSuccessfully());
    _pRuntime->Stop();
}

