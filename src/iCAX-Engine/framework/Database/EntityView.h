#pragma once

#include "EntityWhereEvaluator.h"
#include "IEntityView.h"
#include "IRepositoryEvent.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace iCAX::Database
{
    class IEntity;
    class IRepository;
    class CRepository;

    /*
    * @brief IEntityView 的 Repository 内部实现。
    */
    class CEntityView final
        : public IEntityView
        , public IRepositoryEventListener
    {
    public:
        CEntityView(
            IN std::shared_ptr<CRepository> pRepository_,
            IN SEntityWhere Where_,
            IN iCAX::Data::ObjectMap Parameters_);

        void Initialize();
        void RefreshFromRepository();

        const SEntityWhere& GetWhere() const override;
        const iCAX::Data::ObjectMap& GetParameters() const override;
        uint64_t GetRevision() const override;
        std::vector<iCAX::Data::uuid> GetEntityIDs() const override;
        bool Contains(IN const iCAX::Data::uuid& EntityID_) const override;

        void OnRepositoryChanging(
            IN void* pSender_,
            IN const RepositoryEventArgs& Args_) override;

        void OnRepositoryChanged(
            IN void* pSender_,
            IN const RepositoryEventArgs& Args_) override;

    private:
        using SDependencySet = CEntityWhereEvaluator::SDependencySet;
        using SEvaluationResult = CEntityWhereEvaluator::SEvaluationResult;

        bool EventAffects(
            IN const RepositoryEventArgs::EventType Type_,
            IN const std::string& strClassName_,
            IN const iCAX::Data::PropertySet& PreviousProperties_,
            IN const iCAX::Data::PropertySet& NewProperties_,
            IN const SDependencySet& Dependencies_) const;
        void CollectAffectedEntityIDs(
            IN const RepositoryEventArgs::EventType Type_,
            IN const iCAX::Data::uuid& EntityID_,
            IN const std::string& strClassName_,
            IN const iCAX::Data::PropertySet& PreviousProperties_,
            IN const iCAX::Data::PropertySet& NewProperties_,
            IN OUT std::set<iCAX::Data::uuid>& EntityIDs_) const;
        void ReplaceDependencies(
            IN const iCAX::Data::uuid& EntityID_,
            IN const std::map<iCAX::Data::uuid, SDependencySet>& Dependencies_);
        void ApplyEntityChanges(IN const std::set<iCAX::Data::uuid>& EntityIDs_);

    private:
        std::weak_ptr<CRepository> m_pRepository;
        CEntityWhereEvaluator m_Evaluator;
        mutable std::mutex m_Mutex;
        std::set<iCAX::Data::uuid> m_EntityIDs;
        std::map<
            iCAX::Data::uuid,
            std::map<iCAX::Data::uuid, SDependencySet>> m_DependenciesByEntity;
        std::map<iCAX::Data::uuid, std::set<iCAX::Data::uuid>> m_EntitiesByDependency;
        uint64_t m_nRevision = 0;
    };
}
