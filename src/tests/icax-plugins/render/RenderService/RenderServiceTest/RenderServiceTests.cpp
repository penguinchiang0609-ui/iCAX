#include <gtest/gtest.h>

#include <ApplicationContext/IApplicationContext.h>
#include <ProductContext/IProductContext.h>
#include <ProjectContext/IProjectContext.h>
#include <PDORenderService/PDORenderService.h>
#include <RenderPDO/RenderPDO.h>
#include <PDO/IPDOHub.h>
#include <PDO/PDOLease.h>
#include <Mailbox/MailChannel.h>
#include <Mailbox/MailPayload.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    class CTestApplicationContext final : public iCAX::Application::IApplicationContext
    {
    public:
        const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override
        {
            throw std::logic_error("Application descriptor is not used by render service tests");
        }

        const iCAX::Application::CApplicationPaths& GetPaths() const override
        {
            throw std::logic_error("Application paths are not used by render service tests");
        }

        iCAX::Data::PropertyBag GetSettings() const override
        {
            return {};
        }
    };

    class CTestProductContext final : public iCAX::Product::IProductContext
    {
    public:
        const iCAX::Product::CProductDefinition& GetDefinition() const override
        {
            throw std::logic_error("Product definition is not used by render service tests");
        }

        const std::string& GetProductID() const override
        {
            return m_ProductID;
        }

        iCAX::Product::CProductData GetProductData() const override
        {
            return {};
        }

        iCAX::Services::CServiceProvider& GetServiceProvider() const override
        {
            throw std::logic_error("Service provider is not used by render service tests");
        }

        iCAX::Database::IMetaRegistry& GetMetaRegistry() const override
        {
            throw std::logic_error("Meta registry is not used by render service tests");
        }

        iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override
        {
            throw std::logic_error("Behaviour registry is not used by render service tests");
        }

        iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override
        {
            throw std::logic_error("Resource loader registry is not used by render service tests");
        }

        iCAX::Command::CCommandRegistry& GetCommandRegistry() const override
        {
            throw std::logic_error("Command registry is not used by render service tests");
        }

    private:
        std::string m_ProductID = "render-service-test";
    };

    class CTestProjectContext final : public iCAX::Project::IProjectContext
    {
    public:
        CTestProjectContext(
            IN const iCAX::Data::uuid& ProjectID_,
            IN std::shared_ptr<iCAX::PDO::IPDOHub> pPDOHub_)
            : m_ProjectID(ProjectID_)
            , m_ProjectChannelID(iCAX::Data::GenerateNewUUID())
            , m_pPDOHub(std::move(pPDOHub_))
            , m_pChannel(std::make_shared<iCAX::Mail::CMailChannel>())
        {
        }

        const iCAX::Data::uuid& GetProjectID() const override
        {
            return m_ProjectID;
        }

        const iCAX::Data::uuid& GetProjectChannelID() const override
        {
            return m_ProjectChannelID;
        }

        iCAX::Mail::CMailPostOffice GetBackendPostOffice() const override
        {
            return m_pChannel->GetEndAPostOffice();
        }

        iCAX::Mail::CMailPostOffice GetFrontendPostOffice() const override
        {
            return m_pChannel->GetEndBPostOffice();
        }

        const std::string& GetProjectName() const override
        {
            return m_ProjectName;
        }

        const std::string& GetProjectPath() const override
        {
            return m_ProjectPath;
        }

        iCAX::Data::PropertyBag& Settings() override
        {
            return m_Settings;
        }

        const iCAX::Data::PropertyBag& Settings() const override
        {
            return m_Settings;
        }

        iCAX::Database::IRepository& Database() override
        {
            throw std::logic_error("Repository is not used by render service tests");
        }

        const iCAX::Database::IRepository& Database() const override
        {
            throw std::logic_error("Repository is not used by render service tests");
        }

        iCAX::Resource::CResourceLibrary& Resources() override
        {
            throw std::logic_error("Resources are not used by render service tests");
        }

        const iCAX::Resource::CResourceLibrary& Resources() const override
        {
            throw std::logic_error("Resources are not used by render service tests");
        }

        bool HasPDOHub() const override
        {
            return m_pPDOHub != nullptr;
        }

        iCAX::PDO::IPDOHub& PDOHub() override
        {
            if (!m_pPDOHub)
            {
                throw std::logic_error("PDO hub is not configured");
            }
            return *m_pPDOHub;
        }

        const iCAX::PDO::IPDOHub& PDOHub() const override
        {
            if (!m_pPDOHub)
            {
                throw std::logic_error("PDO hub is not configured");
            }
            return *m_pPDOHub;
        }

        iCAX::Services::CServiceProvider& Services() const override
        {
            throw std::logic_error("Service provider is not used by render service tests");
        }

    private:
        iCAX::Data::uuid m_ProjectID;
        iCAX::Data::uuid m_ProjectChannelID;
        std::shared_ptr<iCAX::PDO::IPDOHub> m_pPDOHub;
        std::shared_ptr<iCAX::Mail::CMailChannel> m_pChannel;
        std::string m_ProjectName = "Render Service Test";
        std::string m_ProjectPath;
        iCAX::Data::PropertyBag m_Settings;
    };

    iCAX::Render::SRenderMeshData MakeTriangleMesh(
        IN iCAX::Render::RenderGeometryID nGeometryID_,
        IN iCAX::Render::RenderDataVersion nVersion_,
        IN float nZ_)
    {
        iCAX::Render::SRenderMeshData _Mesh;
        _Mesh.nGeometryID = nGeometryID_;
        _Mesh.nDataVersion = nVersion_;
        _Mesh.Bounds.Min = { 0.0f, 0.0f, nZ_ };
        _Mesh.Bounds.Max = { 1.0f, 1.0f, nZ_ };
        _Mesh.Positions = {
            { 0.0f, 0.0f, nZ_ },
            { 1.0f, 0.0f, nZ_ },
            { 0.0f, 1.0f, nZ_ }
        };
        return _Mesh;
    }

    iCAX::Render::SRenderPolylineData MakePolyline(
        IN iCAX::Render::RenderGeometryID nGeometryID_,
        IN iCAX::Render::RenderDataVersion nVersion_)
    {
        iCAX::Render::SRenderPolylineData _Polyline;
        _Polyline.nGeometryID = nGeometryID_;
        _Polyline.nDataVersion = nVersion_;
        _Polyline.Bounds.Min = { 0.0f, 0.0f, 0.0f };
        _Polyline.Bounds.Max = { 2.0f, 1.0f, 0.0f };
        _Polyline.Points = {
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 0.0f },
            { 2.0f, 0.0f, 0.0f }
        };

        iCAX::Render::SRenderPolylineRangeData _Range;
        _Range.nFirstPoint = 0;
        _Range.nPointCount = 3;
        _Range.eRenderClass = iCAX::Render::ERenderClass::Selection;
        _Polyline.Ranges.push_back(_Range);
        return _Polyline;
    }

    iCAX::Render::SRenderToolpathData MakeToolpath(
        IN iCAX::Render::RenderGeometryID nGeometryID_,
        IN iCAX::Render::RenderDataVersion nVersion_)
    {
        iCAX::Render::SRenderToolpathData _Toolpath;
        _Toolpath.nGeometryID = nGeometryID_;
        _Toolpath.nDataVersion = nVersion_;
        _Toolpath.Bounds.Min = { 0.0f, 0.0f, -1.0f };
        _Toolpath.Bounds.Max = { 2.0f, 0.0f, 1.0f };
        _Toolpath.Points = {
            { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, 1000.0f, 0 },
            { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 1200.0f, 0 },
            { { 2.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }, 1200.0f, 0 }
        };

        iCAX::Render::SRenderToolpathSpanData _Span;
        _Span.nFirstPoint = 0;
        _Span.nPointCount = 3;
        _Span.eMoveKind = iCAX::Render::EToolpathMoveKind::Cutting;
        _Toolpath.Spans.push_back(_Span);
        return _Toolpath;
    }
}

TEST(PDORenderServiceTest, KeepsScenesIsolatedByProject)
{
    iCAX::PDORenderService::CPDORenderService _Service;
    _Service.OnLoad();

    const auto _ProjectA = iCAX::Data::GenerateNewUUID();
    const auto _ProjectB = iCAX::Data::GenerateNewUUID();
    constexpr iCAX::Render::RenderSceneID _SceneID = 1;

    EXPECT_TRUE(_Service.CreateScene(_ProjectA, _SceneID));
    EXPECT_TRUE(_Service.CreateScene(_ProjectB, _SceneID));
    EXPECT_TRUE(_Service.UpsertMesh(_ProjectA, _SceneID, MakeTriangleMesh(10, 1, 0.0f)));
    EXPECT_TRUE(_Service.UpsertMesh(_ProjectB, _SceneID, MakeTriangleMesh(20, 2, 5.0f)));

    const auto _SnapshotA = _Service.GetSceneSnapshot(_ProjectA, _SceneID);
    const auto _SnapshotB = _Service.GetSceneSnapshot(_ProjectB, _SceneID);
    EXPECT_EQ(1u, _SnapshotA.Meshes.size());
    EXPECT_EQ(1u, _SnapshotB.Meshes.size());
    EXPECT_TRUE(_SnapshotA.Meshes.contains(10));
    EXPECT_TRUE(_SnapshotB.Meshes.contains(20));

    _Service.DestroyProject(_ProjectA);
    EXPECT_FALSE(_Service.HasScene(_ProjectA, _SceneID));
    EXPECT_TRUE(_Service.HasScene(_ProjectB, _SceneID));
}

TEST(PDORenderServiceTest, WritesGeometryInstanceAndViewPayloadsToPDO)
{
    iCAX::PDORenderService::CPDORenderService _Service;
    _Service.OnLoad();

    const auto _ProjectID = iCAX::Data::GenerateNewUUID();
    constexpr iCAX::Render::RenderSceneID _SceneID = 7;
    constexpr iCAX::Render::RenderGeometryID _MeshID = 88;
    constexpr iCAX::Render::RenderGeometryID _PolylineID = 89;
    constexpr iCAX::Render::RenderGeometryID _ToolpathID = 90;

    ASSERT_TRUE(_Service.CreateScene(_ProjectID, _SceneID));
    ASSERT_TRUE(_Service.UpsertMesh(_ProjectID, _SceneID, MakeTriangleMesh(_MeshID, 11, 0.0f)));
    ASSERT_TRUE(_Service.UpsertPolyline(_ProjectID, _SceneID, MakePolyline(_PolylineID, 14)));
    ASSERT_TRUE(_Service.UpsertToolpath(_ProjectID, _SceneID, MakeToolpath(_ToolpathID, 15)));

    iCAX::Render::SRenderInstanceData _Instance;
    _Instance.nObjectID = 100;
    _Instance.nGeometryID = _MeshID;
    _Instance.eGeometryKind = iCAX::Render::ERenderGeometryKind::Mesh;
    ASSERT_TRUE(_Service.SetInstances(_ProjectID, _SceneID, { _Instance }, {}, 12));

    iCAX::Render::SRenderViewStateData _View;
    _View.nViewID = 1;
    _View.nWidth = 800;
    _View.nHeight = 600;
    ASSERT_TRUE(_Service.SetViewStates(_ProjectID, _SceneID, { _View }, 0, 13));

    auto _Hub = iCAX::PDO::GeneratePDOHub({
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Mesh, "main.mesh", iCAX::PDO::kDirection2External, 1024),
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Polyline, "main.polyline", iCAX::PDO::kDirection2External, 1024),
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Toolpath, "main.toolpath", iCAX::PDO::kDirection2External, 2048),
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::InstanceList, "main.instances", iCAX::PDO::kDirection2External, 1024),
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::ViewState, "main.view", iCAX::PDO::kDirection2External, 2048)
    });

    auto& _MeshSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.mesh", "main.mesh"));
    auto& _PolylineSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.polyline", "main.polyline"));
    auto& _ToolpathSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.toolpath", "main.toolpath"));
    auto& _InstanceSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.instance_list", "main.instances"));
    auto& _ViewSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.view_state", "main.view"));

    EXPECT_TRUE(_Service.WriteMeshToPDO(_ProjectID, _SceneID, _MeshID, _MeshSlot));
    EXPECT_TRUE(_Service.WritePolylineToPDO(_ProjectID, _SceneID, _PolylineID, _PolylineSlot));
    EXPECT_TRUE(_Service.WriteToolpathToPDO(_ProjectID, _SceneID, _ToolpathID, _ToolpathSlot));
    EXPECT_TRUE(_Service.WriteInstanceListToPDO(_ProjectID, _SceneID, _InstanceSlot));
    EXPECT_TRUE(_Service.WriteViewStatesToPDO(_ProjectID, _SceneID, _ViewSlot));
    _Hub->SwapOutSlot();

    iCAX::PDO::CPDOReadLease _MeshRead(_MeshSlot);
    const auto& _MeshHeader = _MeshRead.As<iCAX::RenderPDO::SRenderMeshPDOHeader>();
    EXPECT_EQ(_MeshID, _MeshHeader.nGeometryID);
    EXPECT_EQ(3u, _MeshHeader.nVertexCount);
    EXPECT_EQ(11u, _MeshHeader.Header.nDataVersion);

    iCAX::PDO::CPDOReadLease _PolylineRead(_PolylineSlot);
    const auto& _PolylineHeader = _PolylineRead.As<iCAX::RenderPDO::SRenderPolylinePDOHeader>();
    EXPECT_EQ(_PolylineID, _PolylineHeader.nGeometryID);
    EXPECT_EQ(3u, _PolylineHeader.nPointCount);
    EXPECT_EQ(1u, _PolylineHeader.nRangeCount);
    EXPECT_EQ(14u, _PolylineHeader.Header.nDataVersion);

    iCAX::PDO::CPDOReadLease _ToolpathRead(_ToolpathSlot);
    const auto& _ToolpathHeader = _ToolpathRead.As<iCAX::RenderPDO::SRenderToolpathPDOHeader>();
    EXPECT_EQ(_ToolpathID, _ToolpathHeader.nGeometryID);
    EXPECT_EQ(3u, _ToolpathHeader.nPointCount);
    EXPECT_EQ(1u, _ToolpathHeader.nSpanCount);
    EXPECT_EQ(15u, _ToolpathHeader.Header.nDataVersion);

    iCAX::PDO::CPDOReadLease _InstanceRead(_InstanceSlot);
    const auto& _InstanceHeader = _InstanceRead.As<iCAX::RenderPDO::SRenderInstanceListPDOHeader>();
    EXPECT_EQ(1u, _InstanceHeader.nInstanceCount);
    EXPECT_EQ(12u, _InstanceHeader.Header.nDataVersion);

    iCAX::PDO::CPDOReadLease _ViewRead(_ViewSlot);
    const auto& _ViewHeader = _ViewRead.As<iCAX::RenderPDO::SRenderViewStatePDOHeader>();
    EXPECT_EQ(1u, _ViewHeader.nViewCount);
    EXPECT_EQ(13u, _ViewHeader.Header.nDataVersion);
}

TEST(PDORenderServiceTest, UpdateAllocatesObjectLevelPDOSlotsAndSendsFrontendEvents)
{
    iCAX::PDORenderService::CPDORenderService _Service;
    _Service.OnLoad();

    const auto _ProjectID = iCAX::Data::GenerateNewUUID();
    constexpr iCAX::Render::RenderSceneID _SceneID = 3;
    constexpr iCAX::Render::RenderGeometryID _MeshID = 201;
    constexpr iCAX::Render::RenderGeometryID _PolylineID = 202;
    constexpr iCAX::Render::RenderGeometryID _ToolpathID = 203;

    ASSERT_TRUE(_Service.CreateScene(_ProjectID, _SceneID));
    ASSERT_TRUE(_Service.UpsertMesh(_ProjectID, _SceneID, MakeTriangleMesh(_MeshID, 21, 0.0f)));
    ASSERT_TRUE(_Service.UpsertPolyline(_ProjectID, _SceneID, MakePolyline(_PolylineID, 22)));
    ASSERT_TRUE(_Service.UpsertToolpath(_ProjectID, _SceneID, MakeToolpath(_ToolpathID, 23)));

    iCAX::Render::SRenderInstanceData _Instance;
    _Instance.nObjectID = 300;
    _Instance.nGeometryID = _MeshID;
    _Instance.eGeometryKind = iCAX::Render::ERenderGeometryKind::Mesh;
    ASSERT_TRUE(_Service.SetInstances(_ProjectID, _SceneID, { _Instance }, {}, 24));

    iCAX::Render::SRenderViewStateData _View;
    _View.nViewID = 9;
    _View.nWidth = 1280;
    _View.nHeight = 720;
    ASSERT_TRUE(_Service.SetViewStates(_ProjectID, _SceneID, { _View }, 0, 25));

    iCAX::PDO::CPDOHubCreateInfo _PDOHubCreateInfo;
    _PDOHubCreateInfo.nArenaSize = 1024ull * 1024ull;
    _PDOHubCreateInfo.nSlotCapacity = 32;
    auto _Hub = iCAX::PDO::GeneratePDOHub(_PDOHubCreateInfo);

    const auto _MeshPDOID = iCAX::PDORenderService::CPDORenderService::MakeGeometryPDOID(
        _ProjectID,
        _SceneID,
        iCAX::Render::ERenderGeometryKind::Mesh,
        _MeshID);
    const auto _PolylinePDOID = iCAX::PDORenderService::CPDORenderService::MakeGeometryPDOID(
        _ProjectID,
        _SceneID,
        iCAX::Render::ERenderGeometryKind::Polyline,
        _PolylineID);
    const auto _ToolpathPDOID = iCAX::PDORenderService::CPDORenderService::MakeGeometryPDOID(
        _ProjectID,
        _SceneID,
        iCAX::Render::ERenderGeometryKind::Toolpath,
        _ToolpathID);
    const auto _ObjectPDOID = iCAX::PDORenderService::CPDORenderService::MakeObjectPDOID(
        _ProjectID,
        _SceneID,
        _Instance.nObjectID);
    const auto _ViewPDOID = iCAX::PDORenderService::CPDORenderService::MakeViewStatePDOID(_ProjectID, _SceneID);

    CTestApplicationContext _ApplicationContext;
    CTestProductContext _ProductContext;
    CTestProjectContext _ProjectContext(_ProjectID, _Hub);

    _Service.Update(_ApplicationContext, _ProductContext, _ProjectContext, 0.016, 1.0);
    _Hub->SwapOutSlot();

    EXPECT_TRUE(_Hub->HasSlot(_MeshPDOID));
    EXPECT_TRUE(_Hub->HasSlot(_PolylinePDOID));
    EXPECT_TRUE(_Hub->HasSlot(_ToolpathPDOID));
    EXPECT_TRUE(_Hub->HasSlot(_ObjectPDOID));
    EXPECT_TRUE(_Hub->HasSlot(_ViewPDOID));

    {
        iCAX::PDO::CPDOReadLease _MeshRead(_Hub->GetSlot(_MeshPDOID));
        const auto& _MeshHeader = _MeshRead.As<iCAX::RenderPDO::SRenderMeshPDOHeader>();
        EXPECT_EQ(_MeshID, _MeshHeader.nGeometryID);
        EXPECT_EQ(21u, _MeshHeader.Header.nDataVersion);

        iCAX::PDO::CPDOReadLease _PolylineRead(_Hub->GetSlot(_PolylinePDOID));
        const auto& _PolylineHeader = _PolylineRead.As<iCAX::RenderPDO::SRenderPolylinePDOHeader>();
        EXPECT_EQ(_PolylineID, _PolylineHeader.nGeometryID);
        EXPECT_EQ(22u, _PolylineHeader.Header.nDataVersion);

        iCAX::PDO::CPDOReadLease _ToolpathRead(_Hub->GetSlot(_ToolpathPDOID));
        const auto& _ToolpathHeader = _ToolpathRead.As<iCAX::RenderPDO::SRenderToolpathPDOHeader>();
        EXPECT_EQ(_ToolpathID, _ToolpathHeader.nGeometryID);
        EXPECT_EQ(23u, _ToolpathHeader.Header.nDataVersion);

        iCAX::PDO::CPDOReadLease _InstanceRead(_Hub->GetSlot(_ObjectPDOID));
        const auto& _InstanceHeader = _InstanceRead.As<iCAX::RenderPDO::SRenderInstanceListPDOHeader>();
        EXPECT_EQ(1u, _InstanceHeader.nInstanceCount);
        EXPECT_EQ(1u, _InstanceHeader.nStyleCount);
        EXPECT_EQ(24u, _InstanceHeader.Header.nDataVersion);

        iCAX::PDO::CPDOReadLease _ViewRead(_Hub->GetSlot(_ViewPDOID));
        const auto& _ViewHeader = _ViewRead.As<iCAX::RenderPDO::SRenderViewStatePDOHeader>();
        EXPECT_EQ(1u, _ViewHeader.nViewCount);
        EXPECT_EQ(25u, _ViewHeader.Header.nDataVersion);
    }

    auto _Mails = _ProjectContext.GetFrontendPostOffice().Receive();
    ASSERT_GE(_Mails.size(), 5u);
    size_t _AllocatedCount = 0;
    for (auto& _Mail : _Mails)
    {
        if (_Mail.Header.nTypeCode == iCAX::PDORenderService::kPDORenderSlotAllocatedEvent)
        {
            ++_AllocatedCount;
            const auto _Payload = iCAX::Mail::GetMailPayloadText(_Mail);
            EXPECT_NE(std::string::npos, _Payload.find("\"event\":\"SlotAllocated\""));
            EXPECT_NE(std::string::npos, _Payload.find("\"pdoId\":\""));
        }
        iCAX::Mail::ReleaseMailPayload(_Mail);
    }
    EXPECT_EQ(5u, _AllocatedCount);

    ASSERT_TRUE(_Service.SetInstances(_ProjectID, _SceneID, {}, {}, 26));
    _Service.Update(_ApplicationContext, _ProductContext, _ProjectContext, 0.016, 1.016);
    EXPECT_FALSE(_Hub->HasSlot(_ObjectPDOID));

    auto _RemovalMails = _ProjectContext.GetFrontendPostOffice().Receive();
    bool _bFoundObjectFreed = false;
    for (auto& _Mail : _RemovalMails)
    {
        if (_Mail.Header.nTypeCode == iCAX::PDORenderService::kPDORenderSlotFreedEvent)
        {
            const auto _Payload = iCAX::Mail::GetMailPayloadText(_Mail);
            _bFoundObjectFreed = _Payload.find("\"event\":\"SlotFreed\"") != std::string::npos
                && _Payload.find("\"slotRole\":\"Object\"") != std::string::npos
                && _Payload.find("\"pdoId\":\"" + std::to_string(_ObjectPDOID) + "\"") != std::string::npos;
        }
        iCAX::Mail::ReleaseMailPayload(_Mail);
    }
    EXPECT_TRUE(_bFoundObjectFreed);
}

TEST(PDORenderServiceTest, SendsDefragNotificationsThroughProjectMailbox)
{
    iCAX::PDORenderService::CPDORenderService _Service;
    const auto _ProjectID = iCAX::Data::GenerateNewUUID();
    CTestProjectContext _ProjectContext(_ProjectID, nullptr);
    constexpr iCAX::PDO::PDOID _MovedPDOID = 123456789ull;

    _Service.NotifyPDODefragBegin(_ProjectContext);
    _Service.NotifyPDOSlotMoved(_ProjectContext, _MovedPDOID);
    _Service.NotifyPDODefragEnd(_ProjectContext);

    auto _Mails = _ProjectContext.GetFrontendPostOffice().Receive();
    ASSERT_EQ(3u, _Mails.size());

    EXPECT_EQ(iCAX::PDORenderService::kPDORenderDefragBeginEvent, _Mails[0].Header.nTypeCode);
    EXPECT_NE(std::string::npos, iCAX::Mail::GetMailPayloadText(_Mails[0]).find("\"event\":\"DefragBegin\""));

    EXPECT_EQ(iCAX::PDORenderService::kPDORenderSlotMovedEvent, _Mails[1].Header.nTypeCode);
    const auto _MovedPayload = iCAX::Mail::GetMailPayloadText(_Mails[1]);
    EXPECT_NE(std::string::npos, _MovedPayload.find("\"event\":\"SlotMoved\""));
    EXPECT_NE(std::string::npos, _MovedPayload.find("\"pdoId\":\"" + std::to_string(_MovedPDOID) + "\""));

    EXPECT_EQ(iCAX::PDORenderService::kPDORenderDefragEndEvent, _Mails[2].Header.nTypeCode);
    EXPECT_NE(std::string::npos, iCAX::Mail::GetMailPayloadText(_Mails[2]).find("\"event\":\"DefragEnd\""));

    for (auto& _Mail : _Mails)
    {
        iCAX::Mail::ReleaseMailPayload(_Mail);
    }
}
