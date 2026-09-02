#pragma once

#include "EntityViewRuntimeExport.h"

#include "Data/Variant.h"
#include "Database/EntityWhere.h"
#include "Resources/ResourceInfo.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Database
    {
        class IRepository;
    }

    namespace Resource
    {
        class CResourceLibrary;
    }

    namespace View
    {
        using ViewID = iCAX::Data::uuid;

        struct _ENTITY_VIEW_RUNTIME_EXP SViewProjectionField final
        {
            std::string Alias;
            std::string ComponentClass;
            std::string PropertyName;
            bool bResourceReference = false;

            auto operator<=>(const SViewProjectionField&) const = default;
        };

        /*
        * @brief View 的一个成员来源。
        * @details 每个 Source 由一个 Database EntityView 维护成员集合，Role 只表达
        *   该集合在上层 View 中的语义，不参与实体查询。
        */
        struct _ENTITY_VIEW_RUNTIME_EXP SViewSourceDefinition final
        {
            std::string SourceID;
            std::string Role;
            iCAX::Database::SEntityWhere Where;
            iCAX::Data::ObjectMap Parameters;
            std::vector<SViewProjectionField> Projection;
        };

        /*
        * @brief 一个可供前端绑定的正式 View。
        * @details 当前组合方式为 Union：同一 Entity 命中多个 Source 时只产生一个
        *   ViewRow，SourceID 合并，投影字段按统一别名契约合并。
        */
        struct _ENTITY_VIEW_RUNTIME_EXP SViewDefinition final
        {
            std::vector<SViewSourceDefinition> Sources;
        };

        struct _ENTITY_VIEW_RUNTIME_EXP SViewHandle final
        {
            ViewID ID;
            uint64_t nRevision = 0;
            iCAX::Resource::CResourceReference Snapshot;
            std::string Format = "ICVW";
        };

        /*
        * @brief 当前 Scene 持有的正式 View 集合。
        * @details EntityView 只作为每个 Source 的 Database 成员索引；CViewSet 负责
        *   Source 组合、Component 投影以及版本化资源快照。前端只绑定 View。
        */
        class _ENTITY_VIEW_RUNTIME_EXP CViewSet final
        {
        public:
            CViewSet(
                IN iCAX::Database::IRepository& Repository_,
                IN iCAX::Resource::CResourceLibrary& Resources_);
            ~CViewSet();

            CViewSet(const CViewSet&) = delete;
            CViewSet& operator=(const CViewSet&) = delete;

            SViewHandle GetOrCreate(IN const SViewDefinition& Definition_);
            bool Release(IN const ViewID& ViewID_);
            void Publish();
            void Clear() noexcept;
            size_t Size() const noexcept;

        private:
            struct SImpl;
            std::unique_ptr<SImpl> m_pImpl;
        };
    }
}
