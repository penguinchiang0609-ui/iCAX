#pragma once

#include "ColliderDataExport.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace iCAX
{
    namespace Collider
    {
        inline constexpr uint32_t kColliderPDOMagic = 0x4F445043u; // "CPDO", little endian.
        inline constexpr uint32_t kColliderPDOLayoutVersion = 1;

        struct _COLLIDER_DATA_EXP SColliderPDOID final
        {
            std::array<uint8_t, 16> Bytes = {};
        };

        using ColliderPDOObjectID = SColliderPDOID;
        using ColliderPDODataVersion = uint64_t;

        enum class EColliderPDOPayloadKind : uint32_t
        {
            Unknown = 0,
            Object = 1
        };

        inline constexpr uint32_t kColliderFlagVisible = 1u << 0;
        inline constexpr uint32_t kColliderFlagEnabled = 1u << 1;
        inline constexpr uint32_t kColliderFlagSelectable = 1u << 2;

        struct _COLLIDER_DATA_EXP SColliderPDOMatrix4 final
        {
            std::array<float, 16> Values = {};

            static SColliderPDOMatrix4 Identity() noexcept
            {
                SColliderPDOMatrix4 _Matrix;
                _Matrix.Values[0] = 1.0f;
                _Matrix.Values[5] = 1.0f;
                _Matrix.Values[10] = 1.0f;
                _Matrix.Values[15] = 1.0f;
                return _Matrix;
            }
        };

        /*
        * @brief Collider PDO 公共头。
        * @details
        *   所有 Collider PDO payload 都以该头开始。nDataVersion 是本 slot 的数据版本；
        *   Transform 变化不需要重发 Collider PDO，因为对象位姿由 Transform PDO 独立表达。
        */
        struct _COLLIDER_DATA_EXP SColliderPDOHeader final
        {
            uint32_t nMagic = kColliderPDOMagic;
            uint32_t nLayoutVersion = kColliderPDOLayoutVersion;
            uint32_t nPayloadKind = static_cast<uint32_t>(EColliderPDOPayloadKind::Unknown);
            uint32_t nHeaderSize = sizeof(SColliderPDOHeader);
            uint64_t nPayloadSize = sizeof(SColliderPDOHeader);
            ColliderPDODataVersion nDataVersion = 0;
        };

        /*
        * @brief 单个 Entity 的 collider payload 头。
        * @details
        *   一个 Object Collider PDO 对应一个 Entity。objectId 与 Transform PDO、Render Object PDO 使用同一 EntityID。
        *   该 payload 下可包含多个基础 shape，用于表达同一部件的多个碰撞包络。
        */
        struct _COLLIDER_DATA_EXP SObjectColliderPDOHeader final
        {
            SColliderPDOHeader Header;
            ColliderPDOObjectID nObjectID = {};
            uint32_t nFlags = kColliderFlagVisible | kColliderFlagEnabled;
            uint32_t nShapeCount = 0;
            uint64_t nShapesOffset = 0; // SColliderShapePDOData[nShapeCount]
        };

        /*
        * @brief 单个 collider shape 记录。
        * @details
        *   nShapeKind 使用 EColliderShapeKind 的数值。LocalMatrix 是 shape 相对所属 Entity 的局部矩阵。
        *   Parameters 固定为 8 个 float：
        *   - Box:      [halfX, halfY, halfZ]
        *   - Sphere:   [radius]
        *   - Cylinder: [radius, length]
        *   - Capsule:  [radius, halfHeight]
        *   - Mesh:     保留，前端只允许线框显示，不用于 render mesh。
        */
        struct _COLLIDER_DATA_EXP SColliderShapePDOData final
        {
            uint32_t nShapeKind = 0;
            uint32_t nFlags = kColliderFlagVisible | kColliderFlagEnabled;
            uint32_t nLayer = 0;
            uint32_t nMask = 0xFFFFFFFFu;
            SColliderPDOMatrix4 LocalMatrix = SColliderPDOMatrix4::Identity();
            std::array<float, 8> Parameters = {};
        };

        static_assert(sizeof(SColliderPDOID) == 16);
        static_assert(sizeof(SColliderPDOHeader) == 32);
        static_assert(sizeof(SObjectColliderPDOHeader) == 64);
        static_assert(sizeof(SColliderShapePDOData) == 112);
        static_assert(std::is_standard_layout_v<SColliderShapePDOData>);
        static_assert(std::is_trivially_copyable_v<SColliderShapePDOData>);
    }
}
