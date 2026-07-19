#include "pch.h"


#include <ApplicationContext/IApplicationContext.h>
#include <ProductContext/IProductContext.h>
#include <ProjectContext/IProjectContext.h>
#include <ProjectContext/ISceneContext.h>
#include <PDORenderService/PDORenderService.h>
#include <RenderPDO/RenderPDO.h>
#include <PDO/IPDOHub.h>
#include <PDO/PDOLease.h>
#include <Facades/FacadeChannel.h>
#include <Facades/FacadePayload.h>


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

        const iCAX::Services::CServiceProvider& Services() const override
        {
            throw std::logic_error("Application services are not used by render service tests");
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

        iCAX::Interaction::CFacadeRegistry& GetFacadeRegistry() const override
        {
            throw std::logic_error("Command registry is not used by render service tests");
        }

    private:
        std::string m_ProductID = "render-service-test";
    };

    class CTestProjectContext final : public iCAX::Project::IProjectContext
    {
    public:
        explicit CTestProjectContext(IN const iCAX::Data::uuid& ProjectID_)
            : m_ProjectID(ProjectID_)
        {
        }

        const iCAX::Data::uuid& GetProjectID() const override
        {
            return m_ProjectID;
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

    private:
        iCAX::Data::uuid m_ProjectID;
        std::string m_ProjectName = "Render Service Test";
        std::string m_ProjectPath;
        iCAX::Data::PropertyBag m_Settings;
    };

    class CTestSceneContext final : public iCAX::Project::ISceneContext
    {
    public:
        explicit CTestSceneContext(IN std::shared_ptr<iCAX::PDO::IPDOHub> pPDOHub_)
            : m_SceneID(iCAX::Data::GenerateNewUUID())
            , m_SceneChannelID(iCAX::Data::GenerateNewUUID())
            , m_pPDOHub(std::move(pPDOHub_))
            , m_pChannel(std::make_shared<iCAX::Interaction::CFacadeChannel>())
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
            return m_SceneName;
        }

        iCAX::Interaction::CFacadeEndpoint GetBackendFacadeEndpoint() const override
        {
            return m_pChannel->GetEndAEndpoint();
        }

        iCAX::Interaction::CFacadeEndpoint GetFrontendFacadeEndpoint() const override
        {
            return m_pChannel->GetEndBEndpoint();
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
        iCAX::Data::uuid m_SceneID;
        iCAX::Data::uuid m_SceneChannelID;
        std::shared_ptr<iCAX::PDO::IPDOHub> m_pPDOHub;
        std::shared_ptr<iCAX::Interaction::CFacadeChannel> m_pChannel;
        std::string m_SceneName = "Render Service Test Scene";
    };

    iCAX::Render::SRenderMeshData MakeTriangleMesh(
        IN iCAX::Render::RenderGeometryID nGeometryID_,
        IN iCAX::Render::RenderDataVersion nVersion_,
        IN float nZ_)
    {
        iCAX::Render::SRenderMeshData _Mesh;
        _Mesh.nGeometryID = nGeometryID_;
        _Mesh.nDataVersion = nVersion_;
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

    iCAX::Render::STransformData MakeTransform(
        IN iCAX::Render::TransformID nTransformID_,
        IN iCAX::Render::RenderDataVersion nVersion_,
        IN float nX_)
    {
        iCAX::Render::STransformData _Transform;
        _Transform.nTransformID = nTransformID_;
        _Transform.nDataVersion = nVersion_;
        _Transform.LocalToWorld = iCAX::Render::SMatrix4::Identity();
        _Transform.LocalToWorld.Values[12] = nX_;
        return _Transform;
    }

    iCAX::RenderPDO::SRenderID ToPDOID(IN const iCAX::Data::uuid& ID_)
    {
        iCAX::RenderPDO::SRenderID _Result;
        const auto _Bytes = ID_.as_bytes();
        for (size_t _Index = 0; _Index < _Result.Bytes.size(); ++_Index)
        {
            _Result.Bytes[_Index] = std::to_integer<uint8_t>(_Bytes[_Index]);
        }
        return _Result;
    }

    void ExpectRenderIDEq(
        IN const iCAX::Data::uuid& Expected_,
        IN const iCAX::RenderPDO::SRenderID& Actual_)
    {
        EXPECT_EQ(ToPDOID(Expected_).Bytes, Actual_.Bytes);
    }
}

TEST(PDORenderServiceTest, KeepsScenesIsolatedByProject)
{
    iCAX::PDORenderService::CPDORenderService _Service;
    _Service.OnLoad();

    const auto _ProjectA = iCAX::Data::GenerateNewUUID();
    const auto _ProjectB = iCAX::Data::GenerateNewUUID();
    const auto _MeshIDA = iCAX::Render::MakeRenderGeometryID("test.project-a.mesh");
    const auto _MeshIDB = iCAX::Render::MakeRenderGeometryID("test.project-b.mesh");
    constexpr iCAX::Render::RenderSceneID _SceneID = 1;

    EXPECT_TRUE(_Service.CreateScene(_ProjectA, _SceneID));
    EXPECT_TRUE(_Service.CreateScene(_ProjectB, _SceneID));
    EXPECT_TRUE(_Service.UpsertMesh(_ProjectA, _SceneID, MakeTriangleMesh(_MeshIDA, 1, 0.0f)));
    EXPECT_TRUE(_Service.UpsertMesh(_ProjectB, _SceneID, MakeTriangleMesh(_MeshIDB, 2, 5.0f)));

    const auto _SnapshotA = _Service.GetSceneSnapshot(_ProjectA, _SceneID);
    const auto _SnapshotB = _Service.GetSceneSnapshot(_ProjectB, _SceneID);
    EXPECT_EQ(1u, _SnapshotA.Meshes.size());
    EXPECT_EQ(1u, _SnapshotB.Meshes.size());
    EXPECT_TRUE(_SnapshotA.Meshes.contains(_MeshIDA));
    EXPECT_TRUE(_SnapshotB.Meshes.contains(_MeshIDB));

    _Service.DestroyProject(_ProjectA);
    EXPECT_FALSE(_Service.HasScene(_ProjectA, _SceneID));
    EXPECT_TRUE(_Service.HasScene(_ProjectB, _SceneID));
}

TEST(PDORenderServiceTest, WritesGeometryInstanceAndCameraPayloadsToPDO)
{
    iCAX::PDORenderService::CPDORenderService _Service;
    _Service.OnLoad();

    const auto _ProjectID = iCAX::Data::GenerateNewUUID();
    constexpr iCAX::Render::RenderSceneID _SceneID = 7;
    const auto _MeshID = iCAX::Render::MakeRenderGeometryID("test.main.mesh");
    const auto _PolylineID = iCAX::Render::MakeRenderGeometryID("test.main.polyline");
    const auto _ToolpathID = iCAX::Render::MakeRenderGeometryID("test.main.toolpath");
    const auto _ObjectID = iCAX::Data::GenerateNewUUID();
    const auto _CameraID = iCAX::Data::GenerateNewUUID();

    ASSERT_TRUE(_Service.CreateScene(_ProjectID, _SceneID));
    ASSERT_TRUE(_Service.UpsertMesh(_ProjectID, _SceneID, MakeTriangleMesh(_MeshID, 11, 0.0f)));
    ASSERT_TRUE(_Service.UpsertPolyline(_ProjectID, _SceneID, MakePolyline(_PolylineID, 14)));
    ASSERT_TRUE(_Service.UpsertToolpath(_ProjectID, _SceneID, MakeToolpath(_ToolpathID, 15)));

    iCAX::Render::SRenderInstanceData _Instance;
    _Instance.nObjectID = _ObjectID;
    _Instance.nDataVersion = 12;
    _Instance.nGeometryID = _MeshID;
    _Instance.eGeometryKind = iCAX::Render::ERenderGeometryKind::Mesh;
    ASSERT_TRUE(_Service.SetObjects(_ProjectID, _SceneID, { _Instance }));
    ASSERT_TRUE(_Service.SetTransforms(_ProjectID, _SceneID, {
        MakeTransform(_Instance.nObjectID, 16, 10.0f),
        MakeTransform(_CameraID, 17, 20.0f)
    }, 16));

    iCAX::Render::SRenderCameraData _Camera;
    _Camera.nCameraID = _CameraID;
    _Camera.nDataVersion = 13;
    ASSERT_TRUE(_Service.SetCameras(_ProjectID, _SceneID, { _Camera }, _Camera.nCameraID));

    auto _Hub = iCAX::PDO::GeneratePDOHub({
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Mesh, "main.mesh", iCAX::PDO::kDirection2External, 1024),
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Polyline, "main.polyline", iCAX::PDO::kDirection2External, 1024),
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Toolpath, "main.toolpath", iCAX::PDO::kDirection2External, 2048),
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Object, "main.object", iCAX::PDO::kDirection2External, 1024),
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Transform, "main.transform", iCAX::PDO::kDirection2External, 1024),
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Camera, "main.camera", iCAX::PDO::kDirection2External, 2048)
    });

    auto& _MeshSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.mesh", "main.mesh"));
    auto& _PolylineSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.polyline", "main.polyline"));
    auto& _ToolpathSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.toolpath", "main.toolpath"));
    auto& _ObjectSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.object", "main.object"));
    auto& _TransformSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.transform", "main.transform"));
    auto& _CameraSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.camera", "main.camera"));

    EXPECT_TRUE(_Service.WriteMeshToPDO(_ProjectID, _SceneID, _MeshID, _MeshSlot));
    EXPECT_TRUE(_Service.WritePolylineToPDO(_ProjectID, _SceneID, _PolylineID, _PolylineSlot));
    EXPECT_TRUE(_Service.WriteToolpathToPDO(_ProjectID, _SceneID, _ToolpathID, _ToolpathSlot));
    EXPECT_TRUE(_Service.WriteObjectToPDO(_ProjectID, _SceneID, _Instance.nObjectID, _ObjectSlot));
    EXPECT_TRUE(_Service.WriteTransformToPDO(_ProjectID, _SceneID, _Instance.nObjectID, _TransformSlot));
    EXPECT_TRUE(_Service.WriteCameraToPDO(_ProjectID, _SceneID, _Camera.nCameraID, _CameraSlot));
    _Hub->SwapOutSlot();

    iCAX::PDO::CPDOReadLease _MeshRead(_MeshSlot);
    const auto& _MeshHeader = _MeshRead.As<iCAX::RenderPDO::SRenderMeshPDOHeader>();
    ExpectRenderIDEq(_MeshID, _MeshHeader.nGeometryID);
    EXPECT_EQ(3u, _MeshHeader.nVertexCount);
    EXPECT_EQ(11u, _MeshHeader.Header.nDataVersion);

    iCAX::PDO::CPDOReadLease _PolylineRead(_PolylineSlot);
    const auto& _PolylineHeader = _PolylineRead.As<iCAX::RenderPDO::SRenderPolylinePDOHeader>();
    ExpectRenderIDEq(_PolylineID, _PolylineHeader.nGeometryID);
    EXPECT_EQ(3u, _PolylineHeader.nPointCount);
    EXPECT_EQ(1u, _PolylineHeader.nRangeCount);
    EXPECT_EQ(14u, _PolylineHeader.Header.nDataVersion);

    iCAX::PDO::CPDOReadLease _ToolpathRead(_ToolpathSlot);
    const auto& _ToolpathHeader = _ToolpathRead.As<iCAX::RenderPDO::SRenderToolpathPDOHeader>();
    ExpectRenderIDEq(_ToolpathID, _ToolpathHeader.nGeometryID);
    EXPECT_EQ(3u, _ToolpathHeader.nPointCount);
    EXPECT_EQ(1u, _ToolpathHeader.nSpanCount);
    EXPECT_EQ(15u, _ToolpathHeader.Header.nDataVersion);

    iCAX::PDO::CPDOReadLease _ObjectRead(_ObjectSlot);
    const auto& _ObjectHeader = _ObjectRead.As<iCAX::RenderPDO::SRenderObjectPDOHeader>();
    ExpectRenderIDEq(_Instance.nObjectID, _ObjectHeader.nObjectID);
    ExpectRenderIDEq(_Instance.nGeometryID, _ObjectHeader.nGeometryID);
    EXPECT_EQ(static_cast<uint32_t>(iCAX::RenderPDO::ERenderGeometryKind::Mesh), _ObjectHeader.nGeometryKind);
    EXPECT_EQ(12u, _ObjectHeader.Header.nDataVersion);

    iCAX::PDO::CPDOReadLease _TransformRead(_TransformSlot);
    const auto& _TransformHeader = _TransformRead.As<iCAX::RenderPDO::STransformPDOHeader>();
    ExpectRenderIDEq(_Instance.nObjectID, _TransformHeader.nTransformID);
    EXPECT_EQ(16u, _TransformHeader.Header.nDataVersion);

    iCAX::PDO::CPDOReadLease _CameraRead(_CameraSlot);
    const auto& _CameraHeader = _CameraRead.As<iCAX::RenderPDO::SRenderCameraPDOHeader>();
    ExpectRenderIDEq(_Camera.nCameraID, _CameraHeader.nCameraID);
    EXPECT_NE(0u, _CameraHeader.nFlags & iCAX::RenderPDO::kCameraFlagActive);
    EXPECT_EQ(13u, _CameraHeader.Header.nDataVersion);
}

TEST(PDORenderServiceTest, WritesTransformWhenMatrixChangesWithoutCallerVersionBump)
{
    iCAX::PDORenderService::CPDORenderService _Service;
    _Service.OnLoad();

    const auto _ProjectID = iCAX::Data::GenerateNewUUID();
    constexpr iCAX::Render::RenderSceneID _SceneID = 8;
    const auto _TransformID = iCAX::Data::GenerateNewUUID();
    ASSERT_TRUE(_Service.CreateScene(_ProjectID, _SceneID));

    auto _Hub = iCAX::PDO::GeneratePDOHub({
        iCAX::RenderPDO::MakeRenderPDODecl(iCAX::RenderPDO::ERenderPDOPayloadKind::Transform, "main.transform.live", iCAX::PDO::kDirection2External, 1024),
    });
    auto& _TransformSlot = _Hub->GetSlot(iCAX::PDO::MakePDOID("render.transform", "main.transform.live"));

    ASSERT_TRUE(_Service.SetTransforms(_ProjectID, _SceneID, { MakeTransform(_TransformID, 5, 1.0f) }, 5));
    ASSERT_TRUE(_Service.WriteTransformToPDO(_ProjectID, _SceneID, _TransformID, _TransformSlot));
    _Hub->SwapOutSlot();
    iCAX::Render::RenderDataVersion _FirstVersion = 0;
    {
        iCAX::PDO::CPDOReadLease _Read(_TransformSlot);
        const auto& _Header = _Read.As<iCAX::RenderPDO::STransformPDOHeader>();
        _FirstVersion = _Header.Header.nDataVersion;
        EXPECT_EQ(5u, _FirstVersion);
        EXPECT_FLOAT_EQ(1.0f, _Header.LocalToWorld.Values[12]);
    }

    ASSERT_TRUE(_Service.SetTransforms(_ProjectID, _SceneID, { MakeTransform(_TransformID, 5, 2.0f) }, 5));
    ASSERT_TRUE(_Service.WriteTransformToPDO(_ProjectID, _SceneID, _TransformID, _TransformSlot));
    _Hub->SwapOutSlot();
    {
        iCAX::PDO::CPDOReadLease _Read(_TransformSlot);
        const auto& _Header = _Read.As<iCAX::RenderPDO::STransformPDOHeader>();
        EXPECT_GT(_Header.Header.nDataVersion, _FirstVersion);
        EXPECT_FLOAT_EQ(2.0f, _Header.LocalToWorld.Values[12]);
    }
}

TEST(PDORenderServiceTest, UpdateAllocatesObjectLevelPDOSlotsAndSendsFrontendEvents)
{
    iCAX::PDORenderService::CPDORenderService _Service;
    _Service.OnLoad();

    const auto _ProjectID = iCAX::Data::GenerateNewUUID();
    iCAX::PDO::CPDOHubCreateInfo _PDOHubCreateInfo;
    _PDOHubCreateInfo.nArenaSize = 1024ull * 1024ull;
    _PDOHubCreateInfo.nSlotCapacity = 32;
    auto _Hub = iCAX::PDO::GeneratePDOHub(_PDOHubCreateInfo);

    CTestApplicationContext _ApplicationContext;
    CTestProductContext _ProductContext;
    CTestProjectContext _ProjectContext(_ProjectID);
    CTestSceneContext _SceneContext(_Hub);

    const auto _SceneID = iCAX::Render::MakeRenderSceneID(_SceneContext.GetSceneID());
    const auto _MeshID = iCAX::Render::MakeRenderGeometryID("test.dynamic.mesh");
    const auto _PolylineID = iCAX::Render::MakeRenderGeometryID("test.dynamic.polyline");
    const auto _ToolpathID = iCAX::Render::MakeRenderGeometryID("test.dynamic.toolpath");
    const auto _ObjectID = iCAX::Data::GenerateNewUUID();
    const auto _CameraID = iCAX::Data::GenerateNewUUID();

    ASSERT_TRUE(_Service.CreateScene(_ProjectID, _SceneID));
    ASSERT_TRUE(_Service.UpsertMesh(_ProjectID, _SceneID, MakeTriangleMesh(_MeshID, 21, 0.0f)));
    ASSERT_TRUE(_Service.UpsertPolyline(_ProjectID, _SceneID, MakePolyline(_PolylineID, 22)));
    ASSERT_TRUE(_Service.UpsertToolpath(_ProjectID, _SceneID, MakeToolpath(_ToolpathID, 23)));

    iCAX::Render::SRenderInstanceData _Instance;
    _Instance.nObjectID = _ObjectID;
    _Instance.nDataVersion = 24;
    _Instance.nGeometryID = _MeshID;
    _Instance.eGeometryKind = iCAX::Render::ERenderGeometryKind::Mesh;
    ASSERT_TRUE(_Service.SetObjects(_ProjectID, _SceneID, { _Instance }));
    ASSERT_TRUE(_Service.SetTransforms(_ProjectID, _SceneID, {
        MakeTransform(_Instance.nObjectID, 26, 30.0f),
        MakeTransform(_CameraID, 27, 40.0f)
    }, 26));

    iCAX::Render::SRenderCameraData _Camera;
    _Camera.nCameraID = _CameraID;
    _Camera.nDataVersion = 25;
    ASSERT_TRUE(_Service.SetCameras(_ProjectID, _SceneID, { _Camera }, _Camera.nCameraID));

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
    const auto _ObjectTransformPDOID = iCAX::PDORenderService::CPDORenderService::MakeTransformPDOID(
        _ProjectID,
        _SceneID,
        _Instance.nObjectID);
    const auto _CameraTransformPDOID = iCAX::PDORenderService::CPDORenderService::MakeTransformPDOID(
        _ProjectID,
        _SceneID,
        _Camera.nCameraID);
    const auto _CameraPDOID = iCAX::PDORenderService::CPDORenderService::MakeCameraPDOID(_ProjectID, _SceneID, _Camera.nCameraID);

    _Service.Update(_ApplicationContext, _ProductContext, _ProjectContext, _SceneContext, 0.016, 1.0);
    _Hub->SwapOutSlot();

    EXPECT_TRUE(_Hub->HasSlot(_MeshPDOID));
    EXPECT_TRUE(_Hub->HasSlot(_PolylinePDOID));
    EXPECT_TRUE(_Hub->HasSlot(_ToolpathPDOID));
    EXPECT_TRUE(_Hub->HasSlot(_ObjectPDOID));
    EXPECT_TRUE(_Hub->HasSlot(_ObjectTransformPDOID));
    EXPECT_TRUE(_Hub->HasSlot(_CameraTransformPDOID));
    EXPECT_TRUE(_Hub->HasSlot(_CameraPDOID));

    {
        iCAX::PDO::CPDOReadLease _MeshRead(_Hub->GetSlot(_MeshPDOID));
        const auto& _MeshHeader = _MeshRead.As<iCAX::RenderPDO::SRenderMeshPDOHeader>();
        ExpectRenderIDEq(_MeshID, _MeshHeader.nGeometryID);
        EXPECT_EQ(21u, _MeshHeader.Header.nDataVersion);

        iCAX::PDO::CPDOReadLease _PolylineRead(_Hub->GetSlot(_PolylinePDOID));
        const auto& _PolylineHeader = _PolylineRead.As<iCAX::RenderPDO::SRenderPolylinePDOHeader>();
        ExpectRenderIDEq(_PolylineID, _PolylineHeader.nGeometryID);
        EXPECT_EQ(22u, _PolylineHeader.Header.nDataVersion);

        iCAX::PDO::CPDOReadLease _ToolpathRead(_Hub->GetSlot(_ToolpathPDOID));
        const auto& _ToolpathHeader = _ToolpathRead.As<iCAX::RenderPDO::SRenderToolpathPDOHeader>();
        ExpectRenderIDEq(_ToolpathID, _ToolpathHeader.nGeometryID);
        EXPECT_EQ(23u, _ToolpathHeader.Header.nDataVersion);

        iCAX::PDO::CPDOReadLease _ObjectRead(_Hub->GetSlot(_ObjectPDOID));
        const auto& _ObjectHeader = _ObjectRead.As<iCAX::RenderPDO::SRenderObjectPDOHeader>();
        ExpectRenderIDEq(_Instance.nObjectID, _ObjectHeader.nObjectID);
        ExpectRenderIDEq(_Instance.nGeometryID, _ObjectHeader.nGeometryID);
        EXPECT_EQ(static_cast<uint32_t>(iCAX::RenderPDO::ERenderGeometryKind::Mesh), _ObjectHeader.nGeometryKind);
        EXPECT_EQ(24u, _ObjectHeader.Header.nDataVersion);

        iCAX::PDO::CPDOReadLease _TransformRead(_Hub->GetSlot(_ObjectTransformPDOID));
        const auto& _TransformHeader = _TransformRead.As<iCAX::RenderPDO::STransformPDOHeader>();
        ExpectRenderIDEq(_Instance.nObjectID, _TransformHeader.nTransformID);
        EXPECT_EQ(26u, _TransformHeader.Header.nDataVersion);

        iCAX::PDO::CPDOReadLease _CameraRead(_Hub->GetSlot(_CameraPDOID));
        const auto& _CameraHeader = _CameraRead.As<iCAX::RenderPDO::SRenderCameraPDOHeader>();
        ExpectRenderIDEq(_Camera.nCameraID, _CameraHeader.nCameraID);
        EXPECT_NE(0u, _CameraHeader.nFlags & iCAX::RenderPDO::kCameraFlagActive);
        EXPECT_EQ(25u, _CameraHeader.Header.nDataVersion);
    }

    auto _Frames = _SceneContext.GetFrontendFacadeEndpoint().Receive();
    ASSERT_GE(_Frames.size(), 7u);
    size_t _AllocatedCount = 0;
    for (auto& _Frame : _Frames)
    {
        if (_Frame.nMethodCode == iCAX::PDORenderService::kPDORenderSlotAllocatedEvent)
        {
            ++_AllocatedCount;
            const auto _Payload = iCAX::Interaction::GetFacadePayloadText(_Frame);
            EXPECT_NE(std::string::npos, _Payload.find("\"event\":\"SlotAllocated\""));
            EXPECT_NE(std::string::npos, _Payload.find("\"pdoId\":\""));
        }
    }
    EXPECT_EQ(7u, _AllocatedCount);

    ASSERT_TRUE(_Service.SetObjects(_ProjectID, _SceneID, {}));
    _Service.Update(_ApplicationContext, _ProductContext, _ProjectContext, _SceneContext, 0.016, 1.016);
    EXPECT_FALSE(_Hub->HasSlot(_ObjectPDOID));

    auto _RemovalFrames = _SceneContext.GetFrontendFacadeEndpoint().Receive();
    bool _bFoundObjectFreed = false;
    for (auto& _Frame : _RemovalFrames)
    {
        if (_Frame.nMethodCode == iCAX::PDORenderService::kPDORenderSlotFreedEvent)
        {
            const auto _Payload = iCAX::Interaction::GetFacadePayloadText(_Frame);
            _bFoundObjectFreed = _Payload.find("\"event\":\"SlotFreed\"") != std::string::npos
                && _Payload.find("\"slotRole\":\"Object\"") != std::string::npos
                && _Payload.find("\"pdoId\":\"" + std::to_string(_ObjectPDOID) + "\"") != std::string::npos;
        }
    }
    EXPECT_TRUE(_bFoundObjectFreed);
}

TEST(PDORenderServiceTest, SendsDefragNotificationsThroughSceneFacade)
{
    iCAX::PDORenderService::CPDORenderService _Service;
    const auto _ProjectID = iCAX::Data::GenerateNewUUID();
    CTestProjectContext _ProjectContext(_ProjectID);
    CTestSceneContext _SceneContext(nullptr);
    constexpr iCAX::PDO::PDOID _MovedPDOID = 123456789ull;

    _Service.NotifyPDODefragBegin(_ProjectContext.GetProjectID(), _SceneContext);
    _Service.NotifyPDOSlotMoved(_ProjectContext.GetProjectID(), _SceneContext, _MovedPDOID);
    _Service.NotifyPDODefragEnd(_ProjectContext.GetProjectID(), _SceneContext);

    auto _Frames = _SceneContext.GetFrontendFacadeEndpoint().Receive();
    ASSERT_EQ(3u, _Frames.size());

    EXPECT_EQ(iCAX::PDORenderService::kPDORenderDefragBeginEvent, _Frames[0].nMethodCode);
    EXPECT_NE(std::string::npos, iCAX::Interaction::GetFacadePayloadText(_Frames[0]).find("\"event\":\"DefragBegin\""));

    EXPECT_EQ(iCAX::PDORenderService::kPDORenderSlotMovedEvent, _Frames[1].nMethodCode);
    const auto _MovedPayload = iCAX::Interaction::GetFacadePayloadText(_Frames[1]);
    EXPECT_NE(std::string::npos, _MovedPayload.find("\"event\":\"SlotMoved\""));
    EXPECT_NE(std::string::npos, _MovedPayload.find("\"pdoId\":\"" + std::to_string(_MovedPDOID) + "\""));

    EXPECT_EQ(iCAX::PDORenderService::kPDORenderDefragEndEvent, _Frames[2].nMethodCode);
    EXPECT_NE(std::string::npos, iCAX::Interaction::GetFacadePayloadText(_Frames[2]).find("\"event\":\"DefragEnd\""));
}
