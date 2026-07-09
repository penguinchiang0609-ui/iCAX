#pragma once

#include "Data/uuid.h"
#include "ProjectContextExport.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        using SceneRuntimeID = uint64_t;
        using SceneObjectID = SceneRuntimeID;
        using SceneGeometryID = SceneRuntimeID;
        using SceneColliderID = SceneRuntimeID;
        using TransformID = SceneRuntimeID;

        inline constexpr SceneRuntimeID kInvalidSceneRuntimeID = 0;
        inline constexpr SceneObjectID kInvalidSceneObjectID = 0;
        inline constexpr SceneGeometryID kInvalidSceneGeometryID = 0;
        inline constexpr SceneColliderID kInvalidSceneColliderID = 0;
        inline constexpr TransformID kInvalidTransformID = 0;

        /*
        * @brief SceneObject 的外部别名。
        * @details
        *   Alias 是 framework 不解释的字符串键。产品、渲染、碰撞或其他插件可以用自己的命名空间
        *   绑定 SceneObject，例如 "render.camera/default"、"physics.body/42"。
        */
        struct _PROJECT_CONTEXT_EXP CSceneObjectAlias final
        {
            std::string Namespace;
            std::string Key;
        };

        /*
        * @brief Scene 运行期对象绑定。
        * @details
        *   EntityID 非空时表示该对象来自 Repository Entity；Aliases 用于非 Entity 运行期对象，
        *   或者用于把外部系统 ID 绑定到已有 Entity 对象。Registry 不理解 alias 的业务含义。
        */
        struct _PROJECT_CONTEXT_EXP CSceneObjectBinding final
        {
            SceneObjectID nObjectID = kInvalidSceneObjectID; //!< Scene 内运行期对象 ID。
            TransformID nTransformID = kInvalidTransformID; //!< 对象 Transform ID；默认与 nObjectID 相同。
            iCAX::Data::uuid EntityID; //!< Entity 来源对象；非 Entity 对象时为空 UUID。
            std::vector<CSceneObjectAlias> Aliases; //!< framework 不解释的外部别名。
            std::string Name; //!< 调试/显示名称，不参与身份判断。
        };

        /*
        * @brief Scene 几何绑定。
        * @details
        *   同一个 ResourceID 可以按不同 Purpose 产生不同几何，例如 render.mesh、wireframe、collider.shape。
        */
        struct _PROJECT_CONTEXT_EXP CSceneGeometryBinding final
        {
            SceneGeometryID nGeometryID = kInvalidSceneGeometryID;
            std::string ResourceID;
            std::string Purpose;
        };

        /*
        * @brief Scene 碰撞体绑定。
        * @details
        *   ColliderID 是 iCAX Scene 层身份，不是 Jolt/PhysX 等第三方引擎内部 BodyID。
        */
        struct _PROJECT_CONTEXT_EXP CSceneColliderBinding final
        {
            SceneColliderID nColliderID = kInvalidSceneColliderID;
            SceneObjectID nObjectID = kInvalidSceneObjectID;
            std::string ShapeResourceID;
            std::string Purpose;
        };

        /*
        * @brief Scene 运行期对象注册表。
        * @details
        *   Registry 统一维护 Entity、外部 alias、Resource、Transform 和 Collider 之间的运行期 ID 映射。
        *   它不持久化，不跨 Scene 共享，不做线程同步；调用方应在 Scene 工作线程访问它。
        *   Registry 不预设产品对象类型，所有非 Entity 来源都通过 alias namespace/key 表达。
        */
        class _PROJECT_CONTEXT_EXP CSceneObjectRegistry final
        {
        public:
            CSceneObjectRegistry() = default;

            /*
            * @brief 获取或创建 Entity 对应的 SceneObject。
            * @param [in] EntityID_ Repository 中的 Entity ID，不能为空 UUID。
            * @return Scene 内运行期对象 ID。
            */
            SceneObjectID GetOrCreateEntityObject(IN const iCAX::Data::uuid& EntityID_);

            /*
            * @brief 获取或创建 alias 对应的 SceneObject。
            * @param [in] strAliasNamespace_ alias 命名空间，由调用方定义，例如 "render.camera"。
            * @param [in] strAliasKey_ namespace 内稳定 key，例如 "default"。
            * @param [in] strName_ 可选显示/调试名称。
            */
            SceneObjectID GetOrCreateAliasObject(
                IN const std::string& strAliasNamespace_,
                IN const std::string& strAliasKey_,
                IN const std::string& strName_ = std::string());

            /*
            * @brief 根据 SceneObjectID 查找绑定。
            */
            const CSceneObjectBinding* FindObject(IN SceneObjectID nObjectID_) const;

            /*
            * @brief 根据 EntityID 查找 SceneObjectID。
            */
            std::optional<SceneObjectID> FindObjectByEntity(IN const iCAX::Data::uuid& EntityID_) const;

            /*
            * @brief 给已有 SceneObject 绑定 alias。
            * @details
            *   alias 已绑定到同一对象时返回 true；alias 已绑定到其他对象时抛出异常。
            */
            bool BindAlias(
                IN SceneObjectID nObjectID_,
                IN const std::string& strAliasNamespace_,
                IN const std::string& strAliasKey_);

            /*
            * @brief 根据 alias 查找 SceneObjectID。
            */
            std::optional<SceneObjectID> FindObjectByAlias(
                IN const std::string& strAliasNamespace_,
                IN const std::string& strAliasKey_) const;

            /*
            * @brief 根据 TransformID 查找 SceneObjectID。
            */
            std::optional<SceneObjectID> FindObjectByTransform(IN TransformID nTransformID_) const;

            /*
            * @brief 获取对象 TransformID。
            * @throws std::out_of_range 对象不存在时抛出。
            */
            TransformID GetTransformID(IN SceneObjectID nObjectID_) const;

            /*
            * @brief 反查 EntityID。
            * @return Entity 对象返回真实 EntityID；非 Entity 或不存在时返回 nil UUID。
            */
            iCAX::Data::uuid GetEntityID(IN SceneObjectID nObjectID_) const;

            /*
            * @brief 获取或创建资源派生几何 ID。
            */
            SceneGeometryID GetOrCreateGeometry(
                IN const std::string& strResourceID_,
                IN const std::string& strPurpose_);

            /*
            * @brief 查找资源派生几何 ID。
            */
            std::optional<SceneGeometryID> FindGeometry(
                IN const std::string& strResourceID_,
                IN const std::string& strPurpose_) const;

            /*
            * @brief 根据 GeometryID 查找绑定。
            */
            const CSceneGeometryBinding* FindGeometry(IN SceneGeometryID nGeometryID_) const;

            /*
            * @brief 获取或创建对象碰撞体 ID。
            */
            SceneColliderID GetOrCreateCollider(
                IN SceneObjectID nObjectID_,
                IN const std::string& strShapeResourceID_,
                IN const std::string& strPurpose_);

            /*
            * @brief 根据 ColliderID 查找绑定。
            */
            const CSceneColliderBinding* FindCollider(IN SceneColliderID nColliderID_) const;

            /*
            * @brief 根据 ColliderID 反查 SceneObjectID。
            */
            std::optional<SceneObjectID> FindObjectByCollider(IN SceneColliderID nColliderID_) const;

            /*
            * @brief 移除 SceneObject 及其 Collider 绑定。
            * @details ID 不会回收，避免前端延迟事件误命中新对象。
            */
            bool RemoveObject(IN SceneObjectID nObjectID_);

            /*
            * @brief 移除资源派生几何绑定。
            */
            bool RemoveGeometry(IN SceneGeometryID nGeometryID_);

            /*
            * @brief 移除碰撞体绑定。
            */
            bool RemoveCollider(IN SceneColliderID nColliderID_);

            /*
            * @brief 清空 Registry。
            * @details 仅用于 Scene 关闭或完整重建；运行中的 Scene 不应调用它。
            */
            void Clear();

        private:
            SceneRuntimeID AllocateID();
            static std::string MakeAliasKey(IN const std::string& strAliasNamespace_, IN const std::string& strAliasKey_);
            static std::string MakeGeometryKey(IN const std::string& strResourceID_, IN const std::string& strPurpose_);
            static std::string MakeColliderKey(
                IN SceneObjectID nObjectID_,
                IN const std::string& strShapeResourceID_,
                IN const std::string& strPurpose_);

        private:
            SceneRuntimeID m_nNextID = 1;
            std::unordered_map<SceneObjectID, CSceneObjectBinding> m_Objects;
            std::unordered_map<iCAX::Data::uuid, SceneObjectID> m_ObjectByEntity;
            std::unordered_map<std::string, SceneObjectID> m_ObjectByAlias;
            std::unordered_map<TransformID, SceneObjectID> m_ObjectByTransform;
            std::unordered_map<SceneGeometryID, CSceneGeometryBinding> m_Geometries;
            std::unordered_map<std::string, SceneGeometryID> m_GeometryByKey;
            std::unordered_map<SceneColliderID, CSceneColliderBinding> m_Colliders;
            std::unordered_map<std::string, SceneColliderID> m_ColliderByKey;
        };
    }
}
