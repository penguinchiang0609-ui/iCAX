#include "pch.h"
#include "Repository.h"
#include "Domain.h"
#include "MetaRegistry.h"
#include "ChangeLog.h"
#include "RepositoryHistory.h"

#include <map>
#include <set>
#include <tuple>

namespace
{
    bool ShouldAffectVersionAndDerived(IN const std::string& ComponentClass_, IN const std::string& PropertyName_)
    {
        auto _pMeta = iCAX::Database::GetGlobalMetaRegistry();
        if (!_pMeta->HasPropertyByName(ComponentClass_, PropertyName_))
        {
            return !_pMeta->HasTypeByName(ComponentClass_);
        }

        return _pMeta->GetPropertyKindByName(ComponentClass_, PropertyName_) == iCAX::Database::EPropertyKind::Value
            && _pMeta->GetPropertyChangePolicyByName(ComponentClass_, PropertyName_) != iCAX::Database::EPropertyChangePolicy::Silent;
    }

    std::vector<std::string> GetInvalidationPropertyNames(IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_)
    {
        if (!pComponent_)
        {
            return {};
        }

        auto _pMeta = iCAX::Database::GetGlobalMetaRegistry();
        const auto _strComponentClass = pComponent_->GetComponentClass();
        if (_pMeta->HasTypeByName(_strComponentClass))
        {
            return _pMeta->GetPropertyNames(_strComponentClass);
        }

        return pComponent_->GetPropertyNameArray();
    }

    std::vector<std::string> GetInvalidationPropertyNames(IN const std::string& strComponentClass_, IN const iCAX::Data::PropertySet& Properties_)
    {
        auto _pMeta = iCAX::Database::GetGlobalMetaRegistry();
        if (_pMeta->HasTypeByName(strComponentClass_))
        {
            return _pMeta->GetPropertyNames(strComponentClass_);
        }

        std::vector<std::string> _vecNames;
        for (const auto& [_strName, _] : Properties_)
        {
            _vecNames.push_back(_strName);
        }
        return _vecNames;
    }

    using ComponentVersionKey = std::tuple<iCAX::Data::uuid, iCAX::Data::uuid, std::string>;
    using PropertyInvalidationKey = std::tuple<iCAX::Data::uuid, iCAX::Data::uuid, std::string, std::string>;

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

//!< 构造函数
iCAX::Database::CRepository::CRepository(IN const iCAX::Data::uuid& UID_)
{
    m_UID = UID_;
    m_pHistory = std::make_unique<CRepositoryHistory>();
    m_pVerisonTable = std::make_shared<VersionTable>();
    m_pDerivedPropertyManager = std::make_shared<CDerivedPropertyManager>(*this);
}

//!< 析构函数
iCAX::Database::CRepository::~CRepository()
{
}

//! 获取ID
const iCAX::Data::uuid& iCAX::Database::CRepository::GetID() const
{
    return m_UID;
}

std::unique_ptr<iCAX::Database::IRepositoryChangeScope> iCAX::Database::CRepository::BeginChangeScope(IN EChangeScopeKind Kind_, IN const std::string& strName_)
{
    return BeginChangeScopeCore(Kind_, strName_, true);
}

std::unique_ptr<iCAX::Database::IRepositoryChangeScope> iCAX::Database::CRepository::BeginTransaction(IN const std::string& strName_)
{
    return BeginChangeScopeCore(EChangeScopeKind::Transaction, strName_, false);
}

std::unique_ptr<iCAX::Database::IRepositoryUndoScope> iCAX::Database::CRepository::BeginUndoCommand(IN const iCAX::Data::uuid& DomainID_, IN const std::string& strName_)
{
    if (strName_.empty())
    {
        throw std::invalid_argument("Undo command name cannot be empty");
    }
    if (IsChangeScopeActive())
    {
        throw std::runtime_error("Undo scope cannot begin inside an active repository change scope");
    }
    if (!HasDomain(DomainID_))
    {
        throw std::invalid_argument("Undo command domain does not exist");
    }

    return m_pHistory->BeginCommand(DomainID_, strName_);
}

bool iCAX::Database::CRepository::IsUndoCommandRecording() const
{
    return m_pHistory->IsRecording();
}

iCAX::Data::uuid iCAX::Database::CRepository::GetCurrentUndoCommandDomain() const
{
    return m_pHistory->GetCurrentCommandDomain();
}

bool iCAX::Database::CRepository::CanUndo(IN const iCAX::Data::uuid& DomainID_) const
{
    if (IsChangeScopeActive() || IsUndoCommandRecording() || m_nHistoryReplayDepth > 0)
    {
        return false;
    }
    return m_pHistory->CanUndo(DomainID_);
}

bool iCAX::Database::CRepository::CanRedo(IN const iCAX::Data::uuid& DomainID_) const
{
    if (IsChangeScopeActive() || IsUndoCommandRecording() || m_nHistoryReplayDepth > 0)
    {
        return false;
    }
    return m_pHistory->CanRedo(DomainID_);
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepository::GetUndoArray(IN const iCAX::Data::uuid& DomainID_) const
{
    return m_pHistory->GetUndoArray(DomainID_);
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepository::GetRedoArray(IN const iCAX::Data::uuid& DomainID_) const
{
    return m_pHistory->GetRedoArray(DomainID_);
}

bool iCAX::Database::CRepository::Undo(IN const iCAX::Data::uuid& DomainID_)
{
    if (!CanUndo(DomainID_))
    {
        return false;
    }

    auto _StepName = m_pHistory->GetUndoStepName(DomainID_);
    auto _Inverse = MakeInverseChangeSet(m_pHistory->GetUndoChangeSet(DomainID_), _StepName.empty() ? "Undo" : "Undo " + _StepName);

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

    m_pHistory->MoveUndoToRedo(DomainID_);
    AppendOperationLog(_Inverse);
    return true;
}

bool iCAX::Database::CRepository::Redo(IN const iCAX::Data::uuid& DomainID_)
{
    if (!CanRedo(DomainID_))
    {
        return false;
    }

    auto _ChangeSet = m_pHistory->GetRedoChangeSet(DomainID_);

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

    m_pHistory->MoveRedoToUndo(DomainID_);
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

//! 初始化
void iCAX::Database::CRepository::Initialzie()
{
    auto _pDomain = std::make_shared<CDomain>(shared_from_this(), m_UID, true);
    _pDomain->Initialize();
    _pDomain->AddObserver(shared_from_this());
    m_mapDomains[m_UID] = _pDomain;
}

std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CRepository::CreateEntity(IN const iCAX::Data::uuid& ID_)
{
    auto _pDomain = GetDomain(m_UID);
    return _pDomain ? _pDomain->CreateEntity(ID_) : nullptr;
}

bool iCAX::Database::CRepository::HasEntity(IN const iCAX::Data::uuid& ID_) const
{
    auto _Ite = m_mapDomains.find(m_UID);
    return _Ite != m_mapDomains.end() && _Ite->second->HasEntity(ID_);
}

void iCAX::Database::CRepository::DeleteEntity(IN const iCAX::Data::uuid& ID_)
{
    if (auto _pDomain = GetDomain(m_UID))
    {
        _pDomain->DeleteEntity(ID_);
    }
}

std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CRepository::GetEntity(IN const iCAX::Data::uuid& ID_) const
{
    auto _Ite = m_mapDomains.find(m_UID);
    return _Ite != m_mapDomains.end() ? _Ite->second->GetEntity(ID_) : nullptr;
}

std::vector<std::shared_ptr<iCAX::Database::IEntity>> iCAX::Database::CRepository::FilterEntities(IN std::function<bool(std::shared_ptr<IEntity>)> funcWhere_) const
{
    auto _Ite = m_mapDomains.find(m_UID);
    return _Ite != m_mapDomains.end() ? _Ite->second->FilterEntities(funcWhere_) : std::vector<std::shared_ptr<IEntity>>{};
}

int iCAX::Database::CRepository::EntityCount() const
{
    auto _Ite = m_mapDomains.find(m_UID);
    return _Ite != m_mapDomains.end() ? _Ite->second->EntityCount() : 0;
}

std::vector<iCAX::Data::uuid> iCAX::Database::CRepository::GetEntityIDs() const
{
    auto _Ite = m_mapDomains.find(m_UID);
    return _Ite != m_mapDomains.end() ? _Ite->second->GetEntityIDs() : std::vector<iCAX::Data::uuid>{};
}

iCAX::Database::IEntitiesView& iCAX::Database::CRepository::GetView() const
{
    auto _Ite = m_mapDomains.find(m_UID);
    if (_Ite == m_mapDomains.end())
    {
        throw std::runtime_error("Repository data space is missing");
    }
    return _Ite->second->GetView();
}

//!< 创造域
std::shared_ptr<iCAX::Database::IDomain> iCAX::Database::CRepository::CreateDomain(IN const iCAX::Data::uuid& ID_, IN const bool& bPersistent_)
{
    if (ID_ == m_UID)
    {
        return GetDomain(m_UID);
    }
    auto _Ite = m_mapDomains.find(ID_);
    if (_Ite != m_mapDomains.end())
    {
        throw std::runtime_error("Domain already exists: " + to_string(ID_));
    }
    auto _pDomain = std::make_shared<CDomain>(shared_from_this(), ID_, bPersistent_);
    _pDomain->Initialize();
    
    TriggerRepositoryChanging(RepositoryEventArgs::kAddDomain, ID_, {}, {}, {}, {}, {}, {}, _pDomain);
    m_mapDomains[ID_] = _pDomain;
    _pDomain->AddObserver(shared_from_this());
    TriggerRepositoryChanged(RepositoryEventArgs::kAddDomain, ID_, {}, {}, {}, {}, {}, {}, _pDomain);

    return _pDomain;
}

//!< 是否包含域
bool iCAX::Database::CRepository::HasDomain(IN const iCAX::Data::uuid& ID_) const
{
    return m_mapDomains.find(ID_) != m_mapDomains.end();
}

//!< 获取域
std::shared_ptr<iCAX::Database::IDomain> iCAX::Database::CRepository::GetDomain(IN const iCAX::Data::uuid& ID_)
{
    auto _Ite = m_mapDomains.find(ID_);
    if (_Ite != m_mapDomains.end())
    {
        return _Ite->second;
    }

    return nullptr;
}


//!< 移除域
void iCAX::Database::CRepository::DeleteDomain(IN const iCAX::Data::uuid& ID_)
{
    if (ID_ == m_UID)
    {
        return;//!< 元域不得删除
    }
    auto _Ite = m_mapDomains.find(ID_);
    if (_Ite == m_mapDomains.end())
    {
        return;
    }
    _Ite->second->CleanUp();
    _Ite->second->RemoveObserver(shared_from_this());
    auto _pDomain = _Ite->second;
    TriggerRepositoryChanging(RepositoryEventArgs::kDeleteDomain, ID_, {}, {}, {}, {}, {}, {}, _pDomain);
    m_mapDomains.erase(_Ite);
    TriggerRepositoryChanged(RepositoryEventArgs::kDeleteDomain, ID_, {}, {}, {}, {}, {}, {}, _pDomain);
    m_pDerivedPropertyManager->Clear();
}

//!< 域数量
int iCAX::Database::CRepository::DomainCount() const
{
    return (int)m_mapDomains.size();
}

//!< 获取域ID列表
std::vector<iCAX::Data::uuid> iCAX::Database::CRepository::GetDomainIDs() const
{
    std::vector<iCAX::Data::uuid> _vecTemp;
    for (auto& [_ID, _] : m_mapDomains)
    {
        _vecTemp.push_back(_ID);
    }
    return _vecTemp;
}

//!< 获取MetaEntity
std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CRepository::GetMetaEntity()
{
    return GetDomain(m_UID)->GetMetaEntity();
}

//! 清空
void iCAX::Database::CRepository::CleanUp(IN const bool& bForced_/* = false*/)
{
    auto _vecIDs = GetDomainIDs();
    for (auto _Ite = _vecIDs.begin(); _Ite != _vecIDs.end(); _Ite++)
    {
        if (*_Ite == m_UID)
        {
            _vecIDs.erase(_Ite);
            break;
        }
    }

    for (auto& _ID : _vecIDs)
    {
        DeleteDomain(_ID);
    }

    if (bForced_)
    {
        //! 最后删除本体实体
        auto _Ite = m_mapDomains.find(m_UID);
        if (_Ite == m_mapDomains.end())
        {
            return;
        }
        _Ite->second->CleanUp();
        _Ite->second->RemoveObserver(shared_from_this());
        auto _pDomain = _Ite->second;
        TriggerRepositoryChanging(RepositoryEventArgs::kDeleteDomain, m_UID, {}, {}, {}, {}, {}, {}, _pDomain);
        m_mapDomains.erase(_Ite);
        TriggerRepositoryChanged(RepositoryEventArgs::kDeleteDomain, m_UID, {}, {}, {}, {}, {}, {}, _pDomain);
    }

    if (m_pDerivedPropertyManager)
    {
        m_pDerivedPropertyManager->Clear();
    }
}

//! 是否有效
bool iCAX::Database::CRepository::IsValid()
{
    return HasDomain(m_UID);//! 如果缺少了本体域，则标明已经强制清空了，不再可用
}

//! 获取组件版本号
size_t iCAX::Database::CRepository::GetComponentVersion(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
{
    return m_pVerisonTable->GetVersion({ nDomainID_, nEntityID_, strComponentType_});;
}

//! 组件版本升级
void iCAX::Database::CRepository::BumpComponentVersion(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
{
    m_pVerisonTable->BumpVersion({ nDomainID_, nEntityID_, strComponentType_ });
}

//! 计算派生字段
iCAX::Data::PropertyValue iCAX::Database::CRepository::EvaluateDerivedProperty(IN const CComponentBase& Component_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_)
{
    return m_pDerivedPropertyManager->Evaluate(Component_, strPropertyName_, Evaluator_);
}

//! 是否发生了更改
bool iCAX::Database::CRepository::IsComponentChanged(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
{
    return m_pVerisonTable->IsDirty({ nDomainID_, nEntityID_, strComponentType_ });
}

//! 清空修改标记位
void iCAX::Database::CRepository::ResetComponentChangedFlag()
{
    m_pVerisonTable->ClearDirty();
}

//!< 域修改前事件
void iCAX::Database::CRepository::OnDomainChanging(IN void* pSender_, IN const DomainEventArgs& Args_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsChangeScopeActive())
    {
        return;
    }

    TriggerRepositoryChanging((RepositoryEventArgs::EventType)(Args_.nType), Args_.DomainID, Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity, Args_.pDomain);
}

//!< 域更改后事件
void iCAX::Database::CRepository::OnDomainChanged(IN void* pSender_, IN const DomainEventArgs& Args_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsChangeScopeActive())
    {
        TriggerRepositoryChanged((RepositoryEventArgs::EventType)(Args_.nType), Args_.DomainID, Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity, Args_.pDomain);
        return;
    }

    ApplyDomainChangedEffects(Args_);

    TriggerRepositoryChanged((RepositoryEventArgs::EventType)(Args_.nType), Args_.DomainID, Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity, Args_.pDomain);
}

void iCAX::Database::CRepository::ApplyDomainChangedEffects(IN const DomainEventArgs& Args_)
{
    if (Args_.nType == iCAX::Database::DomainEventArgs::kModifyComponent)
    {
        bool _bNeedBumpVersion = false;
        for (const auto& [_strPropertyName, _] : Args_.NewProperties)
        {
            if (ShouldAffectVersionAndDerived(Args_.strClassName, _strPropertyName))
            {
                _bNeedBumpVersion = true;
                m_pDerivedPropertyManager->Invalidate({ Args_.DomainID, Args_.EntityID, Args_.strClassName, _strPropertyName });
            }
        }

        if (_bNeedBumpVersion)
        {
            m_pVerisonTable->BumpVersion({ Args_.DomainID, Args_.EntityID, Args_.strClassName });
        }
    }
    else if (Args_.nType == iCAX::Database::DomainEventArgs::kAddComponent)
    {
        for (const auto& _strPropertyName : GetInvalidationPropertyNames(Args_.pComponent))
        {
            m_pDerivedPropertyManager->Invalidate({ Args_.DomainID, Args_.EntityID, Args_.strClassName, _strPropertyName });
        }
        m_pVerisonTable->Reset({ Args_.DomainID, Args_.EntityID, Args_.strClassName });
    }
    else if (Args_.nType == iCAX::Database::DomainEventArgs::kRemoveComponent)
    {
        for (const auto& [_strPropertyName, _] : Args_.PreviousProperties)
        {
            m_pDerivedPropertyManager->Invalidate({ Args_.DomainID, Args_.EntityID, Args_.strClassName, _strPropertyName });
        }
        m_pDerivedPropertyManager->RemoveComponent(Args_.DomainID, Args_.EntityID, Args_.strClassName);
        m_pVerisonTable->Remove({ Args_.DomainID, Args_.EntityID, Args_.strClassName });
    }
}

//!< 添加观察者
void iCAX::Database::CRepository::AddObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_)
{
    m_Observers.push_back(Observer_);
}

//!< 移除观察者
void iCAX::Database::CRepository::RemoveObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_)
{
    m_Observers.remove_if([&](const std::weak_ptr<IRepositoryEventListener>& weakObserver)
        {
            return weakObserver.lock() == Observer_;
        });
}

//!< 前触发
void iCAX::Database::CRepository::TriggerRepositoryChanging(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<IDomain> pDomain_, IN std::shared_ptr<const CChangeSet> pChangeSet_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsChangeScopeActive() && nType_ != RepositoryEventArgs::kBatchChanged)
    {
        return;
    }

    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        // 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnRepositoryChanging(this, { nType_, GetID(), DomainID_, EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, pDomain_, shared_from_this(), pChangeSet_ });
            ++_Ite;
        }
        else
        {
            // 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

//!< 后触发
void iCAX::Database::CRepository::TriggerRepositoryChanged(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<IDomain> pDomain_, IN std::shared_ptr<const CChangeSet> pChangeSet_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsChangeScopeActive() && nType_ != RepositoryEventArgs::kBatchChanged)
    {
        RecordRepositoryChanged({ nType_, GetID(), DomainID_, EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, pDomain_, shared_from_this(), pChangeSet_ });
        return;
    }

    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        // 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnRepositoryChanged(this, { nType_, GetID(),  DomainID_, EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, pDomain_, shared_from_this(), pChangeSet_});
            ++_Ite;
        }
        else
        {
            // 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }

    if (nType_ != RepositoryEventArgs::kBatchChanged)
    {
        auto _ChangeSet = BuildChangeSetFromRepositoryEvent({ nType_, GetID(), DomainID_, EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, pDomain_, shared_from_this(), pChangeSet_ });
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
    TriggerRepositoryChanging(RepositoryEventArgs::kBatchChanged, {}, {}, {}, {}, {}, {}, {}, {}, _pChangeSet);
    TriggerRepositoryChanged(RepositoryEventArgs::kBatchChanged, {}, {}, {}, {}, {}, {}, {}, {}, _pChangeSet);
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
    case RepositoryEventArgs::kAddDomain:
        m_pChangeSetBuilder->RecordAddDomain(Args_.DomainID, Args_.pDomain ? Args_.pDomain->IsPersistent() : true);
        break;
    case RepositoryEventArgs::kDeleteDomain:
        m_pChangeSetBuilder->RecordDeleteDomain(Args_.DomainID, Args_.pDomain ? Args_.pDomain->IsPersistent() : true);
        break;
    case RepositoryEventArgs::kAddEntity:
        m_pChangeSetBuilder->RecordAddEntity({ Args_.DomainID, Args_.EntityID });
        break;
    case RepositoryEventArgs::kDeleteEntity:
        m_pChangeSetBuilder->RecordDeleteEntity({ Args_.DomainID, Args_.EntityID });
        break;
    case RepositoryEventArgs::kAddComponent:
        m_pChangeSetBuilder->RecordAddComponent({ Args_.DomainID, Args_.EntityID, Args_.strClassName }, Args_.NewProperties);
        break;
    case RepositoryEventArgs::kRemoveComponent:
        m_pChangeSetBuilder->RecordRemoveComponent({ Args_.DomainID, Args_.EntityID, Args_.strClassName }, Args_.PreviousProperties);
        break;
    case RepositoryEventArgs::kModifyComponent:
        m_pChangeSetBuilder->RecordModifyComponent({ Args_.DomainID, Args_.EntityID, Args_.strClassName }, Args_.PreviousProperties, Args_.NewProperties);
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
    case RepositoryEventArgs::kAddDomain:
        _Builder.RecordAddDomain(Args_.DomainID, Args_.pDomain ? Args_.pDomain->IsPersistent() : true);
        break;
    case RepositoryEventArgs::kDeleteDomain:
        _Builder.RecordDeleteDomain(Args_.DomainID, Args_.pDomain ? Args_.pDomain->IsPersistent() : true);
        break;
    case RepositoryEventArgs::kAddEntity:
        _Builder.RecordAddEntity({ Args_.DomainID, Args_.EntityID });
        break;
    case RepositoryEventArgs::kDeleteEntity:
        _Builder.RecordDeleteEntity({ Args_.DomainID, Args_.EntityID });
        break;
    case RepositoryEventArgs::kAddComponent:
        _Builder.RecordAddComponent({ Args_.DomainID, Args_.EntityID, Args_.strClassName }, Args_.NewProperties);
        break;
    case RepositoryEventArgs::kRemoveComponent:
        _Builder.RecordRemoveComponent({ Args_.DomainID, Args_.EntityID, Args_.strClassName }, Args_.PreviousProperties);
        break;
    case RepositoryEventArgs::kModifyComponent:
        _Builder.RecordModifyComponent({ Args_.DomainID, Args_.EntityID, Args_.strClassName }, Args_.PreviousProperties, Args_.NewProperties);
        break;
    default:
        break;
    }

    return _Builder.Build();
}

void iCAX::Database::CRepository::ApplyChangeSetEffects(IN const CChangeSet& ChangeSet_)
{
    if (!ChangeSet_.DeletedDomains.empty())
    {
        m_pDerivedPropertyManager->Clear();
    }

    std::set<ComponentVersionKey> _VersionResets;
    std::set<ComponentVersionKey> _VersionBumps;
    std::set<ComponentVersionKey> _VersionRemoves;
    std::set<PropertyInvalidationKey> _Invalidations;

    for (const auto& _Change : ChangeSet_.AddedComponents)
    {
        const auto& _Key = _Change.Key;
        _VersionResets.emplace(_Key.DomainID, _Key.EntityID, _Key.ComponentClass);
        _VersionBumps.erase({ _Key.DomainID, _Key.EntityID, _Key.ComponentClass });

        for (const auto& _strPropertyName : GetInvalidationPropertyNames(_Key.ComponentClass, _Change.NewProperties))
        {
            _Invalidations.emplace(_Key.DomainID, _Key.EntityID, _Key.ComponentClass, _strPropertyName);
        }
    }

    for (const auto& _Change : ChangeSet_.ModifiedProperties)
    {
        const auto& _Key = _Change.Key;
        if (ShouldAffectVersionAndDerived(_Key.ComponentClass, _Key.PropertyName))
        {
            _Invalidations.emplace(_Key.DomainID, _Key.EntityID, _Key.ComponentClass, _Key.PropertyName);
            if (!_VersionResets.contains({ _Key.DomainID, _Key.EntityID, _Key.ComponentClass }))
            {
                _VersionBumps.emplace(_Key.DomainID, _Key.EntityID, _Key.ComponentClass);
            }
        }
    }

    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        const auto& _Key = _Change.Key;
        for (const auto& [_strPropertyName, _] : _Change.PreviousProperties)
        {
            _Invalidations.emplace(_Key.DomainID, _Key.EntityID, _Key.ComponentClass, _strPropertyName);
        }

        _VersionResets.erase({ _Key.DomainID, _Key.EntityID, _Key.ComponentClass });
        _VersionBumps.erase({ _Key.DomainID, _Key.EntityID, _Key.ComponentClass });
        _VersionRemoves.emplace(_Key.DomainID, _Key.EntityID, _Key.ComponentClass);
    }

    for (const auto& [_DomainID, _EntityID, _ComponentClass, _PropertyName] : _Invalidations)
    {
        m_pDerivedPropertyManager->Invalidate({ _DomainID, _EntityID, _ComponentClass, _PropertyName });
    }

    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        const auto& _Key = _Change.Key;
        m_pDerivedPropertyManager->RemoveComponent(_Key.DomainID, _Key.EntityID, _Key.ComponentClass);
    }

    for (const auto& [_DomainID, _EntityID, _ComponentClass] : _VersionRemoves)
    {
        m_pVerisonTable->Remove({ _DomainID, _EntityID, _ComponentClass });
    }
    for (const auto& [_DomainID, _EntityID, _ComponentClass] : _VersionResets)
    {
        m_pVerisonTable->Reset({ _DomainID, _EntityID, _ComponentClass });
    }
    for (const auto& [_DomainID, _EntityID, _ComponentClass] : _VersionBumps)
    {
        m_pVerisonTable->BumpVersion({ _DomainID, _EntityID, _ComponentClass });
    }
}

void iCAX::Database::CRepository::ApplyChangeSetForward(IN const CChangeSet& ChangeSet_)
{
    for (const auto& _Change : ChangeSet_.CreatedDomains)
    {
        if (!HasDomain(_Change.DomainID))
        {
            CreateDomain(_Change.DomainID, _Change.bPersistent);
        }
    }

    for (const auto& _Change : ChangeSet_.CreatedEntities)
    {
        auto _pDomain = GetDomain(_Change.Key.DomainID);
        if (_pDomain && !_pDomain->HasEntity(_Change.Key.EntityID))
        {
            _pDomain->CreateEntity(_Change.Key.EntityID);
        }
    }

    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        auto _pDomain = GetDomain(_Change.Key.DomainID);
        auto _pEntity = _pDomain ? _pDomain->GetEntity(_Change.Key.EntityID) : nullptr;
        if (_pEntity && _pEntity->HasComponent(_Change.Key.ComponentClass))
        {
            _pEntity->RemoveComponent(_Change.Key.ComponentClass);
        }
    }

    for (const auto& _Change : ChangeSet_.AddedComponents)
    {
        auto _pDomain = GetDomain(_Change.Key.DomainID);
        auto _pEntity = _pDomain ? _pDomain->GetEntity(_Change.Key.EntityID) : nullptr;
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
        _ModifiedProperties[{ _Change.Key.DomainID, _Change.Key.EntityID, _Change.Key.ComponentClass }][_Change.Key.PropertyName] = _Change.NewValue;
    }

    for (const auto& [_Key, _Properties] : _ModifiedProperties)
    {
        auto _pDomain = GetDomain(_Key.DomainID);
        auto _pEntity = _pDomain ? _pDomain->GetEntity(_Key.EntityID) : nullptr;
        auto _pComponent = _pEntity ? _pEntity->GetComponent(_Key.ComponentClass) : nullptr;
        if (_pComponent)
        {
            _pComponent->SetProperties(_Properties);
        }
    }

    for (const auto& _Change : ChangeSet_.DeletedEntities)
    {
        auto _pDomain = GetDomain(_Change.Key.DomainID);
        if (_pDomain && _pDomain->HasEntity(_Change.Key.EntityID))
        {
            _pDomain->DeleteEntity(_Change.Key.EntityID);
        }
    }

    for (const auto& _Change : ChangeSet_.DeletedDomains)
    {
        if (HasDomain(_Change.DomainID))
        {
            DeleteDomain(_Change.DomainID);
        }
    }
}

void iCAX::Database::CRepository::ApplyChangeSetBackward(IN const CChangeSet& ChangeSet_)
{
    for (const auto& _Change : ChangeSet_.DeletedDomains)
    {
        if (!HasDomain(_Change.DomainID))
        {
            CreateDomain(_Change.DomainID, _Change.bPersistent);
        }
    }

    for (const auto& _Change : ChangeSet_.DeletedEntities)
    {
        auto _pDomain = GetDomain(_Change.Key.DomainID);
        if (_pDomain && !_pDomain->HasEntity(_Change.Key.EntityID))
        {
            _pDomain->CreateEntity(_Change.Key.EntityID);
        }
    }

    for (const auto& _Change : ChangeSet_.AddedComponents)
    {
        auto _pDomain = GetDomain(_Change.Key.DomainID);
        auto _pEntity = _pDomain ? _pDomain->GetEntity(_Change.Key.EntityID) : nullptr;
        if (_pEntity && _pEntity->HasComponent(_Change.Key.ComponentClass))
        {
            _pEntity->RemoveComponent(_Change.Key.ComponentClass);
        }
    }

    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        auto _pDomain = GetDomain(_Change.Key.DomainID);
        auto _pEntity = _pDomain ? _pDomain->GetEntity(_Change.Key.EntityID) : nullptr;
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
        _ModifiedProperties[{ _Change.Key.DomainID, _Change.Key.EntityID, _Change.Key.ComponentClass }][_Change.Key.PropertyName] = _Change.PreviousValue;
    }

    for (const auto& [_Key, _Properties] : _ModifiedProperties)
    {
        auto _pDomain = GetDomain(_Key.DomainID);
        auto _pEntity = _pDomain ? _pDomain->GetEntity(_Key.EntityID) : nullptr;
        auto _pComponent = _pEntity ? _pEntity->GetComponent(_Key.ComponentClass) : nullptr;
        if (_pComponent)
        {
            _pComponent->SetProperties(_Properties);
        }
    }

    for (const auto& _Change : ChangeSet_.CreatedEntities)
    {
        auto _pDomain = GetDomain(_Change.Key.DomainID);
        if (_pDomain && _pDomain->HasEntity(_Change.Key.EntityID))
        {
            _pDomain->DeleteEntity(_Change.Key.EntityID);
        }
    }

    for (const auto& _Change : ChangeSet_.CreatedDomains)
    {
        if (HasDomain(_Change.DomainID))
        {
            DeleteDomain(_Change.DomainID);
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

    auto _PersistentChangeSet = FilterPersistentChangeSet(ChangeSet_, [this](const iCAX::Data::uuid& DomainID_)
        {
            auto _pDomain = GetDomain(DomainID_);
            return _pDomain ? _pDomain->IsPersistent() : true;
        });

    m_pOperationLog->Append(_PersistentChangeSet);
}
