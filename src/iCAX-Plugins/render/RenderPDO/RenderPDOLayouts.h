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
        * @brief Mesh PDO payload 头。
        * @details
        *   点、法向、颜色和索引都在同一个 payload 内，通过 offset 定位。offset 为相对 payload 起点的字节偏移。
        */
        struct _RENDER_PDO_EXP SRenderMeshPDOHeader final
        {
            SRenderPDOHeader Header;
            RenderGeometryID nGeometryID = {};
            uint32_t nTopology = static_cast<uint32_t>(ERenderTopology::TriangleList);
            uint32_t nVertexCount = 0;
            uint32_t nIndexCount = 0;
            uint32_t nFlags = 0;
            uint64_t nPositionsOffset = 0;      // SFloat3[nVertexCount]
            uint64_t nNormalsOffset = 0;        // SFloat3[nVertexCount], optional
            uint64_t nVertexColorsOffset = 0;   // uint32_t[nVertexCount], RGBA8, optional
            uint64_t nTextureCoordinatesOffset = 0; // SFloat2[nVertexCount], optional
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
            RenderGeometryID nGeometryID = {};
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
            RenderGeometryID nGeometryID = {};
            SRenderAABB Bounds;
            uint32_t nPointCount = 0;
            uint32_t nSpanCount = 0;
            uint64_t nPointsOffset = 0;         // SRenderToolpathPointData[nPointCount]
            uint64_t nSpansOffset = 0;          // SRenderToolpathSpanData[nSpanCount]
        };

        /*
        * @brief 单个场景对象 PDO payload。
        * @details
        *   一个 ObjectPDO 对应一个 Entity。Transform、Camera、Collider 等 PDO 使用相同 EntityID 时，
        *   前端把它们拼装为同一个可显示对象。材质详情不放在这里，只引用 Render material ID。
        */
        struct _RENDER_PDO_EXP SRenderObjectPDOHeader final
        {
            SRenderPDOHeader Header;
            SceneObjectID nObjectID = {};
            RenderGeometryID nGeometryID = {};
            RenderMaterialID nMaterialID = {};
            uint32_t nGeometryKind = static_cast<uint32_t>(ERenderGeometryKind::Unknown);
            uint32_t nRenderClass = static_cast<uint32_t>(ERenderClass::Unknown);
            uint32_t nFlags = kRenderFlagVisible | kRenderFlagSelectable;
            uint32_t nReserved = 0;
        };

        /*
        * @brief 单个相机 PDO payload。
        * @details
        *   Camera 表达后端控制的渲染相机。它不是 viewport 状态，不包含窗口尺寸、DPI、shader、pipeline 或 GPU 资源。
        *   一个 Camera PDO 对应一个相机 Entity；当前相机通过 nFlags 中的 active 标志表达。
        */
        struct _RENDER_PDO_EXP SRenderCameraPDOHeader final
        {
            SRenderPDOHeader Header;
            RenderCameraID nCameraID = {};
            uint32_t nFlags = kCameraFlagPerspective;
            uint32_t nReserved = 0;
            float nNearPlane = 0.1f;
            float nFarPlane = 1000000.0f;
            float nVerticalFovRadians = 0.785398185f;
            float nOrthographicHeight = 1.0f;
        };

        /*
        * @brief Transform PDO payload。
        * @details
        *   Transform 是 EC 风格的独立组件，只包含 ID 和 local-to-world 矩阵。
        *   Render instance、camera、collider 等组件使用相同 ID 时，前端或其他消费者即可把它们拼到同一个逻辑物体上。
        */
        struct _RENDER_PDO_EXP STransformPDOHeader final
        {
            SRenderPDOHeader Header;
            TransformID nTransformID = {};
            uint32_t nFlags = 0;
            uint32_t nReserved = 0;
            SMatrix4 LocalToWorld = SMatrix4::Identity();
        };

        static_assert(std::is_standard_layout_v<SRenderAABB>);
        static_assert(std::is_trivially_copyable_v<SRenderAABB>);
        static_assert(std::is_standard_layout_v<SRenderPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderPDOHeader>);
        static_assert(sizeof(SRenderID) == 16);
        static_assert(std::is_standard_layout_v<SRenderID>);
        static_assert(std::is_trivially_copyable_v<SRenderID>);
        static_assert(std::is_standard_layout_v<SRenderObjectPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderObjectPDOHeader>);
        static_assert(sizeof(SRenderObjectPDOHeader) == 96);
        static_assert(sizeof(SRenderMeshPDOHeader) == 104);
        static_assert(std::is_standard_layout_v<STransformPDOHeader>);
        static_assert(std::is_trivially_copyable_v<STransformPDOHeader>);
        static_assert(sizeof(STransformPDOHeader) == 120);
        static_assert(std::is_standard_layout_v<SRenderMeshPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderMeshPDOHeader>);
        static_assert(std::is_standard_layout_v<SRenderPolylinePDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderPolylinePDOHeader>);
        static_assert(std::is_standard_layout_v<SRenderToolpathPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderToolpathPDOHeader>);
        static_assert(std::is_standard_layout_v<SRenderCameraPDOHeader>);
        static_assert(std::is_trivially_copyable_v<SRenderCameraPDOHeader>);
        static_assert(sizeof(SRenderPolylinePDOHeader) == 96);
        static_assert(sizeof(SRenderToolpathPDOHeader) == 96);
        static_assert(sizeof(SRenderCameraPDOHeader) == 72);
    }
}
