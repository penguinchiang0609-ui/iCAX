#include <gtest/gtest.h>

#include <ApplicationContext/ApplicationContext.h>
#include <Behaviour/IBehaviourRegistry.h>
#include <CommandHandler/CommandRoute.h>
#include <CommandHandler/CommandTarget.h>
#include <Database/IMetaRegistry.h>
#include <Product/ProductCommands.h>
#include <Product/ProductRuntime.h>
#include <Project/ProjectRuntime.h>
#include <Resources/ResourceLoaderRegistry.h>
#include <Mailbox/MailChannelRegistry.h>
#include <Services/ServiceProvider.h>
#include <Data/Variant.h>
#include <Mailbox/Mail.h>
#include <Mailbox/MailPayload.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace iCAX::Product;

namespace
{
    class CTestProductCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        explicit CTestProductCommandTarget(IN std::string strMainName_)
            : CCommandTarget(std::move(strMainName_))
        {
        }

        using CCommandTarget::Bind;
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

    iCAX::Mail::Mail MakeRequestMail(
        IN uint64_t nMailID_,
        IN uint64_t nTypeCode_,
        IN const std::optional<iCAX::Data::Variant>& Payload_ = std::nullopt)
    {
        iCAX::Mail::Mail _Mail;
        _Mail.Header.nMailId = nMailID_;
        _Mail.Header.nTypeCode = nTypeCode_;

        if (Payload_)
        {
            auto _Bytes = EncodeProductPayload(*Payload_);
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

    std::vector<iCAX::Mail::Mail> WaitForProductMails(
        IN const std::shared_ptr<CProductRuntime>& pRuntime_,
        IN const iCAX::Mail::CMailPostOffice& PostOffice_)
    {
        for (int _Index = 0; _Index < 200; ++_Index)
        {
            pRuntime_->DispatchProductMails();
            auto _Mails = PostOffice_.Receive();
            if (!_Mails.empty())
            {
                return _Mails;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return {};
    }

    std::optional<iCAX::Project::CProjectFault> WaitForProjectFault(
        IN const std::shared_ptr<iCAX::Project::CProject>& pProject_)
    {
        for (int _Index = 0; _Index < 200; ++_Index)
        {
            auto _Fault = pProject_->GetLastFault();
            if (_Fault)
            {
                return _Fault;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return std::nullopt;
    }

    iCAX::Data::ObjectMap DecodeObjectPayload(IN const iCAX::Mail::Mail& Mail_)
    {
        std::vector<uint8_t> _Payload;
        if (Mail_.Payload.nSize > 0)
        {
            _Payload.assign(Mail_.Payload.pData, Mail_.Payload.pData + Mail_.Payload.nSize);
        }

        auto _Variant = DecodeProductPayload(_Payload);
        if (!_Variant.Is<iCAX::Data::ObjectMap>())
        {
            throw std::runtime_error("Expected object payload");
        }
        return _Variant.To<iCAX::Data::ObjectMap>();
    }

    std::shared_ptr<CProductRuntime> MakeRuntime(IN const std::string& strProductID_ = "robot")
    {
        iCAX::Application::CApplicationDescriptor _Descriptor;
        _Descriptor.AppID = "icax-test";
        _Descriptor.AppName = "iCAX Test";

        iCAX::Application::CApplicationPaths _Paths;
        _Paths.InstallDirectory = ".";

        iCAX::Application::CApplicationSettings _Settings;
        auto _pContext = std::make_shared<iCAX::Application::CApplicationContext>(_Descriptor, _Paths, _Settings);
        auto _pServiceProvider = std::make_shared<iCAX::Services::CServiceProvider>();
        auto _pMailChannelRegistry = std::make_shared<iCAX::Mail::CMailChannelRegistry>();

        CProductDefinition _Definition;
        _Definition.ProductID = strProductID_;
        _Definition.ProductName = "Robot Product";
        _Definition.ProductVersion = "1.0";
        _Definition.FrontendEntry = "h5://robot/index.html";
        _Definition.ProjectFile.Magic = "ICAX_ROBOT";
        _Definition.ProjectFile.FormatVersion = "1.0";
        _Definition.ProjectFile.FileExtensions.push_back(".robot");

        auto _pProductDataStore = std::make_shared<CMemoryProductDataStore>();
        CProductData _ProductData;
        _ProductData.RecentProjects.push_back(CRecentProjectItem{
            "D:/projects/recent-robot.robot",
            "Recent Robot",
            "2026-06-20T00:00:00Z" });
        _pProductDataStore->Save(strProductID_, _ProductData);

        return std::make_shared<CProductRuntime>(
            _Definition,
            _pContext,
            _pServiceProvider,
            _pMailChannelRegistry,
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
    _ProductData.UserSettings.Set("theme", std::string("dark"));

    _Store.Save("robot", _ProductData);

    auto _Loaded = _Store.Load("robot");
    ASSERT_EQ(1u, _Loaded.RecentProjects.size());
    EXPECT_EQ("D:/projects/robot.robot", _Loaded.RecentProjects[0].ProjectPath);
    EXPECT_EQ("Robot Cell", _Loaded.RecentProjects[0].DisplayName);
    EXPECT_EQ("2026-06-20T00:00:00Z", _Loaded.RecentProjects[0].LastOpenedTime);
    EXPECT_EQ("dark", _Loaded.UserSettings.Get("theme", iCAX::Data::Variant()).To<std::string>());

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
    EXPECT_TRUE(_pRuntime->GetProjectFrontendPostOffice(_pProject->GetProjectID()).IsValid());

    EXPECT_TRUE(_pRuntime->CloseProjectCatalog(_pCatalog->GetCatalogID()));
    EXPECT_EQ(nullptr, _pRuntime->FindProjectCatalog(_pCatalog->GetCatalogID()));
    EXPECT_THROW(_pRuntime->GetProjectFrontendPostOffice(_pProject->GetProjectID()), std::runtime_error);

    _pRuntime->Stop();
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

TEST(ProductRuntimeMailboxTest, ProductMailboxCanOpenAndListProjectCatalogs)
{
    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();
    auto _FrontendPostOffice = _pRuntime->GetProductFrontendPostOffice();

    iCAX::Data::ObjectMap _OpenPayload;
    _OpenPayload["catalogName"] = std::string("Robot Catalog");
    _OpenPayload["catalogPath"] = std::string("memory://robot-catalog");
    _OpenPayload["projectName"] = std::string("Robot Cell");
    _OpenPayload["projectPath"] = std::string("memory://robot-cell");
    auto _OpenRequest = MakeRequestMail(2001, kProductOpenProjectCatalogCommand, iCAX::Data::Variant(_OpenPayload));
    _FrontendPostOffice.Send(_OpenRequest);
    ReleaseTestMailPayload(_OpenRequest);

    auto _OpenResponses = WaitForProductMails(_pRuntime, _FrontendPostOffice);
    ASSERT_EQ(1u, _OpenResponses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _OpenResponses[0].Header.nStamp);

    auto _OpenPayloadObject = DecodeObjectPayload(_OpenResponses[0]);
    ASSERT_TRUE(_OpenPayloadObject.contains("catalog"));
    auto _Catalog = _OpenPayloadObject.at("catalog").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("Robot Catalog", _Catalog.at("catalogName").To<std::string>());
    EXPECT_TRUE(_Catalog.at("hasMainProject").To<bool>());
    auto _MainProject = _Catalog.at("mainProject").To<iCAX::Data::ObjectMap>();
    EXPECT_FALSE(_MainProject.at("projectChannelId").To<iCAX::Data::uuid>().is_nil());

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
    ReleaseMailPayloads(_OpenResponses);

    iCAX::Data::ObjectMap _ClosePayload;
    _ClosePayload["catalogId"] = _CatalogID;
    auto _CloseRequest = MakeRequestMail(2002, kProductCloseProjectCatalogCommand, iCAX::Data::Variant(_ClosePayload));
    _FrontendPostOffice.Send(_CloseRequest);
    ReleaseTestMailPayload(_CloseRequest);

    auto _CloseResponses = WaitForProductMails(_pRuntime, _FrontendPostOffice);
    ASSERT_EQ(1u, _CloseResponses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _CloseResponses[0].Header.nStamp);

    auto _CloseState = DecodeObjectPayload(_CloseResponses[0]);
    EXPECT_EQ(0ull, _CloseState.at("catalogCount").To<unsigned long long>());
    EXPECT_TRUE(_CloseState.at("catalogs").To<iCAX::Data::VariantArray>().empty());
    ReleaseMailPayloads(_CloseResponses);

    _pRuntime->Stop();
    EXPECT_FALSE(_FrontendPostOffice.IsValid());
}

TEST(ProductRuntimeMailboxTest, ProductRuntimeCanSendFrontendEvent)
{
    constexpr uint64_t kProductStateChangedEvent = iCAX::Command::MakeCommandCode("Product", "StateChanged");

    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();
    auto _FrontendPostOffice = _pRuntime->GetProductFrontendPostOffice();

    _pRuntime->SendFrontendEvent(kProductStateChangedEvent, "product-event");

    auto _Events = WaitForMails(_FrontendPostOffice);
    ASSERT_EQ(1u, _Events.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _Events[0].Header.nStamp);
    EXPECT_EQ(0u, _Events[0].Header.nOriginId);
    EXPECT_EQ(kProductStateChangedEvent, _Events[0].Header.nTypeCode);
    EXPECT_EQ("product-event", iCAX::Mail::GetMailPayloadText(_Events[0]));

    ReleaseMailPayloads(_Events);
    _pRuntime->Stop();
}

TEST(ProductRuntimeMailboxTest, ProductCommandSentToProjectMailboxIsRejected)
{
    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();
    auto _pCatalog = _pRuntime->OpenProjectCatalog("Project Context", {}, "Project Context");
    ASSERT_NE(nullptr, _pCatalog);
    auto _pProject = _pCatalog->GetMainProject();
    ASSERT_NE(nullptr, _pProject);

    auto _ProjectPostOffice = _pRuntime->GetProjectFrontendPostOffice(_pProject->GetProjectID());
    auto _Request = MakeRequestMail(3001, kProductGetStateCommand);
    _ProjectPostOffice.Send(_Request);
    ReleaseTestMailPayload(_Request);

    auto _Fault = WaitForProjectFault(_pProject);
    ASSERT_TRUE(_Fault.has_value());
    EXPECT_NE(std::string::npos, _Fault->Message.find("Product command must be sent to the product mailbox"));
    EXPECT_TRUE(_ProjectPostOffice.Receive().empty());
    _pRuntime->Stop();
}

TEST(ProductRuntimeMailboxTest, ProjectMailboxProvidesProjectContext)
{
    constexpr uint64_t kInspectProjectContextCommand =
        iCAX::Command::MakeCommandCode("InspectProject", "ProjectContext");

    auto _pRuntime = MakeRuntime();
    _pRuntime->Start();
    auto _pCatalog = _pRuntime->OpenProjectCatalog("Project Context", {}, "Project Context");
    ASSERT_NE(nullptr, _pCatalog);
    auto _pProject = _pCatalog->GetMainProject();
    ASSERT_NE(nullptr, _pProject);

    const auto _ProjectID = _pProject->GetProjectID();
    auto _pCommandTarget = std::make_shared<CTestProductCommandTarget>("InspectProject");
    _pCommandTarget->Bind(
        "ProjectContext",
        [_ProjectID](
            IN const iCAX::Command::CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_) {
            iCAX::Data::ObjectMap _Payload;
            _Payload["projectId"] = pProjectContext_ ? pProjectContext_->GetProjectID() : iCAX::Data::GenerateNilUUID();
            _Payload["projectChannelId"] = pProjectContext_ ? pProjectContext_->GetProjectChannelID() : iCAX::Data::GenerateNilUUID();
            _Payload["projectName"] = pProjectContext_ ? pProjectContext_->GetProjectName() : std::string();
            _Payload["hasProductContext"] = pProductContext_ != nullptr;
            _Payload["hasProjectContext"] = pProjectContext_ != nullptr;
            _Payload["sameProject"] = pProjectContext_
                && pProjectContext_->GetProjectID() == _ProjectID;

            iCAX::Command::CCommandResponse _Response;
            _Response.Payload = EncodeProductPayload(iCAX::Data::Variant(_Payload));
            return _Response;
        });
    ASSERT_TRUE(_pRuntime->GetCommandRegistry().Register(_pCommandTarget));

    auto _ProjectPostOffice = _pRuntime->GetProjectFrontendPostOffice(_ProjectID);
    auto _Request = MakeRequestMail(4001, kInspectProjectContextCommand);
    _ProjectPostOffice.Send(_Request);
    ReleaseTestMailPayload(_Request);

    auto _Responses = WaitForMails(_ProjectPostOffice);
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Mail::kMailOk, _Responses[0].Header.nStamp);

    auto _Payload = DecodeObjectPayload(_Responses[0]);
    EXPECT_EQ(_ProjectID, _Payload.at("projectId").To<iCAX::Data::uuid>());
    EXPECT_FALSE(_Payload.at("projectChannelId").To<iCAX::Data::uuid>().is_nil());
    EXPECT_EQ("Project Context", _Payload.at("projectName").To<std::string>());
    EXPECT_TRUE(_Payload.at("hasProductContext").To<bool>());
    EXPECT_TRUE(_Payload.at("hasProjectContext").To<bool>());
    EXPECT_TRUE(_Payload.at("sameProject").To<bool>());

    ReleaseMailPayloads(_Responses);
    _pRuntime->Stop();
}

