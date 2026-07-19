#include "pch.h"


#include <ApplicationRuntime/ApplicationRuntime.h>
#include <ApplicationRuntime/ApplicationRuntimeFacades.h>
#include <Facades/FacadeMethod.h>
#include <Facades/Facade.h>
#include <Data/Variant.h>
#include <Database/ComponentBase.h>
#include <Facades/FacadeFrame.h>
#include <Facades/FacadePayload.h>
#include <Product/ProductFacades.h>


using namespace iCAX::Application;

namespace
{
    class CReopenProbeFacade final : public iCAX::Interaction::CFacade
    {
    public:
        CReopenProbeFacade()
            : CFacade("ReopenProbe")
        {
            ExposeMethod(
                "Ping",
                [](
                    IN const iCAX::Interaction::CInvocation&,
                    IN const iCAX::Application::IApplicationContext&,
                    IN iCAX::Product::IProductContext*,
                    IN iCAX::Project::IProjectContext*,
                    IN iCAX::Project::ISceneContext*) {
                    return iCAX::Interaction::CInvocationResult{};
                });
        }
    };

    void ClearFrames(IN OUT std::vector<iCAX::Interaction::CFacadeFrame>& Frames_) noexcept
    {
        Frames_.clear();
    }

    void SendFrame(
        IN const iCAX::Interaction::CFacadeEndpoint& Endpoint_,
        IN const iCAX::Interaction::CFacadeFrame& Frame_)
    {
        Endpoint_.Send(Frame_);
    }

    void SendFrame(
        IN const iCAX::Interaction::CFacadeEndpoint& Endpoint_,
        IN iCAX::Interaction::CFacadeFrame&& Frame_)
    {
        Endpoint_.Send(std::move(Frame_));
    }

    iCAX::Interaction::CFacadeFrame MakeApplicationRequestFrame(
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
            _Frame.Payload = EncodeApplicationRuntimePayload(*Payload_);
        }

        return _Frame;
    }

    iCAX::Interaction::CFacadeFrame MakeProductRequestFrame(
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
            _Frame.Payload = iCAX::Product::EncodeProductPayload(*Payload_);
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

    iCAX::Data::ObjectMap DecodeApplicationObjectPayload(IN const iCAX::Interaction::CFacadeFrame& Frame_)
    {
        auto _Variant = DecodeApplicationRuntimePayload(Frame_.Payload);
        if (!_Variant.Is<iCAX::Data::ObjectMap>())
        {
            throw std::runtime_error("Expected object payload");
        }
        return _Variant.To<iCAX::Data::ObjectMap>();
    }

    iCAX::Data::ObjectMap DecodeProductObjectPayload(IN const iCAX::Interaction::CFacadeFrame& Frame_)
    {
        auto _Variant = iCAX::Product::DecodeProductPayload(Frame_.Payload);
        if (!_Variant.Is<iCAX::Data::ObjectMap>())
        {
            throw std::runtime_error("Expected object payload");
        }
        return _Variant.To<iCAX::Data::ObjectMap>();
    }

    ApplicationRuntimeConfig MakeTwoProductConfig()
    {
        ApplicationRuntimeConfig _Config;
        _Config.strApplicationSettingsPath = "Setting/Application.Setting";
        _Config.Descriptor.AppID = "icax-test";
        _Config.Descriptor.AppName = "iCAX Test";
        _Config.Paths.InstallDirectory = ".";
        _Config.Paths.UserConfigDirectory = "Setting";
        _Config.Paths.CacheDirectory = "Cache";
        _Config.Paths.TempDirectory = "Temp";
        _Config.Paths.LogDirectory = "Log";

        iCAX::Product::CProductDefinition _Robot;
        _Robot.ProductID = "robot";
        _Robot.ProductName = "Robot";
        _Robot.FrontendEntry = "h5://robot/index.html";
        _Robot.ProjectFile.Magic = "ICAX_ROBOT";
        _Robot.ProjectFile.FormatVersion = "1.0";
        _Robot.ProjectFile.FileExtensions.push_back(".robot");

        iCAX::Product::CProductDefinition _Weld;
        _Weld.ProductID = "weld";
        _Weld.ProductName = "Weld";
        _Weld.FrontendEntry = "h5://weld/index.html";
        _Weld.ProjectFile.Magic = "ICAX_WELD";

        _Config.Products.push_back(_Robot);
        _Config.Products.push_back(_Weld);
        _Config.nFrameIntervalMilliseconds = 1;
        return _Config;
    }

    iCAX::Coroutines::CCoroutine<std::thread::id> ResumeThreadAfterNextFrame()
    {
        co_await iCAX::Coroutines::NextFrame();
        co_return std::this_thread::get_id();
    }

    iCAX::Coroutines::CCoroutine<> RunUntilApplicationStops(std::atomic_int& ResumeCount_)
    {
        while (true)
        {
            ++ResumeCount_;
            co_await iCAX::Coroutines::NextFrame();
        }
    }
}

TEST(ApplicationRuntimeFacadeTest, ApplicationFacadeReturnsProductListBeforeProductIsStarted)
{
    CApplicationRuntime _Runtime;
    _Runtime.Start();

    auto _FrontendEndpoint = _Runtime.GetApplicationFrontendFacadeEndpoint();
    auto _Request = MakeApplicationRequestFrame(1001, kAppGetStateMethodCode);
    SendFrame(_FrontendEndpoint, _Request);

    auto _Responses = WaitForFrames(_FrontendEndpoint);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _Responses[0].nStatus);
    EXPECT_EQ(1001u, _Responses[0].nCallID);
    EXPECT_EQ(kAppGetStateMethodCode, _Responses[0].nMethodCode);

    auto _State = DecodeApplicationObjectPayload(_Responses[0]);
    ASSERT_TRUE(_State.contains("applicationChannelId"));
    ASSERT_TRUE(_State.contains("productCount"));
    ASSERT_TRUE(_State.contains("products"));
    EXPECT_EQ(_Runtime.GetApplicationChannelID(), _State.at("applicationChannelId").To<iCAX::Data::uuid>());
    EXPECT_EQ("Running", _State.at("state").To<std::string>());
    EXPECT_EQ(1ull, _State.at("productCount").To<unsigned long long>());
    EXPECT_EQ(0ull, _State.at("runningProductCount").To<unsigned long long>());

    auto _Products = _State.at("products").To<iCAX::Data::VariantArray>();
    ASSERT_EQ(1u, _Products.size());
    auto _Product = _Products[0].To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("icax.default", _Product.at("productId").To<std::string>());
    EXPECT_FALSE(_Product.at("isStarted").To<bool>());

    ClearFrames(_Responses);
    _Runtime.Stop();
}

TEST(ApplicationRuntimeFacadeTest, ApplicationFrontendEndpointIsAvailableAfterStart)
{
    CApplicationRuntime _Runtime;
    EXPECT_THROW(_Runtime.GetApplicationFrontendFacadeEndpoint(), std::logic_error);

    _Runtime.Start();
    auto _FrontendEndpoint = _Runtime.GetApplicationFrontendFacadeEndpoint();
    ASSERT_TRUE(_FrontendEndpoint.IsValid());

    auto _Request = MakeApplicationRequestFrame(1501, kAppGetStateMethodCode);
    SendFrame(_FrontendEndpoint, _Request);

    auto _Responses = WaitForFrames(_FrontendEndpoint);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _Responses[0].nStatus);
    EXPECT_EQ(1501u, _Responses[0].nCallID);

    ClearFrames(_Responses);
    _Runtime.Stop();
}

TEST(ApplicationRuntimeFacadeTest, ApplicationRuntimeCanSendFrontendEvent)
{
    constexpr uint64_t kStateChangedEvent = iCAX::Interaction::MakeFacadeMethodCode("App", "StateChanged");

    CApplicationRuntime _Runtime;
    _Runtime.Start();

    auto _FrontendEndpoint = _Runtime.GetApplicationFrontendFacadeEndpoint();
    _Runtime.SendFrontendEvent(kStateChangedEvent, "application-event");

    auto _Events = WaitForFrames(_FrontendEndpoint);
    ASSERT_EQ(1u, _Events.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _Events[0].nStatus);
    EXPECT_EQ(iCAX::Interaction::EFacadeFrameKind::Event, _Events[0].nKind);
    EXPECT_EQ(0u, _Events[0].nCallID);
    EXPECT_EQ(kStateChangedEvent, _Events[0].nMethodCode);
    EXPECT_EQ("application-event", iCAX::Interaction::GetFacadePayloadText(_Events[0]));

    ClearFrames(_Events);
    _Runtime.Stop();
}

TEST(ApplicationRuntimeFacadeTest, ApplicationFacadeCanStartProduct)
{
    CApplicationRuntime _Runtime;
    _Runtime.Start();

    auto _FrontendEndpoint = _Runtime.GetApplicationFrontendFacadeEndpoint();
    auto _Request = MakeApplicationRequestFrame(2001, kAppStartProductMethodCode);
    SendFrame(_FrontendEndpoint, _Request);

    auto _Responses = WaitForFrames(_FrontendEndpoint);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _Responses[0].nStatus);

    auto _Payload = DecodeApplicationObjectPayload(_Responses[0]);
    auto _Product = _Payload.at("product").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("icax.default", _Product.at("productId").To<std::string>());
    EXPECT_TRUE(_Product.at("isStarted").To<bool>());
    EXPECT_FALSE(_Product.at("productChannelId").To<iCAX::Data::uuid>().is_nil());
    ASSERT_NE(nullptr, _Runtime.FindProductRuntime("icax.default"));
    EXPECT_TRUE(_Runtime.GetProductFrontendFacadeEndpoint("icax.default").IsValid());

    auto _State = _Payload.at("state").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ(1ull, _State.at("runningProductCount").To<unsigned long long>());
    ClearFrames(_Responses);
    _Runtime.Stop();
}

TEST(ApplicationRuntimeFacadeTest, ProductFacadeCanOpenAndCloseProjectCatalogAfterProductStarts)
{
    CApplicationRuntime _Runtime;
    _Runtime.Start();
    auto _pRuntime = _Runtime.StartProduct();
    ASSERT_NE(nullptr, _pRuntime);

    auto _ProductEndpoint = _Runtime.GetProductFrontendFacadeEndpoint(_pRuntime->GetProductID());

    iCAX::Data::ObjectMap _OpenPayload;
    _OpenPayload["catalogName"] = std::string("Robot Catalog");
    _OpenPayload["catalogPath"] = std::string("memory://robot-catalog");
    _OpenPayload["projectName"] = std::string("Robot Cell");
    _OpenPayload["projectPath"] = std::string("memory://robot-cell");
    auto _OpenRequest = MakeProductRequestFrame(
        3001,
        iCAX::Product::kProductOpenProjectCatalogMethodCode,
        iCAX::Data::Variant(_OpenPayload));
    SendFrame(_ProductEndpoint, _OpenRequest);

    auto _OpenResponses = WaitForFrames(_ProductEndpoint);
    ASSERT_EQ(1u, _OpenResponses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _OpenResponses[0].nStatus);

    auto _OpenObject = DecodeProductObjectPayload(_OpenResponses[0]);
    auto _Catalog = _OpenObject.at("catalog").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("Robot Catalog", _Catalog.at("catalogName").To<std::string>());
    EXPECT_TRUE(_Catalog.at("hasMainProject").To<bool>());

    auto _Project = _Catalog.at("mainProject").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("Robot Cell", _Project.at("projectName").To<std::string>());
    EXPECT_EQ("Running", _Project.at("state").To<std::string>());
    EXPECT_FALSE(_Project.at("mainSceneChannelId").To<iCAX::Data::uuid>().is_nil());
    auto _MainScene = _Project.at("mainScene").To<iCAX::Data::ObjectMap>();
    EXPECT_FALSE(_MainScene.at("sceneChannelId").To<iCAX::Data::uuid>().is_nil());
    const auto _ProjectID = _Project.at("projectId").To<iCAX::Data::uuid>();
    const auto _MainSceneID = _Project.at("mainSceneId").To<iCAX::Data::uuid>();
    const auto _CatalogID = _Catalog.at("catalogId").To<iCAX::Data::uuid>();
    EXPECT_TRUE(_Runtime.GetSceneFrontendFacadeEndpoint(_ProjectID, _MainSceneID).IsValid());
    ASSERT_NE(nullptr, _pRuntime->FindProjectCatalog(_CatalogID));
    ClearFrames(_OpenResponses);

    iCAX::Data::ObjectMap _ClosePayload;
    _ClosePayload["catalogId"] = _CatalogID;
    auto _CloseRequest = MakeProductRequestFrame(
        3002,
        iCAX::Product::kProductCloseProjectCatalogMethodCode,
        iCAX::Data::Variant(_ClosePayload));
    SendFrame(_ProductEndpoint, _CloseRequest);

    auto _CloseResponses = WaitForFrames(_ProductEndpoint);
    ASSERT_EQ(1u, _CloseResponses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _CloseResponses[0].nStatus);
    auto _CloseState = DecodeProductObjectPayload(_CloseResponses[0]);
    EXPECT_EQ(0ull, _CloseState.at("catalogCount").To<unsigned long long>());
    EXPECT_EQ(nullptr, _pRuntime->FindProjectCatalog(_CatalogID));
    EXPECT_THROW(_Runtime.GetSceneFrontendFacadeEndpoint(_ProjectID, _MainSceneID), std::runtime_error);
    ClearFrames(_CloseResponses);

    _Runtime.Stop();
}

TEST(ApplicationRuntimeFacadeTest, ApplicationRuntimeCanStartMultipleConfiguredProducts)
{
    CApplicationRuntime _Runtime;
    _Runtime.SetConfig(MakeTwoProductConfig());
    _Runtime.Start();

    auto _ApplicationEndpoint = _Runtime.GetApplicationFrontendFacadeEndpoint();

    iCAX::Data::ObjectMap _RobotPayload;
    _RobotPayload["productId"] = std::string("robot");
    SendFrame(_ApplicationEndpoint, MakeApplicationRequestFrame(
        4001,
        kAppStartProductMethodCode,
        iCAX::Data::Variant(_RobotPayload)));
    auto _RobotResponses = WaitForFrames(_ApplicationEndpoint);
    ASSERT_EQ(1u, _RobotResponses.size());
    auto _Robot = DecodeApplicationObjectPayload(_RobotResponses[0]).at("product").To<iCAX::Data::ObjectMap>();
    ClearFrames(_RobotResponses);

    iCAX::Data::ObjectMap _WeldPayload;
    _WeldPayload["productId"] = std::string("weld");
    SendFrame(_ApplicationEndpoint, MakeApplicationRequestFrame(
        4002,
        kAppStartProductMethodCode,
        iCAX::Data::Variant(_WeldPayload)));
    auto _WeldResponses = WaitForFrames(_ApplicationEndpoint);
    ASSERT_EQ(1u, _WeldResponses.size());
    auto _WeldResponse = DecodeApplicationObjectPayload(_WeldResponses[0]);
    auto _Weld = _WeldResponse.at("product").To<iCAX::Data::ObjectMap>();
    auto _State = _WeldResponse.at("state").To<iCAX::Data::ObjectMap>();

    EXPECT_EQ("robot", _Robot.at("productId").To<std::string>());
    EXPECT_EQ("weld", _Weld.at("productId").To<std::string>());
    EXPECT_NE(
        _Robot.at("productChannelId").To<iCAX::Data::uuid>(),
        _Weld.at("productChannelId").To<iCAX::Data::uuid>());
    EXPECT_EQ(2ull, _State.at("productCount").To<unsigned long long>());
    EXPECT_EQ(2ull, _State.at("runningProductCount").To<unsigned long long>());
    ASSERT_NE(nullptr, _Runtime.FindProductRuntime("robot"));
    ASSERT_NE(nullptr, _Runtime.FindProductRuntime("weld"));
    ClearFrames(_WeldResponses);

    _Runtime.Stop();
}

TEST(ApplicationRuntimeFacadeTest, StartProductReturnsSingleRuntimeAcrossConcurrentCallers)
{
    CApplicationRuntime _Runtime;
    _Runtime.SetConfig(MakeTwoProductConfig());
    _Runtime.Start();

    constexpr int kThreadCount = 8;
    std::array<std::shared_ptr<iCAX::Product::CProductRuntime>, kThreadCount> _Runtimes;
    std::array<std::exception_ptr, kThreadCount> _Exceptions;
    std::vector<std::thread> _Threads;

    for (int _Index = 0; _Index < kThreadCount; ++_Index)
    {
        _Threads.emplace_back([&, _Index]() {
            try
            {
                _Runtimes[_Index] = _Runtime.StartProduct("robot");
            }
            catch (...)
            {
                _Exceptions[_Index] = std::current_exception();
            }
        });
    }

    for (auto& _Thread : _Threads)
    {
        _Thread.join();
    }

    for (const auto& _Exception : _Exceptions)
    {
        if (_Exception)
        {
            std::rethrow_exception(_Exception);
        }
    }

    ASSERT_NE(nullptr, _Runtimes[0]);
    for (const auto& _pRuntime : _Runtimes)
    {
        EXPECT_EQ(_Runtimes[0], _pRuntime);
    }

    EXPECT_EQ(1u, _Runtime.GetProductRuntimes().size());
    EXPECT_EQ(_Runtimes[0], _Runtime.FindProductRuntime("robot"));
    EXPECT_TRUE(_Runtime.GetProductFrontendFacadeEndpoint("robot").IsValid());

    _Runtime.Stop();
}

TEST(ApplicationRuntimeFacadeTest, StopProductThenStartProductCreatesFreshRuntimeAndRegistries)
{
    CApplicationRuntime _Runtime;
    _Runtime.SetConfig(MakeTwoProductConfig());
    _Runtime.Start();

    auto _pFirstRuntime = _Runtime.StartProduct("robot");
    ASSERT_NE(nullptr, _pFirstRuntime);
    const auto _FirstProductChannelID = _pFirstRuntime->GetProductChannelID();
    auto _FirstEndpoint = _Runtime.GetProductFrontendFacadeEndpoint("robot");
    ASSERT_TRUE(_FirstEndpoint.IsValid());

    constexpr uint64_t kReopenProbeMethod = iCAX::Interaction::MakeFacadeMethodCode("ReopenProbe", "Ping");
    const auto _ReopenProbeFacadeCode = iCAX::Interaction::GetFacadeCode(kReopenProbeMethod);
    ASSERT_TRUE(_pFirstRuntime->GetFacadeRegistry().Register(std::make_shared<CReopenProbeFacade>()));
    EXPECT_TRUE(_pFirstRuntime->GetFacadeRegistry().Has(_ReopenProbeFacadeCode));
    _pFirstRuntime->GetMetaRegistry().RegistType(
        "ReopenProbe.Component",
        iCAX::Database::CComponentBase::S_ClassName);
    EXPECT_TRUE(_pFirstRuntime->GetMetaRegistry().HasTypeByName("ReopenProbe.Component"));

    EXPECT_TRUE(_Runtime.StopProduct("robot"));
    EXPECT_EQ(nullptr, _Runtime.FindProductRuntime("robot"));
    EXPECT_FALSE(_FirstEndpoint.IsValid());
    EXPECT_FALSE(_pFirstRuntime->IsStarted());

    auto _pSecondRuntime = _Runtime.StartProduct("robot");
    ASSERT_NE(nullptr, _pSecondRuntime);
    EXPECT_NE(_pFirstRuntime, _pSecondRuntime);
    EXPECT_TRUE(_pSecondRuntime->IsStarted());
    EXPECT_NE(_FirstProductChannelID, _pSecondRuntime->GetProductChannelID());
    EXPECT_TRUE(_Runtime.GetProductFrontendFacadeEndpoint("robot").IsValid());
    EXPECT_FALSE(_pSecondRuntime->GetFacadeRegistry().Has(_ReopenProbeFacadeCode));
    EXPECT_FALSE(_pSecondRuntime->GetMetaRegistry().HasTypeByName("ReopenProbe.Component"));

    _Runtime.Stop();
}

TEST(ApplicationRuntimeFacadeTest, StartProductIsRejectedAfterRuntimeStops)
{
    CApplicationRuntime _Runtime;
    _Runtime.SetConfig(MakeTwoProductConfig());
    _Runtime.Start();
    _Runtime.Stop();

    EXPECT_THROW(_Runtime.StartProduct("robot"), std::logic_error);
}

TEST(ApplicationRuntimeFacadeTest, ProductDefinitionRequiresProjectFileMagic)
{
    auto _Config = MakeTwoProductConfig();
    _Config.Products.front().ProjectFile.Magic.clear();

    CApplicationRuntime _Runtime;
    EXPECT_THROW(_Runtime.SetConfig(_Config), std::invalid_argument);
}

TEST(ApplicationRuntimeFacadeTest, ProductDefinitionRequiresSafeProductID)
{
    auto _Config = MakeTwoProductConfig();
    _Config.Products.front().ProductID = "../robot";

    CApplicationRuntime _Runtime;
    EXPECT_THROW(_Runtime.SetConfig(_Config), std::invalid_argument);
}

TEST(ApplicationRuntimeFacadeTest, ProductDefinitionRequiresUniqueProjectFileMagic)
{
    auto _Config = MakeTwoProductConfig();
    _Config.Products.back().ProjectFile.Magic = _Config.Products.front().ProjectFile.Magic;

    CApplicationRuntime _Runtime;
    EXPECT_THROW(_Runtime.SetConfig(_Config), std::invalid_argument);
}

TEST(ApplicationRuntimeFacadeTest, ApplicationFacadeCanResolveAndOpenProjectFile)
{
    auto _Root = std::filesystem::current_path()
        / "Temp"
        / ("ApplicationRuntimeOpenProjectFileTest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(_Root);
    const auto _ProjectPath = _Root / "RobotCell.robot";
    {
        std::ofstream _Output(_ProjectPath, std::ios::binary | std::ios::trunc);
        _Output << "ICAX_ROBOT\nproject-body";
    }

    auto _Config = MakeTwoProductConfig();
    _Config.Paths.UserConfigDirectory = (_Root / "Setting").string();

    CApplicationRuntime _Runtime;
    _Runtime.SetConfig(_Config);
    _Runtime.Start();

    auto _ApplicationEndpoint = _Runtime.GetApplicationFrontendFacadeEndpoint();

    iCAX::Data::ObjectMap _ResolvePayload;
    _ResolvePayload["projectPath"] = _ProjectPath.string();
    SendFrame(_ApplicationEndpoint, MakeApplicationRequestFrame(
        6001,
        kAppResolveProjectFileMethodCode,
        iCAX::Data::Variant(_ResolvePayload)));

    auto _ResolveResponses = WaitForFrames(_ApplicationEndpoint);
    ASSERT_EQ(1u, _ResolveResponses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _ResolveResponses[0].nStatus);
    auto _ResolveResponse = DecodeApplicationObjectPayload(_ResolveResponses[0]);
    auto _Resolve = _ResolveResponse.at("resolve").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("Resolved", _Resolve.at("status").To<std::string>());
    EXPECT_EQ("robot", _Resolve.at("productId").To<std::string>());
    EXPECT_TRUE(_Resolve.at("matchedByMagic").To<bool>());
    ClearFrames(_ResolveResponses);

    iCAX::Data::ObjectMap _OpenPayload;
    _OpenPayload["projectPath"] = _ProjectPath.string();
    SendFrame(_ApplicationEndpoint, MakeApplicationRequestFrame(
        6002,
        kAppOpenProjectFileMethodCode,
        iCAX::Data::Variant(_OpenPayload)));

    auto _OpenResponses = WaitForFrames(_ApplicationEndpoint);
    ASSERT_EQ(1u, _OpenResponses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::Ok, _OpenResponses[0].nStatus);

    auto _OpenResponse = DecodeApplicationObjectPayload(_OpenResponses[0]);
    auto _Product = _OpenResponse.at("product").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("robot", _Product.at("productId").To<std::string>());
    EXPECT_TRUE(_Product.at("isStarted").To<bool>());
    auto _Catalog = _OpenResponse.at("catalog").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("RobotCell", _Catalog.at("catalogName").To<std::string>());
    EXPECT_EQ(_ProjectPath.string(), _Catalog.at("catalogPath").To<std::string>());
    EXPECT_TRUE(_Catalog.at("hasMainProject").To<bool>());

    auto _Project = _Catalog.at("mainProject").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("RobotCell", _Project.at("projectName").To<std::string>());
    EXPECT_EQ(_ProjectPath.string(), _Project.at("projectPath").To<std::string>());
    EXPECT_EQ("Running", _Project.at("state").To<std::string>());

    auto _State = _OpenResponse.at("state").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ(1ull, _State.at("runningProductCount").To<unsigned long long>());
    ASSERT_NE(nullptr, _Runtime.FindProductRuntime("robot"));

    ClearFrames(_OpenResponses);
    _Runtime.Stop();
    std::filesystem::remove_all(_Root);
}

TEST(ApplicationRuntimeFacadeTest, ApplicationRuntimeCanOpenProjectFileDirectly)
{
    auto _Root = std::filesystem::current_path()
        / "Temp"
        / ("ApplicationRuntimeDirectOpenProjectFileTest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(_Root);
    const auto _ProjectPath = _Root / "DirectRobot.robot";
    {
        std::ofstream _Output(_ProjectPath, std::ios::binary | std::ios::trunc);
        _Output << "ICAX_ROBOT\nproject-body";
    }

    auto _Config = MakeTwoProductConfig();
    _Config.Paths.UserConfigDirectory = (_Root / "Setting").string();

    CApplicationRuntime _Runtime;
    _Runtime.SetConfig(_Config);
    _Runtime.Start();

    auto _Resolve = _Runtime.ResolveProjectFileProduct(_ProjectPath.string());
    EXPECT_EQ(EProductFileResolveStatus::Resolved, _Resolve.Status);
    EXPECT_EQ("robot", _Resolve.ProductID);

    auto _pCatalog = _Runtime.OpenProjectFile(_ProjectPath.string());
    ASSERT_NE(nullptr, _pCatalog);
    EXPECT_EQ("DirectRobot", _pCatalog->GetCatalogName());
    EXPECT_EQ(_ProjectPath.string(), _pCatalog->GetCatalogPath());
    ASSERT_NE(nullptr, _Runtime.FindProductRuntime("robot"));
    ASSERT_NE(nullptr, _pCatalog->GetMainProject());
    EXPECT_EQ(_ProjectPath.string(), _pCatalog->GetMainProject()->GetProjectPath());

    _Runtime.Stop();
    std::filesystem::remove_all(_Root);
}

TEST(ApplicationRuntimeFacadeTest, ApplicationFacadeCallSentToProductFacadeReturnsNotFound)
{
    CApplicationRuntime _Runtime;
    _Runtime.Start();
    auto _pRuntime = _Runtime.StartProduct();
    ASSERT_NE(nullptr, _pRuntime);

    auto _ProductEndpoint = _Runtime.GetProductFrontendFacadeEndpoint(_pRuntime->GetProductID());
    auto _Request = MakeApplicationRequestFrame(5001, kAppGetStateMethodCode);
    SendFrame(_ProductEndpoint, _Request);

    auto _Responses = WaitForFrames(_ProductEndpoint);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Interaction::EInvocationStatus::FacadeNotFound, _Responses[0].nStatus);
    EXPECT_EQ(5001u, _Responses[0].nCallID);

    ClearFrames(_Responses);
    _Runtime.Stop();
}

TEST(ApplicationRuntimeCoroutineTest, RunsOnRuntimeWorkerAndCancelsWithRuntime)
{
    CApplicationRuntime _Runtime;
    auto _Config = MakeTwoProductConfig();
    _Config.Products.resize(1);
    _Config.nFrameIntervalMilliseconds = 1;
    _Runtime.SetConfig(_Config);
    _Runtime.Start();

    EXPECT_THROW(
        (void)_Runtime.StartCoroutine(ResumeThreadAfterNextFrame()),
        std::logic_error);

    std::atomic_int _ResumeCount = 0;
    iCAX::Tasks::TaskCompletionSource<std::thread::id> _StartThreadSource;
    iCAX::Tasks::TaskCompletionSource<iCAX::Coroutines::CCoroutineHandle<std::thread::id>>
        _ResultHandleSource;
    iCAX::Tasks::TaskCompletionSource<iCAX::Coroutines::CCoroutineHandle<>>
        _LifetimeHandleSource;

    _Runtime.GetApplicationTaskScheduler()->Schedule([&]() {
        _StartThreadSource.SetResult(std::this_thread::get_id());
        _ResultHandleSource.SetResult(
            _Runtime.StartCoroutine(ResumeThreadAfterNextFrame()));
        _LifetimeHandleSource.SetResult(
            _Runtime.StartCoroutine(RunUntilApplicationStops(_ResumeCount)));
    });

    ASSERT_TRUE(_ResultHandleSource.GetTask().WaitFor(std::chrono::seconds(2)));
    ASSERT_TRUE(_LifetimeHandleSource.GetTask().WaitFor(std::chrono::seconds(2)));
    const auto _ResultHandle = _ResultHandleSource.GetTask().Result();
    const auto _LifetimeHandle = _LifetimeHandleSource.GetTask().Result();
    const auto _RuntimeThreadID = _StartThreadSource.GetTask().Result();
    ASSERT_TRUE(_ResultHandle.Completion().WaitFor(std::chrono::seconds(2)));
    EXPECT_EQ(_RuntimeThreadID, _ResultHandle.Completion().Result());

    auto _CancellationContinuation = _LifetimeHandle.Completion().ContinueWith(
        [](iCAX::Tasks::Task<void> Completed_) {
            if (!Completed_.IsCanceled())
            {
                throw std::logic_error("Application coroutine should be canceled during runtime shutdown");
            }
            return std::this_thread::get_id();
        });

    for (int _Index = 0; _Index < 200 && _ResumeCount.load() < 2; ++_Index)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_GE(_ResumeCount.load(), 2);

    _Runtime.Stop();
    EXPECT_TRUE(_LifetimeHandle.Completion().IsCanceled());
    ASSERT_TRUE(_CancellationContinuation.IsCompletedSuccessfully());
    EXPECT_EQ(_RuntimeThreadID, _CancellationContinuation.Result());

    _Runtime.Start();
    iCAX::Tasks::TaskCompletionSource<iCAX::Coroutines::CCoroutineHandle<std::thread::id>>
        _RestartHandleSource;
    _Runtime.GetApplicationTaskScheduler()->Schedule([&]() {
        _RestartHandleSource.SetResult(
            _Runtime.StartCoroutine(ResumeThreadAfterNextFrame()));
    });
    ASSERT_TRUE(_RestartHandleSource.GetTask().WaitFor(std::chrono::seconds(2)));
    const auto _RestartHandle = _RestartHandleSource.GetTask().Result();
    ASSERT_TRUE(_RestartHandle.Completion().WaitFor(std::chrono::seconds(2)));
    EXPECT_TRUE(_RestartHandle.Completion().IsCompletedSuccessfully());
    _Runtime.Stop();
}
