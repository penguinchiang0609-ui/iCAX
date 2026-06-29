#include <gtest/gtest.h>

#include <PDORenderService/PDORenderService.h>
#include <RenderPDO/RenderPDO.h>
#include <PDO/IPDOHub.h>
#include <PDO/PDOLease.h>

namespace
{
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
