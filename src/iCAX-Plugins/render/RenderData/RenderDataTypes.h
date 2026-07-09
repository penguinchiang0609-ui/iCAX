#pragma once

#include "RenderDataExport.h"

#include <array>
#include <cstdint>
#include <vector>

namespace iCAX
{
    namespace Render
    {
        using RenderSceneID = uint64_t;
        using SceneObjectID = uint64_t;
        using RenderGeometryID = uint64_t;
        using TransformID = uint64_t;
        using RenderCameraID = uint64_t;
        using RenderDataVersion = uint64_t;
        using RenderStyleID = uint32_t;

        inline constexpr RenderSceneID kInvalidRenderSceneID = 0;
        inline constexpr SceneObjectID kInvalidSceneObjectID = 0;
        inline constexpr RenderGeometryID kInvalidRenderGeometryID = 0;
        inline constexpr TransformID kInvalidTransformID = 0;
        inline constexpr RenderCameraID kInvalidRenderCameraID = 0;

        enum class ERenderGeometryKind : uint32_t
        {
            Mesh = 1,
            Polyline = 2,
            Toolpath = 3
        };

        enum class ERenderTopology : uint32_t
        {
            TriangleList = 1,
            LineList = 2,
            LineStrip = 3,
            PointList = 4
        };

        enum class ERenderClass : uint32_t
        {
            Model = 1,
            Stock = 2,
            Fixture = 3,
            Tool = 4,
            ToolpathRapid = 5,
            ToolpathCutting = 6,
            Selection = 7,
            Highlight = 8
        };

        enum class EToolpathMoveKind : uint32_t
        {
            Rapid = 1,
            Cutting = 2,
            Link = 3,
            LeadIn = 4,
            LeadOut = 5,
            Dwell = 6
        };

        inline constexpr uint32_t kRenderFlagVisible = 1u << 0;
        inline constexpr uint32_t kRenderFlagSelectable = 1u << 1;
        inline constexpr uint32_t kRenderFlagHighlighted = 1u << 2;
        inline constexpr uint32_t kRenderFlagSelected = 1u << 3;
        inline constexpr uint32_t kRenderFlagDisabled = 1u << 5;

        inline constexpr uint32_t kRenderMeshFlagHasNormals = 1u << 0;
        inline constexpr uint32_t kRenderMeshFlagHasVertexColors = 1u << 1;

        inline constexpr uint32_t kRenderCameraFlagPerspective = 1u << 0;
        inline constexpr uint32_t kRenderCameraFlagOrthographic = 1u << 1;
        inline constexpr uint32_t kRenderCameraFlagCameraLocked = 1u << 2;

        struct _RENDER_DATA_EXP SFloat3 final
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct _RENDER_DATA_EXP SRenderAABB final
        {
            SFloat3 Min;
            SFloat3 Max;
        };

        struct _RENDER_DATA_EXP SMatrix4 final
        {
            std::array<float, 16> Values = {};

            static SMatrix4 Identity() noexcept
            {
                SMatrix4 _Matrix;
                _Matrix.Values[0] = 1.0f;
                _Matrix.Values[5] = 1.0f;
                _Matrix.Values[10] = 1.0f;
                _Matrix.Values[15] = 1.0f;
                return _Matrix;
            }
        };

        struct _RENDER_DATA_EXP SRenderStyleData final
        {
            RenderStyleID nStyleID = 0;
            uint32_t nColorRGBA = 0xFFFFFFFFu;
            float nLineWidth = 1.0f;
            uint32_t nFlags = 0;
        };

        /*
        * @brief Mesh 渲染数据。
        * @details
        *   RenderData 是中立数据契约，不绑定 PDO、GPU buffer 或具体前端。Positions 必须非空；
        *   Indices 可为空，表示非索引拓扑。nDataVersion 用于跳过未变化数据的重复同步。
        */
        struct _RENDER_DATA_EXP SRenderMeshData final
        {
            RenderGeometryID nGeometryID = kInvalidRenderGeometryID;
            RenderDataVersion nDataVersion = 0;
            ERenderTopology eTopology = ERenderTopology::TriangleList;
            uint32_t nFlags = 0;
            std::vector<SFloat3> Positions;
            std::vector<SFloat3> Normals;
            std::vector<uint32_t> VertexColorsRGBA;
            std::vector<uint32_t> Indices;
        };

        struct _RENDER_DATA_EXP SRenderPolylineRangeData final
        {
            uint32_t nFirstPoint = 0;
            uint32_t nPointCount = 0;
            ERenderClass eRenderClass = ERenderClass::Model;
            RenderStyleID nStyleID = 0;
            uint32_t nFlags = kRenderFlagVisible | kRenderFlagSelectable;
        };

        struct _RENDER_DATA_EXP SRenderPolylineData final
        {
            RenderGeometryID nGeometryID = kInvalidRenderGeometryID;
            RenderDataVersion nDataVersion = 0;
            SRenderAABB Bounds;
            std::vector<SFloat3> Points;
            std::vector<SRenderPolylineRangeData> Ranges;
        };

        struct _RENDER_DATA_EXP SRenderToolpathPointData final
        {
            SFloat3 Position;
            SFloat3 ToolAxis = { 0.0f, 0.0f, 1.0f };
            float nFeed = 0.0f;
            uint32_t nFlags = 0;
        };

        struct _RENDER_DATA_EXP SRenderToolpathSpanData final
        {
            uint32_t nFirstPoint = 0;
            uint32_t nPointCount = 0;
            EToolpathMoveKind eMoveKind = EToolpathMoveKind::Cutting;
            uint32_t nToolID = 0;
            RenderStyleID nStyleID = 0;
            uint32_t nFlags = kRenderFlagVisible | kRenderFlagSelectable;
        };

        struct _RENDER_DATA_EXP SRenderToolpathData final
        {
            RenderGeometryID nGeometryID = kInvalidRenderGeometryID;
            RenderDataVersion nDataVersion = 0;
            SRenderAABB Bounds;
            std::vector<SRenderToolpathPointData> Points;
            std::vector<SRenderToolpathSpanData> Spans;
        };

        struct _RENDER_DATA_EXP SRenderInstanceData final
        {
            SceneObjectID nObjectID = kInvalidSceneObjectID;
            RenderGeometryID nGeometryID = kInvalidRenderGeometryID;
            ERenderGeometryKind eGeometryKind = ERenderGeometryKind::Mesh;
            ERenderClass eRenderClass = ERenderClass::Model;
            RenderStyleID nStyleID = 0;
            uint32_t nFlags = kRenderFlagVisible | kRenderFlagSelectable;
        };

        /*
        * @brief 场景 Transform 组件。
        * @details
        *   Transform 只表达 ID 与 local-to-world 矩阵，不知道自己属于 render object、camera、collider 或其他组件。
        *   同一 scene 内其他组件使用相同 ID 时，即视为挂载在同一个逻辑物体上。
        */
        struct _RENDER_DATA_EXP STransformData final
        {
            TransformID nTransformID = kInvalidTransformID;
            RenderDataVersion nDataVersion = 0;
            uint32_t nFlags = 0;
            SMatrix4 LocalToWorld = SMatrix4::Identity();
        };

        struct _RENDER_DATA_EXP SRenderCameraData final
        {
            RenderCameraID nCameraID = kInvalidRenderCameraID;
            uint32_t nFlags = kRenderCameraFlagPerspective;
            float nNearPlane = 0.1f;
            float nFarPlane = 1000000.0f;
            float nVerticalFovRadians = 0.785398185f;
            float nOrthographicHeight = 1.0f;
        };
    }
}
