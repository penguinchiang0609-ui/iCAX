#pragma once

#include "RenderPDOTypes.h"

#include <type_traits>

namespace iCAX
{
    namespace RenderPDO
    {
        /*
        * @brief 所有 Render PDO payload 的公共头。
        * @details
        *   每个 PDO slot 的 payload 起始处都应放置该头。nPayloadSize 表示本次写入实际使用的字节数，
        *   可以小于 PDO slot 声明的固定容量。nDataVersion 应与写入 PDO slot 时传入的数据版本一致。
        */
        struct _RENDER_PDO_EXP SRenderPDOHeader final
        {
            uint32_t nMagic = kRenderPDOMagic;
            uint32_t nLayoutVersion = kRenderPDOLayoutVersion;
            uint32_t nPayloadKind = static_cast<uint32_t>(ERenderPDOPayloadKind::Unknown);
            uint32_t nHeaderSize = sizeof(SRenderPDOHeader);
            uint64_t nPayloadSize = sizeof(SRenderPDOHeader);
            RenderDataVersion nDataVersion = 0;
        };

        /*
        * @brief 轻量显示样式。
        * @details
        *   这不是 material。它只是后端给前方的渲染提示；前方可以按自己的渲染实现解释或忽略。
        */
        struct _RENDER_PDO_EXP SRenderStyleData final
        {
            RenderStyleID nStyleID = 0;
            uint32_t nColorRGBA = 0xFFFFFFFFu;
            float nLineWidth = 1.0f;
            uint32_t nFlags = 0;
        };

        /*
        * @brief Mesh PDO payload 头。
        * @details
        *   点、法向、颜色和索引都在同一个 payload 内，通过 offset 定位。offset 为相对 payload 起点的字节偏移。
        */
        struct _RENDER_PDO_EXP SRenderMeshPDOHeader final
        {
            SRenderPDOHeader Header;
            RenderGeometryID nGeometryID = 0;
            SRenderAABB Bounds;
            uint32_t nTopology = static_cast<uint32_t>(ERenderTopology::TriangleList);
            uint32_t nVertexCount = 0;
            uint32_t nIndexCount = 0;
            uint32_t nFlags = 0;
            uint64_t nPositionsOffset = 0;      // SFloat3[nVertexCount]
            uint64_t nNormalsOffset = 0;        // SFloat3[nVertexCount], optional
            uint64_t nVertexColorsOffset = 0;   // uint32_t[nVertexCount], RGBA8, optional
            uint64_t nIndicesOffset = 0;        // uint32_t[nIndexCount], optional
        };

        struct _RENDER_PDO_EXP SRenderPolylineRangeData final
        {
            uint32_t nFirstPoint = 0;
            uint32_t nPointCount = 0;
            uint32_t nRenderClass = static_cast<uint32_t>(ERenderClass::Unknown);
            RenderStyleID nStyleID = 0;
            uint32_t nFlags = 0;
            uint32_t nReserved = 0;
        };

        struct _RENDER_PDO_EXP SRenderPolylinePDOHeader final
        {
            SRenderPDOHeader Header;
            RenderGeometryID nGeometryID = 0;
            SRenderAABB Bounds;
            uint32_t nPointCount = 0;
            uint32_t nRangeCount = 0;
            uint64_t nPointsOffset = 0;         // SFloat3[nPointCount]
            uint64_t nRangesOffset = 0;         // SRenderPolylineRangeData[nRangeCount]
        };

        struct _RENDER_PDO_EXP SRenderToolpathPointData final
        {
            SFloat3 Position;
            SFloat3 ToolAxis = { 0.0f, 0.0f, 1.0f };
            float nFeed = 0.0f;
            uint32_t nFlags = 0;
        };

        struct _RENDER_PDO_EXP SRenderToolpathSpanData final
        {
            uint32_t nFirstPoint = 0;
            uint32_t nPointCount = 0;
            uint32_t nMoveKind = static_cast<uint32_t>(EToolpathMoveKind::Unknown);
            uint32_t nToolID = 0;
            RenderStyleID nStyleID = 0;
            uint32_t nFlags = 0;
        };

        struct _RENDER_PDO_EXP SRenderToolpathPDOHeader final
        {
            SRenderPDOHeader Header;
            RenderGeometryID nGeometryID = 0;
            SRenderAABB Bounds;
            uint32_t nPointCount = 0;
            uint32_t nSpanCount = 0;
            uint64_t nPointsOffset = 0;         // SRenderToolpathPointData[nPointCount]
            uint64_t nSpansOffset = 0;          // SRenderToolpathSpanData[nSpanCount]
        };

        struct _RENDER_PDO_EXP SRenderInstanceData final
        {
            RenderObjectID nObjectID = 0;
            RenderGeometryID nGeometryID = 0;
            uint32_t nGeometryKind = static_cast<uint32_t>(ERenderGeometryKind::Unknown);
            uint32_t nRenderClass = static_cast<uint32_t>(ERenderClass::Unknown);
            RenderStyleID nStyleID = 0;
            uint32_t nFlags = kRenderFlagVisible | kRenderFlagSelectable;
            SMatrix4 Transform = SMatrix4::Identity();
        };

        /*
        * @brief 实例列表 PDO payload 头。
        * @details
        *   几何 payload 描述“几何数据是什么”；实例列表描述“哪些对象要显示、如何摆放、是否可见”。
        */
        struct _RENDER_PDO_EXP SRenderInstanceListPDOHeader final
        {
            SRenderPDOHeader Header;
            uint32_t nInstanceCount = 0;
            uint32_t nStyleCount = 0;
            uint64_t nInstancesOffset = 0;      // SRenderInstanceData[nInstanceCount]
            uint64_t nStylesOffset = 0;         // SRenderStyleData[nStyleCount], optional
        };

        /*
        * @brief 视图状态数据。
        * @details
        *   视图状态表达 viewport 尺寸、相机语义和矩阵。它不是 renderer 状态，不包含 shader、pipeline 或 GPU 资源。
        *   前端可以写入该 PDO 供后端做拾取/LOD，也可以读取后端写入的默认视角或仿真同步视角。
        */
        struct _RENDER_PDO_EXP SRenderViewStateData final
        {
            RenderViewID nViewID = 0;
            uint32_t nWidth = 0;
            uint32_t nHeight = 0;
            float nDpiScale = 1.0f;
            uint32_t nFlags = kViewFlagPerspective;
            SFloat3 Eye;
            SFloat3 Target;
            SFloat3 Up = { 0.0f, 0.0f, 1.0f };
            float nNearPlane = 0.1f;
            float nFarPlane = 1000000.0f;
            float nVerticalFovRadians = 0.785398185f;
            float nOrthographicHeight = 1.0f;
            SMatrix4 ViewMatrix = SMatrix4::Identity();
            SMatrix4 ProjectionMatrix = SMatrix4::Identity();
        };

        /*
        * @brief 视图状态 PDO payload 头。
        */
        struct _RENDER_PDO_EXP SRenderViewStatePDOHeader final
        {
            SRenderPDOHeader Header;
            uint32_t nViewCount = 0;
            uint32_t nActiveViewIndex = 0;
            uint64_t nViewsOffset = 0;          // SRenderViewStateData[nViewCount]
        };

        static_assert(std::is_standard_layout_v<SRenderAABB>);
        static_assert(std::is_trivially_copyable_v<SRenderAABB>);
        static_assert(std::is_standard_layout_v<SRenderPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderPDOHeader>);
        static_assert(std::is_standard_layout_v<SRenderViewStateData>);
        static_assert(std::is_trivially_copyable_v<SRenderViewStateData>);
        static_assert(std::is_standard_layout_v<SRenderMeshPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderMeshPDOHeader>);
        static_assert(std::is_standard_layout_v<SRenderPolylinePDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderPolylinePDOHeader>);
        static_assert(std::is_standard_layout_v<SRenderToolpathPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderToolpathPDOHeader>);
        static_assert(std::is_standard_layout_v<SRenderInstanceListPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderInstanceListPDOHeader>);
        static_assert(std::is_standard_layout_v<SRenderViewStatePDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderViewStatePDOHeader>);
    }
}
