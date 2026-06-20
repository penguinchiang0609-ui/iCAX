#include "pch.h"
#include "Repository.h"
#include "MetaRegistry.h"
#include "ChangeLog.h"
#include "RepositoryHistory.h"

#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace
{
    bool ShouldAffectVersionAndDerived(
        IN iCAX::Database::IMetaRegistry& Meta_,
        IN const std::string& ComponentClass_,
        IN const std::string& PropertyName_)
    {
        if (!Meta_.HasPropertyByName(ComponentClass_, PropertyName_))
        {
            return !Meta_.HasTypeByName(ComponentClass_);
        }

        return Meta_.GetPropertyKindByName(ComponentClass_, PropertyName_) == iCAX::Database::EPropertyKind::Value
            && Meta_.GetPropertyChangePolicyByName(ComponentClass_, PropertyName_) != iCAX::Database::EPropertyChangePolicy::Silent;
    }

    std::vector<std::string> GetInvalidationPropertyNames(
        IN iCAX::Database::IMetaRegistry& Meta_,
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_)
    {
        if (!pComponent_)
        {
            return {};
        }

        const auto _strComponentClass = pComponent_->GetComponentClass();
        if (Meta_.HasTypeByName(_strComponentClass))
        {
            return Meta_.GetPropertyNames(_strComponentClass);
        }

        return pComponent_->GetPropertyNameArray();
    }

    std::vector<std::string> GetInvalidationPropertyNames(
        IN iCAX::Database::IMetaRegistry& Meta_,
        IN const std::string& strComponentClass_,
        IN const iCAX::Data::PropertySet& Properties_)
    {
        if (Meta_.HasTypeByName(strComponentClass_))
        {
            return Meta_.GetPropertyNames(strComponentClass_);
        }

        std::vector<std::string> _vecNames;
        for (const auto& [_strName, _] : Properties_)
        {
            _vecNames.push_back(_strName);
        }
        return _vecNames;
    }

    using ComponentVersionKey = std::tuple<iCAX::Data::uuid, std::string>;
    using PropertyInvalidationKey = std::tuple<iCAX::Data::uuid, std::string, std::string>;
}

namespace iCAX
{
    namespace Database
    {
        class CRepositoryChangeScope final : public IRepositoryChangeScope
        {
        public:
            CRepositoryChangeScope(IN CRepository& Repository_, IN const bool bAutoCommitOnDestroy_)
                : m_Repository(Repository_)
                , m_bCompleted(false)
                , m_bAutoCommitOnDestroy(bAutoCommitOnDestroy_)
            {
            }

            ~CRepositoryChangeScope() override
            {
                if (!m_bCompleted)
                {
                    try
                    {
                        if (m_bAutoCommitOnDestroy)
                        {
                            Commit();
                        }
                        else
                        {
                            Cancel();
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }

            void Commit() override
            {
                if (m_bCompleted)
                {
                    return;
                }
                m_bCompleted = true;
                m_Repository.EndChangeScope(true);
            }

            void Cancel() override
            {
                if (m_bCompleted)
                {
                    return;
                }
                m_bCompleted = true;
                m_Repository.EndChangeScope(false);
            }

            bool IsCompleted() const override
            {
                return m_bCompleted;
            }

        private:
            CRepository& m_Repository;
            bool m_bCompleted;
            bool m_bAutoCommitOnDestroy;
        };

        class CRepositoryEventSuppressor final
        {
        public:
            CRepositoryEventSuppressor(IN CRepository& Repository_)
                : m_Repository(Repository_)
            {
                ++m_Repository.m_nRepositoryEventSuppressionDepth;
            }

            ~CRepositoryEventSuppressor()
            {
                --m_Repository.m_nRepositoryEventSuppressionDepth;
            }

            CRepositoryEventSuppressor(IN const CRepositoryEventSuppressor&) = delete;
            CRepositoryEventSuppressor& operator=(IN const CRepositoryEventSuppressor&) = delete;

        private:
            CRepository& m_Repository;
        };
    }
}

iCAX::Database::CRepository::CRepository(IN const iCAX::Data::uuid& UID_)
    : CRepository(UID_, GetGlobalMetaRegistry())
{
}

iCAX::Database::CRepository::CRepository(IN const iCAX::Data::uuid& UID_, IN std::shared_ptr<IMetaRegistry> pMetaRegistry_)
    : m_pMetaRegistry(pMetaRegistry_ ? std::move(pMetaRegistry_) : GetGlobalMetaRegistry())
{
    m_UID = UID_;
    m_pHistory = std::make_unique<CRepositoryHistory>(m_pMetaRegistry);
    m_pVerisonTable = std::make_shared<VersionTable>();
    m_pDerivedPropertyManager = std::make_shared<CDerivedPropertyManager>(*this);
}

iCAX::Database::CRepository::~CRepository()
{
}

const iCAX::Data::uuid& iCAX::Database::CRepository::GetID() const
{
    return m_UID;
}

std::shared_ptr<iCAX::Database::IMetaRegistry> iCAX::Database::CRepository::GetMetaRegistry() const
{
    return m_pMetaRegistry;
}

std::unique_ptr<iCAX::Database::IRepositoryChangeScope> iCAX::Database::CRepository::BeginChangeScope(IN EChangeScopeKind Kind_, IN const std::string& strName_)
{
    return BeginChangeScopeCore(Kind_, strName_, true);
}

std::unique_ptr<iCAX::Database::IRepositoryChangeScope> iCAX::Database::CRepository::BeginTransaction(IN const std::string& strName_)
{
    return BeginChangeScopeCore(EChangeScopeKind::Transaction, strName_, false);
}

std::unique_ptr<iCAX::Database::IRepositoryUndoScope> iCAX::Database::CRepository::BeginUndoCommand(IN const std::string& strName_)
{
    if (strName_.empty())
    {
        throw std::invalid_argument("Undo command name cannot be empty");
    }
    if (IsChangeScopeActive())
    {
        throw std::runtime_error("Undo scope cannot begin inside an active repository change scope");
    }

    return m_pHistory->BeginCommand(strName_);
}

bool iCAX::Database::CRepository::IsUndoCommandRecording() const
{
    return m_pHistory->IsRecording();
}

bool iCAX::Database::CRepository::CanUndo() const
{
    if (IsChangeScopeActive() || IsUndoCommandRecording() || m_nHistoryReplayDepth > 0)
    {
        return false;
    }
    return m_pHistory->CanUndo();
}

bool iCAX::Database::CRepository::CanRedo() const
{
    if (IsChangeScopeActive() || IsUndoCommandRecording() || m_nHistoryReplayDepth > 0)
    {
        return false;
    }
    return m_pHistory->CanRedo();
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepository::GetUndoArray() const
{
    return m_pHistory->GetUndoArray();
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepository::GetRedoArray() const
{
    return m_pHistory->GetRedoArray();
}

bool iCAX::Database::CRepository::Undo()
{
    if (!CanUndo())
    {
        return false;
    }

    auto _StepName = m_pHistory->GetUndoStepName();
    auto _Inverse = MakeInverseChangeSet(m_pHistory->GetUndoChangeSet(), _StepName.empty() ? "Undo" : "Undo " + _StepName);

    ++m_nHistoryReplayDepth;
    try
    {
        ApplyChangeSetWithReplayEvent(_Inverse);
        --m_nHistoryReplayDepth;
    }
    catch (...)
    {
        --m_nHistoryReplayDepth;
        throw;
    }

    m_pHistory->MoveUndoToRedo();
    AppendOperationLog(_Inverse);
    return true;
}

bool iCAX::Database::CRepository::Redo()
{
    if (!CanRedo())
    {
        return false;
    }

    auto _ChangeSet = m_pHistory->GetRedoChangeSet();

    ++m_nHistoryReplayDepth;
    try
    {
        ApplyChangeSetWithReplayEvent(_ChangeSet);
        --m_nHistoryReplayDepth;
    }
    catch (...)
    {
        --m_nHistoryReplayDepth;
        throw;
    }

    m_pHistory->MoveRedoToUndo();
    AppendOperationLog(_ChangeSet);
    return true;
}

std::unique_ptr<iCAX::Database::IRepositoryChangeScope> iCAX::Database::CRepository::BeginChangeScopeCore(IN EChangeScopeKind Kind_, IN const std::string& strName_, IN const bool bAutoCommitOnDestroy_)
{
    if (IsChangeScopeActive())
    {
        throw std::runtime_error("Nested repository change scopes are not supported");
    }

    m_nChangeScopeKind = Kind_;
    m_strChangeScopeName = strName_;
    m_pChangeSetBuilder = std::make_unique<CChangeSetBuilder>(Kind_, strName_);
    return std::make_unique<CRepositoryChangeScope>(*this, bAutoCommitOnDestroy_);
}

void iCAX::Database::CRepository::OpenOperationLog(IN const std::string& strPath_, IN const bool bTruncate_)
{
    if (!m_pOperationLog)
    {
        m_pOperationLog = std::make_unique<CChangeSetJournal>();
    }
    m_pOperationLog->Open(strPath_, bTruncate_);
}

void iCAX::Database::CRepository::CloseOperationLog()
{
    if (m_pOperationLog)
    {
        m_pOperationLog->Close();
    }
}

bool iCAX::Database::CRepository::HasOperationLog() const
{
    return m_pOperationLog && m_pOperationLog->IsOpen();
}

void iCAX::Database::CRepository::ReplayOperationLog(IN const std::string& strPath_)
{
    CChangeSetJournal _Journal;
    const auto _ChangeSets = _Journal.ReadAll(strPath_);

    m_pHistory->Clear();

    ++m_nHistoryReplayDepth;
    try
    {
        for (const auto& _ChangeSet : _ChangeSets)
        {
            ApplyChangeSetWithReplayEvent(_ChangeSet);
        }
        --m_nHistoryReplayDepth;
    }
    catch (...)
    {
        --m_nHistoryReplayDepth;
        throw;
    }
}

void iCAX::Database::CRepository::Initialzie()
{
    auto _pMetaEntity = std::make_shared<CEntity>(shared_from_this(), m_UID);
    _pMetaEntity->AddObserver(shared_from_this());
    m_mapEntities[m_UID] = _pMetaEntity;

    m_pEntitesView = std::make_shared<CEntitiesView>(shared_from_this());
    AddObserver(m_pEntitesView);
}

std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CRepository::CreateEntity(IN const iCAX::Data::uuid& ID_)
{
    if (ID_ == m_UID)
    {
        return GetEntity(m_UID);
    }
    auto _Ite = m_mapEntities.find(ID_);
    if (_Ite != m_mapEntities.end())
    {
        throw std::runtime_error("Entity already exists: " + to_string(ID_));
    }

    auto _pEntity = std::make_shared<CEntity>(shared_from_this(), ID_);
    TriggerRepositoryChanging(RepositoryEventArgs::kAddEntity, ID_, {}, {}, {}, {}, _pEntity);
    m_mapEntities[ID_] = _pEntity;
    _pEntity->AddObserver(shared_from_this());
    TriggerRepositoryChanged(RepositoryEventArgs::kAddEntity, ID_, {}, {}, {}, {}, _pEntity);
    return _pEntity;
}

bool iCAX::Database::CRepository::HasEntity(IN const iCAX::Data::uuid& ID_) const
{
    return m_mapEntities.find(ID_) != m_mapEntities.end();
}

void iCAX::Database::CRepository::DeleteEntity(IN const iCAX::Data::uuid& ID_)
{
    if (ID_ == m_UID)
    {
        return;
    }

    auto _Ite = m_mapEntities.find(ID_);
    if (_Ite == m_mapEntities.end())
    {
        return;
    }

    _Ite->second->Cleanup();
    _Ite->second->RemoveObserver(shared_from_this());
    auto _pEntity = _Ite->second;
    TriggerRepositoryChanging(RepositoryEventArgs::kDeleteEntity, ID_, {}, {}, {}, {}, _pEntity);
    m_mapEntities.erase(_Ite);
    TriggerRepositoryChanged(RepositoryEventArgs::kDeleteEntity, ID_, {}, {}, {}, {}, _pEntity);
}

std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CRepository::GetEntity(IN const iCAX::Data::uuid& ID_) const
{
    auto _Ite = m_mapEntities.find(ID_);
    if (_Ite != m_mapEntities.end())
    {
        return _Ite->second;
    }

    return nullptr;
}

std::vector<std::shared_ptr<iCAX::Database::IEntity>> iCAX::Database::CRepository::FilterEntities(IN std::function<bool(std::shared_ptr<IEntity>)> funcWhere_) const
{
    std::vector<std::shared_ptr<IEntity>> _vecTemp;
    for (const auto& [_, _pEntity] : m_mapEntities)
    {
        if (funcWhere_(_pEntity))
        {
            _vecTemp.push_back(_pEntity);
        }
    }
    return _vecTemp;
}

int iCAX::Database::CRepository::EntityCount() const
{
    return (int)m_mapEntities.size();
}

std::vector<iCAX::Data::uuid> iCAX::Database::CRepository::GetEntityIDs() const
{
    std::vector<iCAX::Data::uuid> _vecTemp;
    for (const auto& [_ID, _] : m_mapEntities)
    {
        _vecTemp.push_back(_ID);
    }
    return _vecTemp;
}

iCAX::Database::IEntitiesView& iCAX::Database::CRepository::GetView() const
{
    if (!m_pEntitesView)
    {
        throw std::runtime_error("Repository entity view is missing");
    }
    return *m_pEntitesView;
}

std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CRepository::GetMetaEntity()
{
    return GetEntity(m_UID);
}

void iCAX::Database::CRepository::CleanUp(IN const bool& bForced_)
{
    auto _vecIDs = GetEntityIDs();
    for (auto _Ite = _vecIDs.begin(); _Ite != _vecIDs.end(); ++_Ite)
    {
        if (*_Ite == m_UID)
        {
            _vecIDs.erase(_Ite);
            break;
        }
    }

    for (auto& _ID : _vecIDs)
    {
        DeleteEntity(_ID);
    }

    if (bForced_)
    {
        auto _Ite = m_mapEntities.find(m_UID);
        if (_Ite != m_mapEntities.end())
        {
            _Ite->second->Cleanup();
            auto _pEntity = _Ite->second;
            TriggerRepositoryChanging(RepositoryEventArgs::kDeleteEntity, m_UID, {}, {}, {}, {}, _pEntity);
            _Ite->second->RemoveObserver(shared_from_this());
            m_mapEntities.erase(_Ite);
            TriggerRepositoryChanged(RepositoryEventArgs::kDeleteEntity, m_UID, {}, {}, {}, {}, _pEntity);
        }
    }

    if (m_pDerivedPropertyManager)
    {
        m_pDerivedPropertyManager->Clear();
    }
}

bool iCAX::Database::CRepository::IsValid()
{
    return HasEntity(m_UID);
}

size_t iCAX::Database::CRepository::GetComponentVersion(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
{
    return m_pVerisonTable->GetVersion({ nEntityID_, strComponentType_ });
}

void iCAX::Database::CRepository::BumpComponentVersion(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
{
    m_pVerisonTable->BumpVersion({ nEntityID_, strComponentType_ });
}

iCAX::Data::PropertyValue iCAX::Database::CRepository::EvaluateDerivedProperty(IN const CComponentBase& Component_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_)
{
    return m_pDerivedPropertyManager->Evaluate(Component_, strPropertyName_, Evaluator_);
}

bool iCAX::Database::CRepository::IsComponentChanged(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
{
    return m_pVerisonTable->IsDirty({ nEntityID_, strComponentType_ });
}

void iCAX::Database::CRepository::ResetComponentChangedFlag()
{
    m_pVerisonTable->ClearDirty();
}

void iCAX::Database::CRepository::OnEntityChanging(IN void* pSender_, IN const EntityEventArgs& Args_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsChangeScopeActive())
    {
        return;
    }

    TriggerRepositoryChanging((RepositoryEventArgs::EventType)(Args_.nType), Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity);
}

void iCAX::Database::CRepository::OnEntityChanged(IN void* pSender_, IN const EntityEventArgs& Args_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsChangeScopeActive())
    {
        TriggerRepositoryChanged((RepositoryEventArgs::EventType)(Args_.nType), Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity);
        return;
    }

    ApplyEntityChangedEffects(Args_);

    TriggerRepositoryChanged((RepositoryEventArgs::EventType)(Args_.nType), Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity);
}

void iCAX::Database::CRepository::ApplyEntityChangedEffects(IN const EntityEventArgs& Args_)
{
    if (Args_.nType == iCAX::Database::EntityEventArgs::kModifyComponent)
    {
        bool _bNeedBumpVersion = false;
        for (const auto& [_strPropertyName, _] : Args_.NewProperties)
        {
            if (ShouldAffectVersionAndDerived(*m_pMetaRegistry, Args_.strClassName, _strPropertyName))
            {
                _bNeedBumpVersion = true;
                m_pDerivedPropertyManager->Invalidate({ Args_.EntityID, Args_.strClassName, _strPropertyName });
            }
        }

        if (_bNeedBumpVersion)
        {
            m_pVerisonTable->BumpVersion({ Args_.EntityID, Args_.strClassName });
        }
    }
    else if (Args_.nType == iCAX::Database::EntityEventArgs::kAddComponent)
    {
        for (const auto& _strPropertyName : GetInvalidationPropertyNames(*m_pMetaRegistry, Args_.pComponent))
        {
            m_pDerivedPropertyManager->Invalidate({ Args_.EntityID, Args_.strClassName, _strPropertyName });
        }
        m_pVerisonTable->Reset({ Args_.EntityID, Args_.strClassName });
    }
    else if (Args_.nType == iCAX::Database::EntityEventArgs::kRemoveComponent)
    {
        for (const auto& [_strPropertyName, _] : Args_.PreviousProperties)
        {
            m_pDerivedPropertyManager->Invalidate({ Args_.EntityID, Args_.strClassName, _strPropertyName });
        }
        m_pDerivedPropertyManager->RemoveComponent(Args_.EntityID, Args_.strClassName);
        m_pVerisonTable->Remove({ Args_.EntityID, Args_.strClassName });
    }
}

void iCAX::Database::CRepository::AddObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_)
{
    m_Observers.push_back(Observer_);
}

void iCAX::Database::CRepository::RemoveObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_)
{
    m_Observers.remove_if([&](const std::weak_ptr<IRepositoryEventListener>& weakObserver)
        {
            return weakObserver.expired() || weakObserver.lock() == Observer_;
        });
}

void iCAX::Database::CRepository::TriggerRepositoryChanging(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<const CChangeSet> pChangeSet_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsChangeScopeActive() && nType_ != RepositoryEventArgs::kBatchChanged)
    {
        return;
    }

    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnRepositoryChanging(this, { nType_, GetID(), EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this(), pChangeSet_ });
            ++_Ite;
        }
        else
        {
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

void iCAX::Database::CRepository::TriggerRepositoryChanged(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<const CChangeSet> pChangeSet_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsChangeScopeActive() && nType_ != RepositoryEventArgs::kBatchChanged)
    {
        RecordRepositoryChanged({ nType_, GetID(), EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this(), pChangeSet_ });
        return;
    }

    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnRepositoryChanged(this, { nType_, GetID(), EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this(), pChangeSet_ });
            ++_Ite;
        }
        else
        {
            _Ite = m_Observers.erase(_Ite);
        }
    }

    if (nType_ != RepositoryEventArgs::kBatchChanged)
    {
        auto _ChangeSet = BuildChangeSetFromRepositoryEvent({ nType_, GetID(), EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this(), pChangeSet_ });
        HandleCommittedChangeSet(_ChangeSet);
    }
}

bool iCAX::Database::CRepository::IsChangeScopeActive() const
{
    return m_pChangeSetBuilder != nullptr;
}

bool iCAX::Database::CRepository::IsRepositoryEventSuppressed() const
{
    return m_nRepositoryEventSuppressionDepth > 0;
}

void iCAX::Database::CRepository::EndChangeScope(IN const bool bCommit_)
{
    if (!IsChangeScopeActive())
    {
        return;
    }

    auto _pBuilder = std::move(m_pChangeSetBuilder);
    auto _nKind = m_nChangeScopeKind;
    m_nChangeScopeKind = EChangeScopeKind::UserCommand;
    m_strChangeScopeName.clear();

    auto _pChangeSet = std::make_shared<CChangeSet>(_pBuilder->Build());

    if (!bCommit_)
    {
        if (!_pChangeSet->IsEmpty())
        {
            RollbackChangeSetSilently(*_pChangeSet);
        }
        return;
    }

    if (_nKind == EChangeScopeKind::LoadBaseline)
    {
        m_pVerisonTable->Clear();
        m_pDerivedPropertyManager->Clear();
        m_pHistory->Clear();
        return;
    }

    if (_pChangeSet->IsEmpty())
    {
        return;
    }

    ApplyChangeSetEffects(*_pChangeSet);
    TriggerRepositoryChanging(RepositoryEventArgs::kBatchChanged, {}, {}, {}, {}, {}, {}, _pChangeSet);
    TriggerRepositoryChanged(RepositoryEventArgs::kBatchChanged, {}, {}, {}, {}, {}, {}, _pChangeSet);
    HandleCommittedChangeSet(*_pChangeSet);
}

void iCAX::Database::CRepository::RecordRepositoryChanged(IN const RepositoryEventArgs& Args_)
{
    if (!m_pChangeSetBuilder)
    {
        return;
    }

    switch (Args_.nType)
    {
    case RepositoryEventArgs::kAddEntity:
        m_pChangeSetBuilder->RecordAddEntity({ Args_.EntityID });
        break;
    case RepositoryEventArgs::kDeleteEntity:
        m_pChangeSetBuilder->RecordDeleteEntity({ Args_.EntityID });
        break;
    case RepositoryEventArgs::kAddComponent:
        m_pChangeSetBuilder->RecordAddComponent({ Args_.EntityID, Args_.strClassName }, Args_.NewProperties);
        break;
    case RepositoryEventArgs::kRemoveComponent:
        m_pChangeSetBuilder->RecordRemoveComponent({ Args_.EntityID, Args_.strClassName }, Args_.PreviousProperties);
        break;
    case RepositoryEventArgs::kModifyComponent:
        m_pChangeSetBuilder->RecordModifyComponent({ Args_.EntityID, Args_.strClassName }, Args_.PreviousProperties, Args_.NewProperties);
        break;
    default:
        break;
    }
}

iCAX::Database::CChangeSet iCAX::Database::CRepository::BuildChangeSetFromRepositoryEvent(IN const RepositoryEventArgs& Args_) const
{
    CChangeSetBuilder _Builder(EChangeScopeKind::UserCommand, {});

    switch (Args_.nType)
    {
    case RepositoryEventArgs::kAddEntity:
        _Builder.RecordAddEntity({ Args_.EntityID });
        break;
    case RepositoryEventArgs::kDeleteEntity:
        _Builder.RecordDeleteEntity({ Args_.EntityID });
        break;
    case RepositoryEventArgs::kAddComponent:
        _Builder.RecordAddComponent({ Args_.EntityID, Args_.strClassName }, Args_.NewProperties);
        break;
    case RepositoryEventArgs::kRemoveComponent:
        _Builder.RecordRemoveComponent({ Args_.EntityID, Args_.strClassName }, Args_.PreviousProperties);
        break;
    case RepositoryEventArgs::kModifyComponent:
        _Builder.RecordModifyComponent({ Args_.EntityID, Args_.strClassName }, Args_.PreviousProperties, Args_.NewProperties);
        break;
    default:
        break;
    }

    return _Builder.Build();
}

void iCAX::Database::CRepository::ApplyChangeSetEffects(IN const CChangeSet& ChangeSet_)
{
    std::set<ComponentVersionKey> _VersionResets;
    std::set<ComponentVersionKey> _VersionBumps;
    std::set<ComponentVersionKey> _VersionRemoves;
    std::set<PropertyInvalidationKey> _Invalidations;

    for (const auto& _Change : ChangeSet_.AddedComponents)
    {
        const auto& _Key = _Change.Key;
        _VersionResets.emplace(_Key.EntityID, _Key.ComponentClass);
        _VersionBumps.erase({ _Key.EntityID, _Key.ComponentClass });

        for (const auto& _strPropertyName : GetInvalidationPropertyNames(*m_pMetaRegistry, _Key.ComponentClass, _Change.NewProperties))
        {
            _Invalidations.emplace(_Key.EntityID, _Key.ComponentClass, _strPropertyName);
        }
    }

    for (const auto& _Change : ChangeSet_.ModifiedProperties)
    {
        const auto& _Key = _Change.Key;
        if (ShouldAffectVersionAndDerived(*m_pMetaRegistry, _Key.ComponentClass, _Key.PropertyName))
        {
            _Invalidations.emplace(_Key.EntityID, _Key.ComponentClass, _Key.PropertyName);
            if (!_VersionResets.contains({ _Key.EntityID, _Key.ComponentClass }))
            {
                _VersionBumps.emplace(_Key.EntityID, _Key.ComponentClass);
            }
        }
    }

    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        const auto& _Key = _Change.Key;
        for (const auto& [_strPropertyName, _] : _Change.PreviousProperties)
        {
            _Invalidations.emplace(_Key.EntityID, _Key.ComponentClass, _strPropertyName);
        }

        _VersionResets.erase({ _Key.EntityID, _Key.ComponentClass });
        _VersionBumps.erase({ _Key.EntityID, _Key.ComponentClass });
        _VersionRemoves.emplace(_Key.EntityID, _Key.ComponentClass);
    }

    for (const auto& [_EntityID, _ComponentClass, _PropertyName] : _Invalidations)
    {
        m_pDerivedPropertyManager->Invalidate({ _EntityID, _ComponentClass, _PropertyName });
    }

    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        const auto& _Key = _Change.Key;
        m_pDerivedPropertyManager->RemoveComponent(_Key.EntityID, _Key.ComponentClass);
    }

    for (const auto& [_EntityID, _ComponentClass] : _VersionRemoves)
    {
        m_pVerisonTable->Remove({ _EntityID, _ComponentClass });
    }
    for (const auto& [_EntityID, _ComponentClass] : _VersionResets)
    {
        m_pVerisonTable->Reset({ _EntityID, _ComponentClass });
    }
    for (const auto& [_EntityID, _ComponentClass] : _VersionBumps)
    {
        m_pVerisonTable->BumpVersion({ _EntityID, _ComponentClass });
    }
}

void iCAX::Database::CRepository::ApplyChangeSetForward(IN const CChangeSet& ChangeSet_)
{
    for (const auto& _Change : ChangeSet_.CreatedEntities)
    {
        if (!HasEntity(_Change.Key.EntityID))
        {
            CreateEntity(_Change.Key.EntityID);
        }
    }

    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        auto _pEntity = GetEntity(_Change.Key.EntityID);
        if (_pEntity && _pEntity->HasComponent(_Change.Key.ComponentClass))
        {
            _pEntity->RemoveComponent(_Change.Key.ComponentClass);
        }
    }

    for (const auto& _Change : ChangeSet_.AddedComponents)
    {
        auto _pEntity = GetEntity(_Change.Key.EntityID);
        if (!_pEntity)
        {
            continue;
        }

        auto _pComponent = _pEntity->HasComponent(_Change.Key.ComponentClass)
            ? _pEntity->GetComponent(_Change.Key.ComponentClass)
            : _pEntity->AddComponent(_Change.Key.ComponentClass);
        if (_pComponent && !_Change.NewProperties.empty())
        {
            _pComponent->SetProperties(_Change.NewProperties);
        }
    }

    std::map<CChangeComponentKey, PropertySet> _ModifiedProperties;
    for (const auto& _Change : ChangeSet_.ModifiedProperties)
    {
        _ModifiedProperties[{ _Change.Key.EntityID, _Change.Key.ComponentClass }][_Change.Key.PropertyName] = _Change.NewValue;
    }

    for (const auto& [_Key, _Properties] : _ModifiedProperties)
    {
        auto _pEntity = GetEntity(_Key.EntityID);
        auto _pComponent = _pEntity ? _pEntity->GetComponent(_Key.ComponentClass) : nullptr;
        if (_pComponent)
        {
            _pComponent->SetProperties(_Properties);
        }
    }

    for (const auto& _Change : ChangeSet_.DeletedEntities)
    {
        if (HasEntity(_Change.Key.EntityID))
        {
            DeleteEntity(_Change.Key.EntityID);
        }
    }
}

void iCAX::Database::CRepository::ApplyChangeSetBackward(IN const CChangeSet& ChangeSet_)
{
    for (const auto& _Change : ChangeSet_.DeletedEntities)
    {
        if (!HasEntity(_Change.Key.EntityID))
        {
            CreateEntity(_Change.Key.EntityID);
        }
    }

    for (const auto& _Change : ChangeSet_.AddedComponents)
    {
        auto _pEntity = GetEntity(_Change.Key.EntityID);
        if (_pEntity && _pEntity->HasComponent(_Change.Key.ComponentClass))
        {
            _pEntity->RemoveComponent(_Change.Key.ComponentClass);
        }
    }

    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        auto _pEntity = GetEntity(_Change.Key.EntityID);
        if (!_pEntity)
        {
            continue;
        }

        auto _pComponent = _pEntity->HasComponent(_Change.Key.ComponentClass)
            ? _pEntity->GetComponent(_Change.Key.ComponentClass)
            : _pEntity->AddComponent(_Change.Key.ComponentClass);
        if (_pComponent && !_Change.PreviousProperties.empty())
        {
            _pComponent->SetProperties(_Change.PreviousProperties);
        }
    }

    std::map<CChangeComponentKey, PropertySet> _ModifiedProperties;
    for (const auto& _Change : ChangeSet_.ModifiedProperties)
    {
        _ModifiedProperties[{ _Change.Key.EntityID, _Change.Key.ComponentClass }][_Change.Key.PropertyName] = _Change.PreviousValue;
    }

    for (const auto& [_Key, _Properties] : _ModifiedProperties)
    {
        auto _pEntity = GetEntity(_Key.EntityID);
        auto _pComponent = _pEntity ? _pEntity->GetComponent(_Key.ComponentClass) : nullptr;
        if (_pComponent)
        {
            _pComponent->SetProperties(_Properties);
        }
    }

    for (const auto& _Change : ChangeSet_.CreatedEntities)
    {
        if (HasEntity(_Change.Key.EntityID))
        {
            DeleteEntity(_Change.Key.EntityID);
        }
    }
}

void iCAX::Database::CRepository::ApplyChangeSetWithReplayEvent(IN const CChangeSet& ChangeSet_)
{
    if (ChangeSet_.IsEmpty())
    {
        return;
    }

    auto _Scope = BeginChangeScopeCore(EChangeScopeKind::Replay, ChangeSet_.Name, true);
    ApplyChangeSetForward(ChangeSet_);
    _Scope->Commit();
}

void iCAX::Database::CRepository::RollbackChangeSetSilently(IN const CChangeSet& ChangeSet_)
{
    CRepositoryEventSuppressor _Suppressor(*this);
    ApplyChangeSetBackward(ChangeSet_);
}

void iCAX::Database::CRepository::HandleCommittedChangeSet(IN const CChangeSet& ChangeSet_)
{
    if (ChangeSet_.IsEmpty()
        || ChangeSet_.Kind == EChangeScopeKind::LoadBaseline
        || ChangeSet_.Kind == EChangeScopeKind::Replay
        || m_nHistoryReplayDepth > 0)
    {
        return;
    }

    m_pHistory->HandleCommittedChangeSet(ChangeSet_);
    AppendOperationLog(ChangeSet_);
}

void iCAX::Database::CRepository::AppendOperationLog(IN const CChangeSet& ChangeSet_)
{
    if (!m_pOperationLog || !m_pOperationLog->IsOpen())
    {
        return;
    }

    m_pOperationLog->Append(FilterPersistentChangeSet(ChangeSet_, *m_pMetaRegistry));
}
