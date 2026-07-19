#include "pch.h"


#include "Data/PropertyBag.h"
#include "Data/uuid.h"
#include "InputPDO/InputPDO.h"
#include "InputService/InputService.h"
#include "PDO/IPDOHub.h"
#include "PDO/PDOLease.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"


namespace
{
    class CFakeProjectContext final : public iCAX::Project::IProjectContext
    {
    public:
        CFakeProjectContext()
            : m_ProjectID(iCAX::Data::GenerateNewUUID())
            , m_strProjectName("input-service-test")
        {
        }

        const iCAX::Data::uuid& GetProjectID() const override
        {
            return m_ProjectID;
        }

        const std::string& GetProjectName() const override
        {
            return m_strProjectName;
        }

        const std::string& GetProjectPath() const override
        {
            return m_strProjectPath;
        }

        iCAX::Data::PropertyBag& Settings() override
        {
            return m_Settings;
        }

        const iCAX::Data::PropertyBag& Settings() const override
        {
            return m_Settings;
        }

    private:
        iCAX::Data::uuid m_ProjectID;
        std::string m_strProjectName;
        std::string m_strProjectPath;
        iCAX::Data::PropertyBag m_Settings;
    };

    class CFakeSceneContext final : public iCAX::Project::ISceneContext
    {
    public:
        explicit CFakeSceneContext(IN std::shared_ptr<iCAX::PDO::IPDOHub> pPDOHub_)
            : m_SceneID(iCAX::Data::GenerateNewUUID())
            , m_SceneChannelID(iCAX::Data::GenerateNewUUID())
            , m_pPDOHub(std::move(pPDOHub_))
        {
        }

        const iCAX::Data::uuid& GetSceneID() const override
        {
            return m_SceneID;
        }

        const iCAX::Data::uuid& GetSceneChannelID() const override
        {
            return m_SceneChannelID;
        }

        const std::string& GetSceneName() const override
        {
            return m_strSceneName;
        }

        iCAX::Interaction::CFacadeEndpoint GetBackendFacadeEndpoint() const override
        {
            return {};
        }

        iCAX::Interaction::CFacadeEndpoint GetFrontendFacadeEndpoint() const override
        {
            return {};
        }

        bool IsMainScene() const override
        {
            return true;
        }

        bool IsTransientScene() const override
        {
            return false;
        }

        iCAX::Database::IRepository& Database() override
        {
            throw std::logic_error("Database is not used by InputService tests");
        }

        const iCAX::Database::IRepository& Database() const override
        {
            throw std::logic_error("Database is not used by InputService tests");
        }

        iCAX::Resource::CResourceLibrary& Resources() override
        {
            throw std::logic_error("Resources are not used by InputService tests");
        }

        const iCAX::Resource::CResourceLibrary& Resources() const override
        {
            throw std::logic_error("Resources are not used by InputService tests");
        }

        bool HasPDOHub() const override
        {
            return m_pPDOHub != nullptr;
        }

        iCAX::PDO::IPDOHub& PDOHub() override
        {
            return *m_pPDOHub;
        }

        const iCAX::PDO::IPDOHub& PDOHub() const override
        {
            return *m_pPDOHub;
        }

        iCAX::Services::CServiceProvider& Services() const override
        {
            throw std::logic_error("Services are not used by InputService tests");
        }

    private:
        iCAX::Data::uuid m_SceneID;
        iCAX::Data::uuid m_SceneChannelID;
        std::string m_strSceneName = "input-scene";
        std::shared_ptr<iCAX::PDO::IPDOHub> m_pPDOHub;
    };

    std::shared_ptr<iCAX::Input::IInputService> CreateInputService()
    {
        EXPECT_EQ(1u, iCAX::Input::GetInputServiceContractVersion());

        iCAX::Services::CServiceProvider _Provider;
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(_Provider);
        return _Provider.Resolve<iCAX::Input::IInputService>();
    }

    std::shared_ptr<iCAX::PDO::IPDOHub> CreatePDOHub()
    {
        iCAX::PDO::CPDOHubCreateInfo _CreateInfo;
        _CreateInfo.nArenaSize = 1024 * 1024;
        _CreateInfo.nSlotCapacity = 16;
        return iCAX::PDO::GeneratePDOHub(_CreateInfo);
    }

    void WriteInputState(
        IN iCAX::PDO::IPDOHub& Hub_,
        IN iCAX::InputPDO::InputDataVersion nDataVersion_,
        IN iCAX::InputPDO::InputViewportID nViewportID_,
        IN bool bWDown_,
        IN bool bDDown_,
        IN float nDeltaX_,
        IN float nWheelY_)
    {
        const auto _Decl = iCAX::InputPDO::MakeInputStatePDODecl(iCAX::InputPDO::kDefaultInputStatePDOInstanceName);
        ASSERT_TRUE(Hub_.HasSlot(_Decl.nID));

        auto& _Slot = Hub_.GetSlot(_Decl.nID);
        iCAX::PDO::CPDOWriteLease _Write(_Slot, nDataVersion_);
        auto& _State = _Write.As<iCAX::InputPDO::SInputStatePDOHeader>();
        _State = {};
        iCAX::InputPDO::PrepareInputStatePDOHeader(_State, nDataVersion_);
        _State.nSceneID = 100;
        _State.nViewportID = nViewportID_;
        _State.nTimestampUS = nDataVersion_ * 1000;
        _State.nSequence = nDataVersion_;
        _State.Pointer.nX = 10.0f;
        _State.Pointer.nY = 20.0f;
        _State.Pointer.nDeltaX = nDeltaX_;
        _State.Pointer.nWheelY = nWheelY_;
        _State.Pointer.nButtonMask = iCAX::InputPDO::kInputMouseButtonRight;
        ASSERT_TRUE(iCAX::InputPDO::SetInputKeyDown(
            _State.Keyboard,
            static_cast<iCAX::InputPDO::InputKeyCode>(iCAX::InputPDO::EInputKeyCode::KeyW),
            bWDown_));
        ASSERT_TRUE(iCAX::InputPDO::SetInputKeyDown(
            _State.Keyboard,
            static_cast<iCAX::InputPDO::InputKeyCode>(iCAX::InputPDO::EInputKeyCode::KeyD),
            bDDown_));
        _Write.Commit();
        Hub_.SwapInSlot();
    }
}

TEST(InputServiceTest, ReadsKeyboardTransitionsAndPointerSnapshotFromInputPDO)
{
    auto _pService = CreateInputService();
    ASSERT_NE(nullptr, _pService);

    CFakeProjectContext _ProjectContext;
    auto _pPDOHub = CreatePDOHub();
    CFakeSceneContext _SceneContext(_pPDOHub);

    _pService->UpdateFromPDO(_ProjectContext, _SceneContext);

    const auto _Decl = iCAX::InputPDO::MakeInputStatePDODecl(iCAX::InputPDO::kDefaultInputStatePDOInstanceName);
    ASSERT_TRUE(_pPDOHub->HasSlot(_Decl.nID));

    const auto& _ProjectID = _ProjectContext.GetProjectID();
    const auto& _SceneID = _SceneContext.GetSceneID();
    constexpr iCAX::InputPDO::InputViewportID kViewportID = 1;
    constexpr auto kW = static_cast<iCAX::InputPDO::InputKeyCode>(iCAX::InputPDO::EInputKeyCode::KeyW);
    constexpr auto kD = static_cast<iCAX::InputPDO::InputKeyCode>(iCAX::InputPDO::EInputKeyCode::KeyD);

    WriteInputState(*_pPDOHub, 1, kViewportID, true, false, 3.0f, -120.0f);
    _pService->UpdateFromPDO(_ProjectContext, _SceneContext);

    EXPECT_TRUE(_pService->GetKey(_ProjectID, _SceneID, kViewportID, kW));
    EXPECT_TRUE(_pService->GetKeyDown(_ProjectID, _SceneID, kViewportID, kW));
    EXPECT_FALSE(_pService->GetKeyUp(_ProjectID, _SceneID, kViewportID, kW));
    EXPECT_FALSE(_pService->GetKey(_ProjectID, _SceneID, kViewportID, kD));

    auto _Snapshot = _pService->GetFrameSnapshot(_ProjectID, _SceneID, kViewportID);
    EXPECT_TRUE(_Snapshot.bHasState);
    EXPECT_EQ(1u, _Snapshot.nDataVersion);
    EXPECT_EQ(3.0f, _Snapshot.Pointer.nDeltaX);
    EXPECT_EQ(-120.0f, _Snapshot.Pointer.nWheelY);
    EXPECT_EQ(iCAX::InputPDO::kInputMouseButtonRight, _Snapshot.Pointer.nButtonMask);

    _pService->UpdateFromPDO(_ProjectContext, _SceneContext);
    EXPECT_TRUE(_pService->GetKey(_ProjectID, _SceneID, kViewportID, kW));
    EXPECT_FALSE(_pService->GetKeyDown(_ProjectID, _SceneID, kViewportID, kW));
    _Snapshot = _pService->GetFrameSnapshot(_ProjectID, _SceneID, kViewportID);
    EXPECT_TRUE(_Snapshot.bHasState);
    EXPECT_EQ(1u, _Snapshot.nDataVersion);
    EXPECT_EQ(0.0f, _Snapshot.Pointer.nDeltaX);
    EXPECT_EQ(0.0f, _Snapshot.Pointer.nWheelY);
    EXPECT_EQ(iCAX::InputPDO::kInputMouseButtonRight, _Snapshot.Pointer.nButtonMask);

    WriteInputState(*_pPDOHub, 2, kViewportID, true, true, 0.0f, 0.0f);
    _pService->UpdateFromPDO(_ProjectContext, _SceneContext);

    EXPECT_TRUE(_pService->GetKey(_ProjectID, _SceneID, kViewportID, kW));
    EXPECT_FALSE(_pService->GetKeyDown(_ProjectID, _SceneID, kViewportID, kW));
    EXPECT_TRUE(_pService->GetKey(_ProjectID, _SceneID, kViewportID, kD));
    EXPECT_TRUE(_pService->GetKeyDown(_ProjectID, _SceneID, kViewportID, kD));

    WriteInputState(*_pPDOHub, 3, kViewportID, false, true, 0.0f, 0.0f);
    _pService->UpdateFromPDO(_ProjectContext, _SceneContext);

    EXPECT_FALSE(_pService->GetKey(_ProjectID, _SceneID, kViewportID, kW));
    EXPECT_TRUE(_pService->GetKeyUp(_ProjectID, _SceneID, kViewportID, kW));
    EXPECT_TRUE(_pService->GetKey(_ProjectID, _SceneID, kViewportID, kD));
    EXPECT_FALSE(_pService->GetKeyUp(_ProjectID, _SceneID, kViewportID, kD));
}
