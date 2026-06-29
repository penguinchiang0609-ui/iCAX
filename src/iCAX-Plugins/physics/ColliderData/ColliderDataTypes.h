#pragma once

#include "ColliderDataExport.h"

#include <cstdint>
#include <vector>

namespace iCAX
{
    namespace Collider
    {
        using ColliderSceneID = uint64_t;
        using ColliderID = uint64_t;
        using ColliderObjectID = uint64_t;
        using ColliderDataVersion = uint64_t;
        using CollisionLayer = uint32_t;
        using CollisionMask = uint32_t;

        inline constexpr ColliderSceneID kInvalidColliderSceneID = 0;
        inline constexpr ColliderID kInvalidColliderID = 0;
        inline constexpr ColliderObjectID kInvalidColliderObjectID = 0;
        inline constexpr CollisionLayer kDefaultCollisionLayer = 0;
        inline constexpr CollisionMask kCollisionMaskAll = 0xFFFFFFFFu;

        enum class EColliderShapeKind : uint32_t
        {
            Box = 1,
            Sphere = 2,
            Capsule = 3,
            TriangleMesh = 4,
            ConvexHull = 5
        };

        enum class EColliderMotionType : uint32_t
        {
            Static = 0,
            Kinematic = 1,
            Dynamic = 2
        };

        struct _COLLIDER_DATA_EXP SColliderVec3 final
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct _COLLIDER_DATA_EXP SColliderQuat final
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
        };

        struct _COLLIDER_DATA_EXP SColliderTransform final
        {
            SColliderVec3 Position;
            SColliderQuat Rotation;
        };

        struct _COLLIDER_DATA_EXP SColliderMaterial final
        {
            float nDensity = 1000.0f;
            float nFriction = 0.2f;
            float nRestitution = 0.0f;
        };

        struct _COLLIDER_DATA_EXP SBoxColliderData final
        {
            ColliderObjectID nOwnerObjectID = kInvalidColliderObjectID;
            ColliderDataVersion nDataVersion = 1;
            EColliderMotionType eMotionType = EColliderMotionType::Static;
            SColliderTransform Transform;
            SColliderVec3 HalfExtent = { 0.5f, 0.5f, 0.5f };
            SColliderMaterial Material;
            CollisionLayer nLayer = kDefaultCollisionLayer;
            CollisionMask nMask = kCollisionMaskAll;
            bool bActive = true;
        };

        struct _COLLIDER_DATA_EXP SSphereColliderData final
        {
            ColliderObjectID nOwnerObjectID = kInvalidColliderObjectID;
            ColliderDataVersion nDataVersion = 1;
            EColliderMotionType eMotionType = EColliderMotionType::Static;
            SColliderTransform Transform;
            float nRadius = 0.5f;
            SColliderMaterial Material;
            CollisionLayer nLayer = kDefaultCollisionLayer;
            CollisionMask nMask = kCollisionMaskAll;
            bool bActive = true;
        };

        struct _COLLIDER_DATA_EXP SCapsuleColliderData final
        {
            ColliderObjectID nOwnerObjectID = kInvalidColliderObjectID;
            ColliderDataVersion nDataVersion = 1;
            EColliderMotionType eMotionType = EColliderMotionType::Static;
            SColliderTransform Transform;
            float nHalfHeight = 0.5f;
            float nRadius = 0.1f;
            SColliderMaterial Material;
            CollisionLayer nLayer = kDefaultCollisionLayer;
            CollisionMask nMask = kCollisionMaskAll;
            bool bActive = true;
        };

        /*
        * @brief 三角网格碰撞数据。
        * @details
        *   TriangleMesh 通常用于静态模型拾取和场景碰撞。动态复杂 mesh 的成本较高，
        *   产品层应优先用 box/sphere/capsule/convex 组合表达动态刚体。
        */
        struct _COLLIDER_DATA_EXP STriangleMeshColliderData final
        {
            ColliderObjectID nOwnerObjectID = kInvalidColliderObjectID;
            ColliderDataVersion nDataVersion = 1;
            EColliderMotionType eMotionType = EColliderMotionType::Static;
            SColliderTransform Transform;
            std::vector<SColliderVec3> Vertices;
            std::vector<uint32_t> Indices;
            SColliderMaterial Material;
            CollisionLayer nLayer = kDefaultCollisionLayer;
            CollisionMask nMask = kCollisionMaskAll;
            bool bActive = true;
        };

        struct _COLLIDER_DATA_EXP SConvexHullColliderData final
        {
            ColliderObjectID nOwnerObjectID = kInvalidColliderObjectID;
            ColliderDataVersion nDataVersion = 1;
            EColliderMotionType eMotionType = EColliderMotionType::Static;
            SColliderTransform Transform;
            std::vector<SColliderVec3> Points;
            SColliderMaterial Material;
            CollisionLayer nLayer = kDefaultCollisionLayer;
            CollisionMask nMask = kCollisionMaskAll;
            bool bActive = true;
        };

        struct _COLLIDER_DATA_EXP SColliderRaycastRequest final
        {
            SColliderVec3 Origin;
            SColliderVec3 Direction = { 0.0f, 0.0f, -1.0f };
            float nMaxDistance = 1000000.0f;
            CollisionMask nMask = kCollisionMaskAll;
        };

        struct _COLLIDER_DATA_EXP SColliderRaycastHit final
        {
            bool bHit = false;
            ColliderID nColliderID = kInvalidColliderID;
            ColliderObjectID nOwnerObjectID = kInvalidColliderObjectID;
            SColliderVec3 Position;
            SColliderVec3 Normal;
            float nDistance = 0.0f;
        };
    }
}
