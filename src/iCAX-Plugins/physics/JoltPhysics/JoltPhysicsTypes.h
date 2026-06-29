#pragma once

#include "JoltPhysicsExport.h"

#include <cstdint>

namespace iCAX
{
    namespace JoltPhysics
    {
        using PhysicsSceneID = uint64_t;
        using PhysicsBodyID = uint64_t;
        using PhysicsObjectID = uint64_t;

        inline constexpr PhysicsSceneID kInvalidPhysicsSceneID = 0;
        inline constexpr PhysicsBodyID kInvalidPhysicsBodyID = 0;

        enum class EPhysicsBodyMotionType : uint32_t
        {
            Static = 0,
            Kinematic = 1,
            Dynamic = 2
        };

        enum class EPhysicsShapeKind : uint32_t
        {
            Box = 1,
            Sphere = 2,
            Capsule = 3,
            Mesh = 4
        };

        struct _JOLT_PHYSICS_EXP SPhysicsVec3 final
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct _JOLT_PHYSICS_EXP SPhysicsQuat final
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
        };

        struct _JOLT_PHYSICS_EXP SPhysicsTransform final
        {
            SPhysicsVec3 Position;
            SPhysicsQuat Rotation;
        };

        struct _JOLT_PHYSICS_EXP SBoxBodyDesc final
        {
            PhysicsObjectID nOwnerObjectID = 0;
            EPhysicsBodyMotionType eMotionType = EPhysicsBodyMotionType::Static;
            SPhysicsTransform Transform;
            SPhysicsVec3 HalfExtent = { 0.5f, 0.5f, 0.5f };
            float nDensity = 1000.0f;
            float nFriction = 0.2f;
            float nRestitution = 0.0f;
            bool bActive = true;
        };

        struct _JOLT_PHYSICS_EXP SSphereBodyDesc final
        {
            PhysicsObjectID nOwnerObjectID = 0;
            EPhysicsBodyMotionType eMotionType = EPhysicsBodyMotionType::Static;
            SPhysicsTransform Transform;
            float nRadius = 0.5f;
            float nDensity = 1000.0f;
            float nFriction = 0.2f;
            float nRestitution = 0.0f;
            bool bActive = true;
        };

        /*
        * @brief 三角网格刚体描述。
        * @details
        *   Mesh collider 主要用于模型拾取和静态碰撞场景。pVertices/pIndices 只在 CreateTriangleMeshBody 调用期间读取，
        *   调用返回后不会保存外部指针。nIndexCount 必须是 3 的倍数，三角面使用 uint32_t 顶点索引。
        */
        struct _JOLT_PHYSICS_EXP STriangleMeshBodyDesc final
        {
            PhysicsObjectID nOwnerObjectID = 0;
            EPhysicsBodyMotionType eMotionType = EPhysicsBodyMotionType::Static;
            SPhysicsTransform Transform;
            const SPhysicsVec3* pVertices = nullptr;
            uint32_t nVertexCount = 0;
            const uint32_t* pIndices = nullptr;
            uint32_t nIndexCount = 0;
            float nFriction = 0.2f;
            float nRestitution = 0.0f;
            bool bActive = true;
        };

        struct _JOLT_PHYSICS_EXP SPhysicsRaycastRequest final
        {
            SPhysicsVec3 Origin;
            SPhysicsVec3 Direction = { 0.0f, 0.0f, -1.0f };
            float nMaxDistance = 1000000.0f;
        };

        struct _JOLT_PHYSICS_EXP SPhysicsRaycastHit final
        {
            bool bHit = false;
            PhysicsBodyID nBodyID = kInvalidPhysicsBodyID;
            PhysicsObjectID nOwnerObjectID = 0;
            SPhysicsVec3 Position;
            SPhysicsVec3 Normal;
            float nDistance = 0.0f;
        };
    }
}
