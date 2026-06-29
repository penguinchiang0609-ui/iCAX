#pragma once

#include "RenderPDOExport.h"

#include <array>
#include <cstdint>

namespace iCAX
{
    namespace RenderPDO
    {
        inline constexpr uint32_t kRenderPDOMagic = 0x4F445052u; // "RPDO", little endian.
        inline constexpr uint32_t kRenderPDOLayoutVersion = 1;

        using RenderObjectID = uint64_t;
        using RenderGeometryID = uint64_t;
        using RenderViewID = uint64_t;
        using RenderDataVersion = uint64_t;
        using RenderStyleID = uint32_t;

        enum class ERenderPDOPayloadKind : uint32_t
        {
            Unknown = 0,
            Mesh = 1,
            Polyline = 2,
            Toolpath = 3,
            InstanceList = 4,
            ViewState = 5
        };

        enum class ERenderGeometryKind : uint32_t
        {
            Unknown = 0,
            Mesh = 1,
            Polyline = 2,
            Toolpath = 3
        };

        enum class ERenderTopology : uint32_t
        {
            Unknown = 0,
            TriangleList = 1,
            LineList = 2,
            LineStrip = 3,
            PointList = 4
        };

        enum class ERenderClass : uint32_t
        {
            Unknown = 0,
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
            Unknown = 0,
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
        inline constexpr uint32_t kRenderFlagDirty = 1u << 4;

        inline constexpr uint32_t kMeshFlagHasNormals = 1u << 0;
        inline constexpr uint32_t kMeshFlagHasVertexColors = 1u << 1;

        inline constexpr uint32_t kViewFlagPerspective = 1u << 0;
        inline constexpr uint32_t kViewFlagOrthographic = 1u << 1;
        inline constexpr uint32_t kViewFlagCameraLocked = 1u << 2;

        struct _RENDER_PDO_EXP SFloat3 final
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct _RENDER_PDO_EXP SRenderAABB final
        {
            SFloat3 Min;
            SFloat3 Max;
        };

        struct _RENDER_PDO_EXP SMatrix4 final
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
    }
}
