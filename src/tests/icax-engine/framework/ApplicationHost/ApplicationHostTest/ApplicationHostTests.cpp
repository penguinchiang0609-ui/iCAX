#include <gtest/gtest.h>

#include <ApplicationHost/ApplicationHost.h>
#include <ApplicationHost/ApplicationHostCommands.h>
#include <CommandHandler/CommandRoute.h>
#include <CommandHandler/CommandTarget.h>
#include <Data/Variant.h>
#include <Database/ComponentBase.h>
#include <Mailbox/Mail.h>
#include <Mailbox/MailPayload.h>
#include <Product/ProductCommands.h>

#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace iCAX::ApplicationHost;

namespace
{
    class CReopenProbeCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CReopenProbeCommandTarget()
            : CCommandTarget("ReopenProbe")
        {
            Bind(
                "Ping",
                [](
                    IN const iCAX::Command::CCommandRequest&,
                    IN iCAX::Application::IApplicationContext&,
                    IN iCAX::Product::IProductContext*,
                    IN iCAX::Project::IProjectContext*) {
                    return iCAX::Command::CCommandResponse{};
                });
        }
    };

    void ReleaseTestMailPayload(IN OUT iCAX::Mail::Mail& Mail_) noexcept
    {
        iCAX::Mail::ReleaseMailPayload(Mail_);
    }

    void ReleaseMailPayloads(IN OUT std::vector<iCAX::Mail::Mail>& Mails_) noexcept
    {
        for (auto& _Mail : Mails_)
        {
            ReleaseTestMailPayload(_Mail);
        }
        Mails_.clear();
    }

    void SendMail(
        IN const iCAX::Mail::CMailPostOffice& PostOffice_,
        IN OUT iCAX::Mail::Mail& Mail_)
    {
        PostOffice_.Send(Mail_);
        ReleaseTestMailPayload(Mail_);
    }

    void SendMail(
        IN const iCAX::Mail::CMailPostOffice& PostOffice_,
        IN iCAX::Mail::Mail&& Mail_)
    {
        PostOffice_.Send(Mail_);
        ReleaseTestMailPayload(Mail_);
    }

    iCAX::Mail::Mail MakeApplicationRequestMail(
        IN uint64_t nMailID_,
        IN uint64_t nTypeCode_,
        IN const std::optional<iCAX::Data::Variant>& Payload_ = std::nullopt)
    {
        iCAX::Mail::Mail _Mail;
        _Mail.Header.nMailId = nMailID_;
        _Mail.Header.nTypeCode = nTypeCode_;

        if (Payload_)
        {
            auto _Bytes = EncodeApplicationHostPayload(*Payload_);
            if (!_Bytes.empty())
            {
                _Mail.Payload.nSize = _Bytes.size();
                _Mail.Payload.pData = new uint8_t[_Mail.Payload.nSize];
                std::memcpy(_Mail.Payload.pData, _Bytes.data(), _Mail.Payload.nSize);
            }
        }

        return _Mail;
    }

    iCAX::Mail::Mail MakeProductRequestMail(
        IN uint64_t nMailID_,
        IN uint64_t nTypeCode_,
        IN const std::optional<iCAX::Data::Variant>& Payload_ = std::nullopt)
    {
        iCAX::Mail::Mail _Mail;
        _Mail.Header.nMailId = nMailID_;
        _Mail.Header.nTypeCode = nTypeCode_;

        if (Payload_)
        {
            auto _Bytes = iCAX::Product::EncodeProductPayload(*Payload_);
            if (!_Bytes.empty())
            {
                _Mail.Payload.nSize = _Bytes.size();
                _Mail.Payload.pData = new uint8_t[_Mail.Payload.nSize];
                std::memcpy(_Mail.Payload.pData, _Bytes.data(), _Mail.Payload.nSize);
            }
        }

        return _Mail;
    }

    std::vector<iCAX::Mail::Mail> WaitForMails(IN const iCAX::Mail::CMailPostOffice& PostOffice_)
    {
        for (int _Index = 0; _Index < 200; ++_Index)
        {
            auto _Mails = PostOffice_.Receive();
            if (!_Mails.empty())
            {
                return _Mails;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return {};
    }

    iCAX::Data::ObjectMap DecodeApplicationObjectPayload(IN const iCAX::Mail::Mail& Mail_)
    {
        std::vector<uint8_t> _Payload;
        if (Mail_.Payload.nSize > 0)
        {
            _Payload.assign(Mail_.Payload.pData, Mail_.Payload.pData + Mail_.Payload.nSize);
        }

        auto _Variant = DecodeApplicationHostPayload(_Payload);
        if (!_Variant.Is<iCAX::Data::ObjectMap>())
        {
            throw std::runtime_error("Expected object payload");
        }
        return _Variant.To<iCAX::Data::ObjectMap>();
    }

    iCAX::Data::ObjectMap DecodeProductObjectPayload(IN const iCAX::Mail::Mail& Mail_)
    {
        std::vector<uint8_t> _Payload;
        if (Mail_.Payload.nSize > 0)
        {
            _Payload.assign(Mail_.Payload.pData, Mail_.Payload.pData + Mail_.Payload.nSize);
        }

        auto _Variant = iCAX::Product::DecodeProductPayload(_Payload);
        if (!_Variant.Is<iCAX::Data::ObjectMap>())
        {
            throw std::runtime_error("Expected object payload");
        }
        return _Variant.To<iCAX::Data::ObjectMap>();
    }

    ApplicationHostConfig MakeTwoProductConfig()
    {
        ApplicationHostConfig _Config;
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
}

TEST(ApplicationHostMailboxTest, ApplicationMailboxReturnsProductListBeforeProductIsStarted)
{
    CApplicationHost _Host;
    _Host.Start();

    auto _FrontendPostOffice = _Host.GetApplicationFrontendPostOffice();
    auto _Request = MakeApplicationRequestMail(1001, kAppGetStateCommand);
    SendMail(_FrontendPostOffice, _Request);

    auto _Responses = WaitForMails(_FrontendPostOffice);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _Responses[0].Header.nStamp);
    EXPECT_EQ(1001u, _Responses[0].Header.nOriginId);
    EXPECT_EQ(kAppGetStateCommand, _Responses[0].Header.nTypeCode);

    auto _State = DecodeApplicationObjectPayload(_Responses[0]);
    ASSERT_TRUE(_State.contains("applicationChannelId"));
    ASSERT_TRUE(_State.contains("productCount"));
    ASSERT_TRUE(_State.contains("products"));
    EXPECT_EQ(_Host.GetApplicationChannelID(), _State.at("applicationChannelId").To<iCAX::Data::uuid>());
    EXPECT_EQ("Running", _State.at("state").To<std::string>());
    EXPECT_EQ(1ull, _State.at("productCount").To<unsigned long long>());
    EXPECT_EQ(0ull, _State.at("runningProductCount").To<unsigned long long>());

    auto _Products = _State.at("products").To<iCAX::Data::VariantArray>();
    ASSERT_EQ(1u, _Products.size());
    auto _Product = _Products[0].To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("icax.default", _Product.at("productId").To<std::string>());
    EXPECT_FALSE(_Product.at("isStarted").To<bool>());

    ReleaseMailPayloads(_Responses);
    _Host.Stop();
}

TEST(ApplicationHostMailboxTest, ApplicationFrontendPostOfficeIsAvailableAfterStart)
{
    CApplicationHost _Host;
    EXPECT_THROW(_Host.GetApplicationFrontendPostOffice(), std::logic_error);

    _Host.Start();
    auto _FrontendPostOffice = _Host.GetApplicationFrontendPostOffice();
    ASSERT_TRUE(_FrontendPostOffice.IsValid());

    auto _Request = MakeApplicationRequestMail(1501, kAppGetStateCommand);
    SendMail(_FrontendPostOffice, _Request);

    auto _Responses = WaitForMails(_FrontendPostOffice);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _Responses[0].Header.nStamp);
    EXPECT_EQ(1501u, _Responses[0].Header.nOriginId);

    ReleaseMailPayloads(_Responses);
    _Host.Stop();
}

TEST(ApplicationHostMailboxTest, ApplicationHostCanSendFrontendEvent)
{
    constexpr uint64_t kStateChangedEvent = iCAX::Command::MakeCommandCode("App", "StateChanged");

    CApplicationHost _Host;
    _Host.Start();

    auto _FrontendPostOffice = _Host.GetApplicationFrontendPostOffice();
    _Host.SendFrontendEvent(kStateChangedEvent, "application-event");

    auto _Events = WaitForMails(_FrontendPostOffice);
    ASSERT_EQ(1u, _Events.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _Events[0].Header.nStamp);
    EXPECT_EQ(0u, _Events[0].Header.nOriginId);
    EXPECT_EQ(kStateChangedEvent, _Events[0].Header.nTypeCode);
    EXPECT_EQ("application-event", iCAX::Mail::GetMailPayloadText(_Events[0]));

    ReleaseMailPayloads(_Events);
    _Host.Stop();
}

TEST(ApplicationHostMailboxTest, ApplicationMailboxCanStartProduct)
{
    CApplicationHost _Host;
    _Host.Start();

    auto _FrontendPostOffice = _Host.GetApplicationFrontendPostOffice();
    auto _Request = MakeApplicationRequestMail(2001, kAppStartProductCommand);
    SendMail(_FrontendPostOffice, _Request);

    auto _Responses = WaitForMails(_FrontendPostOffice);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _Responses[0].Header.nStamp);

    auto _Payload = DecodeApplicationObjectPayload(_Responses[0]);
    auto _Product = _Payload.at("product").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("icax.default", _Product.at("productId").To<std::string>());
    EXPECT_TRUE(_Product.at("isStarted").To<bool>());
    EXPECT_FALSE(_Product.at("productChannelId").To<iCAX::Data::uuid>().is_nil());
    ASSERT_NE(nullptr, _Host.FindProductRuntime("icax.default"));
    EXPECT_TRUE(_Host.GetProductFrontendPostOffice("icax.default").IsValid());

    auto _State = _Payload.at("state").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ(1ull, _State.at("runningProductCount").To<unsigned long long>());
    ReleaseMailPayloads(_Responses);
    _Host.Stop();
}

TEST(ApplicationHostMailboxTest, ProductMailboxCanOpenAndCloseProjectCatalogAfterProductStarts)
{
    CApplicationHost _Host;
    _Host.Start();
    auto _pRuntime = _Host.StartProduct();
    ASSERT_NE(nullptr, _pRuntime);

    auto _ProductPostOffice = _Host.GetProductFrontendPostOffice(_pRuntime->GetProductID());

    iCAX::Data::ObjectMap _OpenPayload;
    _OpenPayload["catalogName"] = std::string("Robot Catalog");
    _OpenPayload["catalogPath"] = std::string("memory://robot-catalog");
    _OpenPayload["projectName"] = std::string("Robot Cell");
    _OpenPayload["projectPath"] = std::string("memory://robot-cell");
    auto _OpenRequest = MakeProductRequestMail(
        3001,
        iCAX::Product::kProductOpenProjectCatalogCommand,
        iCAX::Data::Variant(_OpenPayload));
    SendMail(_ProductPostOffice, _OpenRequest);

    auto _OpenResponses = WaitForMails(_ProductPostOffice);
    ASSERT_EQ(1u, _OpenResponses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _OpenResponses[0].Header.nStamp);

    auto _OpenObject = DecodeProductObjectPayload(_OpenResponses[0]);
    auto _Catalog = _OpenObject.at("catalog").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("Robot Catalog", _Catalog.at("catalogName").To<std::string>());
    EXPECT_TRUE(_Catalog.at("hasMainProject").To<bool>());

    auto _Project = _Catalog.at("mainProject").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("Robot Cell", _Project.at("projectName").To<std::string>());
    EXPECT_EQ("Running", _Project.at("state").To<std::string>());
    EXPECT_FALSE(_Project.at("projectChannelId").To<iCAX::Data::uuid>().is_nil());
    const auto _ProjectID = _Project.at("projectId").To<iCAX::Data::uuid>();
    const auto _CatalogID = _Catalog.at("catalogId").To<iCAX::Data::uuid>();
    EXPECT_TRUE(_Host.GetProjectFrontendPostOffice(_ProjectID).IsValid());
    ASSERT_NE(nullptr, _pRuntime->FindProjectCatalog(_CatalogID));
    ReleaseMailPayloads(_OpenResponses);

    iCAX::Data::ObjectMap _ClosePayload;
    _ClosePayload["catalogId"] = _CatalogID;
    auto _CloseRequest = MakeProductRequestMail(
        3002,
        iCAX::Product::kProductCloseProjectCatalogCommand,
        iCAX::Data::Variant(_ClosePayload));
    SendMail(_ProductPostOffice, _CloseRequest);

    auto _CloseResponses = WaitForMails(_ProductPostOffice);
    ASSERT_EQ(1u, _CloseResponses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _CloseResponses[0].Header.nStamp);
    auto _CloseState = DecodeProductObjectPayload(_CloseResponses[0]);
    EXPECT_EQ(0ull, _CloseState.at("catalogCount").To<unsigned long long>());
    EXPECT_EQ(nullptr, _pRuntime->FindProjectCatalog(_CatalogID));
    EXPECT_THROW(_Host.GetProjectFrontendPostOffice(_ProjectID), std::runtime_error);
    ReleaseMailPayloads(_CloseResponses);

    _Host.Stop();
}

TEST(ApplicationHostMailboxTest, ApplicationHostCanStartMultipleConfiguredProducts)
{
    CApplicationHost _Host;
    _Host.SetConfig(MakeTwoProductConfig());
    _Host.Start();

    auto _ApplicationPostOffice = _Host.GetApplicationFrontendPostOffice();

    iCAX::Data::ObjectMap _RobotPayload;
    _RobotPayload["productId"] = std::string("robot");
    SendMail(_ApplicationPostOffice, MakeApplicationRequestMail(
        4001,
        kAppStartProductCommand,
        iCAX::Data::Variant(_RobotPayload)));
    auto _RobotResponses = WaitForMails(_ApplicationPostOffice);
    ASSERT_EQ(1u, _RobotResponses.size());
    auto _Robot = DecodeApplicationObjectPayload(_RobotResponses[0]).at("product").To<iCAX::Data::ObjectMap>();
    ReleaseMailPayloads(_RobotResponses);

    iCAX::Data::ObjectMap _WeldPayload;
    _WeldPayload["productId"] = std::string("weld");
    SendMail(_ApplicationPostOffice, MakeApplicationRequestMail(
        4002,
        kAppStartProductCommand,
        iCAX::Data::Variant(_WeldPayload)));
    auto _WeldResponses = WaitForMails(_ApplicationPostOffice);
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
    ASSERT_NE(nullptr, _Host.FindProductRuntime("robot"));
    ASSERT_NE(nullptr, _Host.FindProductRuntime("weld"));
    ReleaseMailPayloads(_WeldResponses);

    _Host.Stop();
}

TEST(ApplicationHostMailboxTest, StartProductReturnsSingleRuntimeAcrossConcurrentCallers)
{
    CApplicationHost _Host;
    _Host.SetConfig(MakeTwoProductConfig());
    _Host.Start();

    constexpr int kThreadCount = 8;
    std::array<std::shared_ptr<iCAX::Product::CProductRuntime>, kThreadCount> _Runtimes;
    std::array<std::exception_ptr, kThreadCount> _Exceptions;
    std::vector<std::thread> _Threads;

    for (int _Index = 0; _Index < kThreadCount; ++_Index)
    {
        _Threads.emplace_back([&, _Index]() {
            try
            {
                _Runtimes[_Index] = _Host.StartProduct("robot");
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

    EXPECT_EQ(1u, _Host.GetProductRuntimes().size());
    EXPECT_EQ(_Runtimes[0], _Host.FindProductRuntime("robot"));
    EXPECT_TRUE(_Host.GetProductFrontendPostOffice("robot").IsValid());

    _Host.Stop();
}

TEST(ApplicationHostMailboxTest, StopProductThenStartProductCreatesFreshRuntimeAndRegistries)
{
    CApplicationHost _Host;
    _Host.SetConfig(MakeTwoProductConfig());
    _Host.Start();

    auto _pFirstRuntime = _Host.StartProduct("robot");
    ASSERT_NE(nullptr, _pFirstRuntime);
    const auto _FirstProductChannelID = _pFirstRuntime->GetProductChannelID();
    auto _FirstPostOffice = _Host.GetProductFrontendPostOffice("robot");
    ASSERT_TRUE(_FirstPostOffice.IsValid());

    constexpr uint64_t kReopenProbeCommand = iCAX::Command::MakeCommandCode("ReopenProbe", "Ping");
    const auto _ReopenProbeMainCode = iCAX::Command::GetCommandMainCode(kReopenProbeCommand);
    ASSERT_TRUE(_pFirstRuntime->GetCommandRegistry().Register(std::make_shared<CReopenProbeCommandTarget>()));
    EXPECT_TRUE(_pFirstRuntime->GetCommandRegistry().Has(_ReopenProbeMainCode));
    _pFirstRuntime->GetMetaRegistry().RegistType(
        "ReopenProbe.Component",
        iCAX::Database::CComponentBase::S_ClassName);
    EXPECT_TRUE(_pFirstRuntime->GetMetaRegistry().HasTypeByName("ReopenProbe.Component"));

    EXPECT_TRUE(_Host.StopProduct("robot"));
    EXPECT_EQ(nullptr, _Host.FindProductRuntime("robot"));
    EXPECT_FALSE(_FirstPostOffice.IsValid());
    EXPECT_FALSE(_pFirstRuntime->IsStarted());

    auto _pSecondRuntime = _Host.StartProduct("robot");
    ASSERT_NE(nullptr, _pSecondRuntime);
    EXPECT_NE(_pFirstRuntime, _pSecondRuntime);
    EXPECT_TRUE(_pSecondRuntime->IsStarted());
    EXPECT_NE(_FirstProductChannelID, _pSecondRuntime->GetProductChannelID());
    EXPECT_TRUE(_Host.GetProductFrontendPostOffice("robot").IsValid());
    EXPECT_FALSE(_pSecondRuntime->GetCommandRegistry().Has(_ReopenProbeMainCode));
    EXPECT_FALSE(_pSecondRuntime->GetMetaRegistry().HasTypeByName("ReopenProbe.Component"));

    _Host.Stop();
}

TEST(ApplicationHostMailboxTest, StartProductIsRejectedAfterHostStops)
{
    CApplicationHost _Host;
    _Host.SetConfig(MakeTwoProductConfig());
    _Host.Start();
    _Host.Stop();

    EXPECT_THROW(_Host.StartProduct("robot"), std::logic_error);
}

TEST(ApplicationHostMailboxTest, ProductDefinitionRequiresProjectFileMagic)
{
    auto _Config = MakeTwoProductConfig();
    _Config.Products.front().ProjectFile.Magic.clear();

    CApplicationHost _Host;
    EXPECT_THROW(_Host.SetConfig(_Config), std::invalid_argument);
}

TEST(ApplicationHostMailboxTest, ProductDefinitionRequiresSafeProductID)
{
    auto _Config = MakeTwoProductConfig();
    _Config.Products.front().ProductID = "../robot";

    CApplicationHost _Host;
    EXPECT_THROW(_Host.SetConfig(_Config), std::invalid_argument);
}

TEST(ApplicationHostMailboxTest, ProductDefinitionRequiresUniqueProjectFileMagic)
{
    auto _Config = MakeTwoProductConfig();
    _Config.Products.back().ProjectFile.Magic = _Config.Products.front().ProjectFile.Magic;

    CApplicationHost _Host;
    EXPECT_THROW(_Host.SetConfig(_Config), std::invalid_argument);
}

TEST(ApplicationHostMailboxTest, ApplicationMailboxCanResolveAndOpenProjectFile)
{
    auto _Root = std::filesystem::current_path()
        / "Temp"
        / ("ApplicationHostOpenProjectFileTest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(_Root);
    const auto _ProjectPath = _Root / "RobotCell.robot";
    {
        std::ofstream _Output(_ProjectPath, std::ios::binary | std::ios::trunc);
        _Output << "ICAX_ROBOT\nproject-body";
    }

    auto _Config = MakeTwoProductConfig();
    _Config.Paths.UserConfigDirectory = (_Root / "Setting").string();

    CApplicationHost _Host;
    _Host.SetConfig(_Config);
    _Host.Start();

    auto _ApplicationPostOffice = _Host.GetApplicationFrontendPostOffice();

    iCAX::Data::ObjectMap _ResolvePayload;
    _ResolvePayload["projectPath"] = _ProjectPath.string();
    SendMail(_ApplicationPostOffice, MakeApplicationRequestMail(
        6001,
        kAppResolveProjectFileCommand,
        iCAX::Data::Variant(_ResolvePayload)));

    auto _ResolveResponses = WaitForMails(_ApplicationPostOffice);
    ASSERT_EQ(1u, _ResolveResponses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _ResolveResponses[0].Header.nStamp);
    auto _ResolveResponse = DecodeApplicationObjectPayload(_ResolveResponses[0]);
    auto _Resolve = _ResolveResponse.at("resolve").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("Resolved", _Resolve.at("status").To<std::string>());
    EXPECT_EQ("robot", _Resolve.at("productId").To<std::string>());
    EXPECT_TRUE(_Resolve.at("matchedByMagic").To<bool>());
    ReleaseMailPayloads(_ResolveResponses);

    iCAX::Data::ObjectMap _OpenPayload;
    _OpenPayload["projectPath"] = _ProjectPath.string();
    SendMail(_ApplicationPostOffice, MakeApplicationRequestMail(
        6002,
        kAppOpenProjectFileCommand,
        iCAX::Data::Variant(_OpenPayload)));

    auto _OpenResponses = WaitForMails(_ApplicationPostOffice);
    ASSERT_EQ(1u, _OpenResponses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _OpenResponses[0].Header.nStamp);

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
    ASSERT_NE(nullptr, _Host.FindProductRuntime("robot"));

    ReleaseMailPayloads(_OpenResponses);
    _Host.Stop();
    std::filesystem::remove_all(_Root);
}

TEST(ApplicationHostMailboxTest, ApplicationHostCanOpenProjectFileDirectly)
{
    auto _Root = std::filesystem::current_path()
        / "Temp"
        / ("ApplicationHostDirectOpenProjectFileTest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(_Root);
    const auto _ProjectPath = _Root / "DirectRobot.robot";
    {
        std::ofstream _Output(_ProjectPath, std::ios::binary | std::ios::trunc);
        _Output << "ICAX_ROBOT\nproject-body";
    }

    auto _Config = MakeTwoProductConfig();
    _Config.Paths.UserConfigDirectory = (_Root / "Setting").string();

    CApplicationHost _Host;
    _Host.SetConfig(_Config);
    _Host.Start();

    auto _Resolve = _Host.ResolveProjectFileProduct(_ProjectPath.string());
    EXPECT_EQ(EProductFileResolveStatus::Resolved, _Resolve.Status);
    EXPECT_EQ("robot", _Resolve.ProductID);

    auto _pCatalog = _Host.OpenProjectFile(_ProjectPath.string());
    ASSERT_NE(nullptr, _pCatalog);
    EXPECT_EQ("DirectRobot", _pCatalog->GetCatalogName());
    EXPECT_EQ(_ProjectPath.string(), _pCatalog->GetCatalogPath());
    ASSERT_NE(nullptr, _Host.FindProductRuntime("robot"));
    ASSERT_NE(nullptr, _pCatalog->GetMainProject());
    EXPECT_EQ(_ProjectPath.string(), _pCatalog->GetMainProject()->GetProjectPath());

    _Host.Stop();
    std::filesystem::remove_all(_Root);
}

TEST(ApplicationHostMailboxTest, ApplicationCommandSentToProductMailboxHasNoHandler)
{
    CApplicationHost _Host;
    _Host.Start();
    auto _pRuntime = _Host.StartProduct();
    ASSERT_NE(nullptr, _pRuntime);

    auto _ProductPostOffice = _Host.GetProductFrontendPostOffice(_pRuntime->GetProductID());
    auto _Request = MakeApplicationRequestMail(5001, kAppGetStateCommand);
    SendMail(_ProductPostOffice, _Request);

    auto _Responses = WaitForMails(_ProductPostOffice);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Mail::kMailNoHandler, _Responses[0].Header.nStamp);
    EXPECT_EQ(5001u, _Responses[0].Header.nOriginId);

    ReleaseMailPayloads(_Responses);
    _Host.Stop();
}
