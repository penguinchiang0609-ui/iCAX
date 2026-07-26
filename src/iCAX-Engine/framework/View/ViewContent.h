#pragma once

#include "ViewExport.h"

#include "Data/Variant.h"
#include "Data/uuid.h"
#include "Database/IEntityView.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        class ISceneContext;
    }

    namespace View
    {
        using ViewDefinitionID = std::string;

        /*
        * @brief 一个 View 对 Scene Entity 的运行时投影结果。
        * @details EntityID 只用于把 View 内容与 Scene 级 PDO 对齐，不持久化为 Entity 的 View 归属。
        *   Presentation 是 View 局部的可选表现覆盖，不会写回 Scene Database。
        */
        struct _VIEW_EXP SViewObject final
        {
            iCAX::Data::uuid EntityID;
            iCAX::Data::ObjectMap Presentation;
        };

        /*
        * @brief View 内容快照。
        * @details 快照是可丢弃、可重建的派生数据。Revision 只在内容发生变化时递增。
        */
        struct _VIEW_EXP SViewContent final
        {
            ViewDefinitionID DefinitionID;
            uint64_t nRevision = 0;
            std::vector<SViewObject> Objects;
        };

        /*
        * @brief 产品可选提供的 View 局部表现解析器。
        * @details
        *   Entity 成员关系由 Database IEntityView 维护；该接口只计算不写回 Database 的表现覆盖。
        */
        class _VIEW_EXP IViewPresentationProvider
        {
        public:
            virtual ~IViewPresentationProvider() = default;

            virtual iCAX::Data::ObjectMap Resolve(
                IN iCAX::Project::ISceneContext& Scene_,
                IN const iCAX::Data::uuid& EntityID_,
                IN const iCAX::Data::ObjectMap& Context_) const = 0;
        };

        struct _VIEW_EXP SViewDefinition final
        {
            ViewDefinitionID ID;
            iCAX::Database::SEntityWhere EntityWhere;
            std::shared_ptr<const IViewPresentationProvider> pPresentationProvider;
        };

        /*
        * @brief 通用 View Definition 注册表。
        * @details 只管理不透明 DefinitionID 与 Provider，不包含任何产品枚举或业务判断。
        */
        class _VIEW_EXP CViewDefinitionRegistry final
        {
        public:
            bool Register(IN SViewDefinition Definition_);
            bool HasDefinition(IN const ViewDefinitionID& DefinitionID_) const;
            std::optional<SViewDefinition> GetDefinition(
                IN const ViewDefinitionID& DefinitionID_) const;
            std::vector<ViewDefinitionID> ListDefinitionIDs() const;

        private:
            mutable std::mutex m_Mutex;
            std::map<ViewDefinitionID, SViewDefinition> m_Definitions;
        };
    }
}
