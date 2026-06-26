#include "pch.h"
#include "Repository.h"
#include "MetaRegistry.h"
#include "RepositoryUndoRedoHistory.h"

#include <map>
#include <format>
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
        if (!Meta_.HasTypeByName(ComponentClass_))
        {
            throw std::runtime_error("Component type is not registered: " + ComponentClass_);
        }
        if (!Meta_.HasPropertyByName(ComponentClass_, PropertyName_))
        {
            throw std::runtime_error("Component property is not registered: " + ComponentClass_ + "." + PropertyName_);
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
        if (!Meta_.HasTypeByName(_strComponentClass))
        {
            throw std::runtime_error("Component type is not registered: " + _strComponentClass);
        }

        return Meta_.GetPropertyNames(_strComponentClass);
    }

    std::vector<std::string> GetInvalidationPropertyNames(
        IN iCAX::Database::IMetaRegistry& Meta_,
        IN const std::string& strComponentClass_,
        IN const iCAX::Data::PropertySet& Properties_)
    {
        if (!Meta_.HasTypeByName(strComponentClass_))
        {
            throw std::runtime_error("Component type is not registered: " + strComponentClass_);
        }

        return Meta_.GetPropertyNames(strComponentClass_);
    }

    using ComponentVersionKey = std::tuple<iCAX::Data::uuid, std::string>;
    using PropertyInvalidationKey = std::tuple<iCAX::Data::uuid, std::string, std::string>;

    void RequireRepositoryWrite(IN const bool bSucceeded_, IN const std::string& strError_, IN const std::string& strDefaultError_)
    {
        if (!bSucceeded_)
        {
            throw std::runtime_error(strError_.empty() ? strDefaultError_ : strError_);
        }
    }

    bool IsOperationBatchEventType(IN const iCAX::Database::RepositoryEventArgs::EventType Type_)
    {
        switch (Type_)
        {
        case iCAX::Database::RepositoryEventArgs::kModifyComponent:
        case iCAX::Database::RepositoryEventArgs::kAddComponent:
        case iCAX::Database::RepositoryEventArgs::kRemoveComponent:
        case iCAX::Database::RepositoryEventArgs::kAddEntity:
        case iCAX::Database::RepositoryEventArgs::kDeleteEntity:
        case iCAX::Database::RepositoryEventArgs::kEnableComponent:
        case iCAX::Database::RepositoryEventArgs::kDisableComponent:
            return true;
        case iCAX::Database::RepositoryEventArgs::kBatchChanged:
            return false;
        }

        return false;
    }

    bool HasNotificationOnlyEvents(IN const std::vector<iCAX::Database::RepositoryEventRecord>& Records_)
    {
        for (const auto& _Record : Records_)
        {
            if (!IsOperationBatchEventType(_Record.nType))
            {
                return true;
            }
        }
        return false;
    }

    void ApplyComponentEnabledState(IN iCAX::Database::CComponentBase& Component_, IN const bool bEnabled_)
    {
        if (bEnabled_)
        {
            Component_.Enable();
        }
        else
        {
            Component_.Disable();
        }
    }

    std::shared_ptr<iCAX::Database::IMetaRegistry> RequireMetaRegistry(IN std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry_)
    {
        if (!pMetaRegistry_)
        {
            throw std::invalid_argument("Repository MetaRegistry cannot be null");
        }
        return pMetaRegistry_;
    }
}

namespace iCAX
{
    namespace Database
    {
        class CRepositoryTransaction final : public ITransaction
        {
        public:
            explicit CRepositoryTransaction(IN const std::string& strName_)
                : m_strName(strName_)
            {
            }

            ~CRepositoryTransaction() override = default;

            void CreateEntity(IN const iCAX::Data::uuid& EntityID_) override
            {
                CRepositoryOperation _Operation;
                _Operation.Type = RepositoryEventArgs::kAddEntity;
                _Operation.EntityID = EntityID_;
                m_Operations.push_back(std::move(_Operation));
            }

            void DisposeEntity(IN const iCAX::Data::uuid& EntityID_) override
            {
                CRepositoryOperation _Operation;
                _Operation.Type = RepositoryEventArgs::kDeleteEntity;
                _Operation.EntityID = EntityID_;
                m_Operations.push_back(std::move(_Operation));
            }

            void AttachComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_) override
            {
                CRepositoryOperation _Operation;
                _Operation.Type = RepositoryEventArgs::kAddComponent;
                _Operation.EntityID = EntityID_;
                _Operation.ComponentClass = strClassName_;
                _Operation.NewProperties = Properties_;
                m_Operations.push_back(std::move(_Operation));
            }

            void DetachComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_) override
            {
                CRepositoryOperation _Operation;
                _Operation.Type = RepositoryEventArgs::kRemoveComponent;
                _Operation.EntityID = EntityID_;
                _Operation.ComponentClass = strClassName_;
                m_Operations.push_back(std::move(_Operation));
            }

            void ModifyComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_) override
            {
                CRepositoryOperation _Operation;
                _Operation.Type = RepositoryEventArgs::kModifyComponent;
                _Operation.EntityID = EntityID_;
                _Operation.ComponentClass = strClassName_;
                _Operation.NewProperties = Properties_;
                m_Operations.push_back(std::move(_Operation));
            }

            void EnableComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_) override
            {
                CRepositoryOperation _Operation;
                _Operation.Type = RepositoryEventArgs::kEnableComponent;
                _Operation.EntityID = EntityID_;
                _Operation.ComponentClass = strClassName_;
                _Operation.PreviousEnabled = false;
                _Operation.NewEnabled = true;
                m_Operations.push_back(std::move(_Operation));
            }

            void DisableComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_) override
            {
                CRepositoryOperation _Operation;
                _Operation.Type = RepositoryEventArgs::kDisableComponent;
                _Operation.EntityID = EntityID_;
                _Operation.ComponentClass = strClassName_;
                _Operation.PreviousEnabled = true;
                _Operation.NewEnabled = false;
                m_Operations.push_back(std::move(_Operation));
            }

            const std::string& GetName() const
            {
                return m_strName;
            }

            const std::vector<CRepositoryOperation>& GetOperations() const
            {
                return m_Operations;
            }

        private:
            std::string m_strName;
            std::vector<CRepositoryOperation> m_Operations;
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

iCAX::Database::CRepository::CRepository(IN const iCAX::Data::uuid& UID_, IN std::shared_ptr<IMetaRegistry> pMetaRegistry_)
    : m_pMetaRegistry(RequireMetaRegistry(std::move(pMetaRegistry_)))
{
    m_UID = UID_;
    m_pHistory = std::make_unique<CRepositoryUndoRedoHistory>(m_pMetaRegistry);
    m_pVerisonTable = std::make_shared<VersionTable>();
    m_pDerivedPropertyManager = std::make_shared<CDerivedPropertyManager>(*this);
    m_OperationBatchEventRecords.clear();
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

void iCAX::Database::CRepository::BeginBatch()
{
    BeginOperationBatchCore(EOperationBatchKind::UserCommand, std::string());
}

void iCAX::Database::CRepository::EndBatch()
{
    if (!IsOperationBatchActive() || m_nOperationBatchKind != EOperationBatchKind::UserCommand)
    {
        throw std::runtime_error("No active repository batch command");
    }
    EndOperationBatch();
}

void iCAX::Database::CRepository::BeginLoadBaseline()
{
    BeginOperationBatchCore(EOperationBatchKind::LoadBaseline, "LoadBaseline");
}

void iCAX::Database::CRepository::EndLoadBaseline()
{
    if (!IsOperationBatchActive() || m_nOperationBatchKind != EOperationBatchKind::LoadBaseline)
    {
        throw std::runtime_error("No active repository load baseline");
    }
    EndOperationBatch();
}

iCAX::Database::ITransaction& iCAX::Database::CRepository::BeginTransaction(IN const std::string& strName_)
{
    if (IsOperationBatchActive())
    {
        throw std::runtime_error("Repository transaction cannot begin inside an active repository batch scope");
    }
    if (m_pCurrentTransaction)
    {
        throw std::runtime_error("Nested repository transactions are not supported");
    }

    m_pCurrentTransaction = std::make_unique<CRepositoryTransaction>(strName_);
    return *m_pCurrentTransaction;
}

bool iCAX::Database::CRepository::CommitTransaction(IN ITransaction& Transaction_, OUT std::string& strError_)
{
    strError_.clear();
    if (!m_pCurrentTransaction)
    {
        throw std::runtime_error("No active repository transaction");
    }
    if (static_cast<ITransaction*>(m_pCurrentTransaction.get()) != &Transaction_)
    {
        throw std::runtime_error("Repository transaction handle does not match the active transaction");
    }

    auto _pTransaction = std::move(m_pCurrentTransaction);
    const auto& _Operations = _pTransaction->GetOperations();
    if (_Operations.empty())
    {
        return true;
    }

    try
    {
        BeginOperationBatchCore(EOperationBatchKind::Transaction, _pTransaction->GetName());
        for (const auto& _Operation : _Operations)
        {
            ApplyTransactionOperation(_Operation);
        }
        EndOperationBatch();
        return true;
    }
    catch (...)
    {
        if (const auto _pException = std::current_exception())
        {
            try
            {
                std::rethrow_exception(_pException);
            }
            catch (const std::exception& _Exception)
            {
                strError_ = _Exception.what();
            }
            catch (...)
            {
                strError_ = "Repository transaction commit threw a non-standard exception";
            }
        }

        try
        {
            if (IsOperationBatchActive())
            {
                CancelOperationBatch();
            }
        }
        catch (...)
        {
        }
        return false;
    }
}

bool iCAX::Database::CRepository::CommitTransaction(IN ITransaction& Transaction_)
{
    std::string _strError;
    return CommitTransaction(Transaction_, _strError);
}

void iCAX::Database::CRepository::CancelTransaction(IN ITransaction& Transaction_)
{
    if (!m_pCurrentTransaction)
    {
        throw std::runtime_error("No active repository transaction");
    }
    if (static_cast<ITransaction*>(m_pCurrentTransaction.get()) != &Transaction_)
    {
        throw std::runtime_error("Repository transaction handle does not match the active transaction");
    }

    m_pCurrentTransaction.reset();
}

std::unique_ptr<iCAX::Database::IRepositoryUndoScope> iCAX::Database::CRepository::BeginUndoCommand(IN const std::string& strName_)
{
    if (strName_.empty())
    {
        throw std::invalid_argument("Undo command name cannot be empty");
    }
    if (IsOperationBatchActive() || m_pCurrentTransaction)
    {
        throw std::runtime_error("Undo scope cannot begin inside an active repository operation batch or transaction");
    }

    return m_pHistory->BeginCommand(strName_);
}

bool iCAX::Database::CRepository::IsUndoCommandRecording() const
{
    return m_pHistory->IsRecording();
}

bool iCAX::Database::CRepository::CanUndo() const
{
    if (IsOperationBatchActive() || m_pCurrentTransaction || IsUndoCommandRecording() || m_nHistoryReplayDepth > 0)
    {
        return false;
    }
    return m_pHistory->CanUndo();
}

bool iCAX::Database::CRepository::CanRedo() const
{
    if (IsOperationBatchActive() || m_pCurrentTransaction || IsUndoCommandRecording() || m_nHistoryReplayDepth > 0)
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
    auto _Inverse = MakeInverseOperationBatch(m_pHistory->GetUndoOperationBatch(), _StepName.empty() ? "Undo" : "Undo " + _StepName);

    ++m_nHistoryReplayDepth;
    try
    {
        ApplyOperationBatchWithReplayEvent(_Inverse);
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

    auto _Batch = m_pHistory->GetRedoOperationBatch();

    ++m_nHistoryReplayDepth;
    try
    {
        ApplyOperationBatchWithReplayEvent(_Batch);
        --m_nHistoryReplayDepth;
    }
    catch (...)
    {
        --m_nHistoryReplayDepth;
        throw;
    }

    m_pHistory->MoveRedoToUndo();
    AppendOperationLog(_Batch);
    return true;
}

void iCAX::Database::CRepository::BeginOperationBatchCore(IN EOperationBatchKind Kind_, IN const std::string& strName_)
{
    if (IsOperationBatchActive())
    {
        throw std::runtime_error("Nested repository operation batches are not supported");
    }
    if (m_pCurrentTransaction)
    {
        throw std::runtime_error("Repository operation batch cannot begin inside an active repository transaction");
    }

    m_nOperationBatchKind = Kind_;
    m_strOperationBatchName = strName_;
    m_OperationBatchEventRecords.clear();
    m_pOperationBatchBuilder = std::make_unique<COperationBatchBuilder>(Kind_, strName_);
}

void iCAX::Database::CRepository::OpenOperationLog(
    IN const std::string& strPath_,
    IN const bool bTruncate_,
    IN const std::string& strMagic_,
    IN const uint32_t nVersion_)
{
    if (!m_pOperationLog)
    {
        m_pOperationLog = std::make_unique<COperationBatchJournal>();
    }
    m_pOperationLog->Open(strPath_, bTruncate_, strMagic_, nVersion_);
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

void iCAX::Database::CRepository::ReplayOperationLog(
    IN const std::string& strPath_,
    IN const std::string& strMagic_,
    IN const uint32_t nVersion_)
{
    if (IsOperationBatchActive())
    {
        throw std::runtime_error("Repository operation log cannot be replayed while an operation batch is active");
    }
    if (m_pCurrentTransaction)
    {
        throw std::runtime_error("Repository operation log cannot be replayed while a transaction is active");
    }
    if (IsUndoCommandRecording())
    {
        throw std::runtime_error("Repository operation log cannot be replayed while an undo command is recording");
    }
    if (m_nHistoryReplayDepth > 0)
    {
        throw std::runtime_error("Repository operation log cannot be replayed while history replay is active");
    }
    if (HasOperationLog())
    {
        throw std::runtime_error("Repository operation log cannot be replayed while an operation log is open for append");
    }

    COperationBatchJournal _Journal;
    const auto _Batches = _Journal.ReadAll(strPath_, strMagic_, nVersion_);

    m_pHistory->Clear();

    ++m_nHistoryReplayDepth;
    try
    {
        for (const auto& _Batch : _Batches)
        {
            ApplyOperationBatchWithReplayEvent(_Batch);
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

bool iCAX::Database::CRepository::CreateEntity(IN const iCAX::Data::uuid& ID_, OUT std::shared_ptr<IEntity>& pEntity_, OUT std::string& strError_)
{
    pEntity_.reset();
    strError_.clear();
    if (ID_ == m_UID)
    {
        pEntity_ = GetEntity(m_UID);
        return pEntity_ != nullptr;
    }
    auto _Ite = m_mapEntities.find(ID_);
    if (_Ite != m_mapEntities.end())
    {
        strError_ = "Entity already exists: " + to_string(ID_);
        return false;
    }

    auto _pEntity = std::make_shared<CEntity>(shared_from_this(), ID_);
    TriggerRepositoryChanging(RepositoryEventArgs::kAddEntity, ID_, {}, {}, {}, {}, _pEntity);
    m_mapEntities[ID_] = _pEntity;
    _pEntity->AddObserver(shared_from_this());
    TriggerRepositoryChanged(RepositoryEventArgs::kAddEntity, ID_, {}, {}, {}, {}, _pEntity);
    pEntity_ = _pEntity;
    return true;
}

bool iCAX::Database::CRepository::HasEntity(IN const iCAX::Data::uuid& ID_) const
{
    return m_mapEntities.find(ID_) != m_mapEntities.end();
}

bool iCAX::Database::CRepository::DeleteEntity(IN const iCAX::Data::uuid& ID_, OUT std::string& strError_)
{
    strError_.clear();
    if (ID_ == m_UID)
    {
        return true;
    }

    auto _Ite = m_mapEntities.find(ID_);
    if (_Ite == m_mapEntities.end())
    {
        return true;
    }

    _Ite->second->Cleanup();
    _Ite->second->RemoveObserver(shared_from_this());
    auto _pEntity = _Ite->second;
    TriggerRepositoryChanging(RepositoryEventArgs::kDeleteEntity, ID_, {}, {}, {}, {}, _pEntity);
    m_mapEntities.erase(_Ite);
    TriggerRepositoryChanged(RepositoryEventArgs::kDeleteEntity, ID_, {}, {}, {}, {}, _pEntity);
    return true;
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
        std::string _strError;
        if (!DeleteEntity(_ID, _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Repository cleanup entity failed" : _strError);
        }
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

uint64_t iCAX::Database::CRepository::GetComponentVersion(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
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

    if (IsOperationBatchActive())
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

    if (IsOperationBatchActive())
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
    else if (Args_.nType == iCAX::Database::EntityEventArgs::kEnableComponent
        || Args_.nType == iCAX::Database::EntityEventArgs::kDisableComponent)
    {
        m_pVerisonTable->BumpVersion({ Args_.EntityID, Args_.strClassName });
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

void iCAX::Database::CRepository::TriggerRepositoryChanging(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<const RepositoryEventBatch> pBatch_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsOperationBatchActive() && nType_ != RepositoryEventArgs::kBatchChanged)
    {
        return;
    }

    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        if (auto _Observer = _Ite->lock())
        {
            try
            {
                _Observer->OnRepositoryChanging(this, { nType_, GetID(), EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this(), pBatch_ });
            }
            catch (...)
            {
                // Repository observer is notification-only. ModifyFilter is the validation path;
                // observer failures must not corrupt repository history/log state.
            }
            ++_Ite;
        }
        else
        {
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

void iCAX::Database::CRepository::TriggerRepositoryChanged(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<const RepositoryEventBatch> pBatch_)
{
    if (IsRepositoryEventSuppressed())
    {
        return;
    }

    if (IsOperationBatchActive() && nType_ != RepositoryEventArgs::kBatchChanged)
    {
        RepositoryEventArgs _Args{ nType_, GetID(), EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this() };
        RecordRepositoryBatchEvent(_Args);
        RecordRepositoryOperation(_Args);
        return;
    }

    if (nType_ != RepositoryEventArgs::kBatchChanged && IsOperationBatchEventType(nType_))
    {
        auto _Batch = MakeOperationBatchFromRepositoryEvent({ nType_, GetID(), EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this() });
        auto _Summary = BuildChangeSetFromOperationBatch(_Batch);
        HandleCommittedOperationBatch(_Batch, _Summary);
    }

    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        if (auto _Observer = _Ite->lock())
        {
            try
            {
                _Observer->OnRepositoryChanged(this, { nType_, GetID(), EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this(), pBatch_ });
            }
            catch (...)
            {
                // External notification failure is intentionally isolated from committed data.
            }
            ++_Ite;
        }
        else
        {
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

bool iCAX::Database::CRepository::IsOperationBatchActive() const
{
    return m_pOperationBatchBuilder != nullptr;
}

bool iCAX::Database::CRepository::IsRepositoryEventSuppressed() const
{
    return m_nRepositoryEventSuppressionDepth > 0;
}

void iCAX::Database::CRepository::EndOperationBatch()
{
    if (!IsOperationBatchActive())
    {
        return;
    }

    auto _pBuilder = std::move(m_pOperationBatchBuilder);
    auto _nKind = m_nOperationBatchKind;
    m_nOperationBatchKind = EOperationBatchKind::UserCommand;
    m_strOperationBatchName.clear();
    auto _EventRecords = std::move(m_OperationBatchEventRecords);
    m_OperationBatchEventRecords.clear();

    // OperationBatch 是事实日志，ChangeSet 是由事实日志派生的净变更摘要。
    // 提交路径必须先冻结 Batch，再派生 Summary，避免撤销/日志丢失真实操作顺序。
    auto _Batch = _pBuilder->Build();
    auto _ChangeSet = BuildChangeSetFromOperationBatch(_Batch);

    if (_nKind == EOperationBatchKind::LoadBaseline)
    {
        // 加载基线用于首次打开项目文件。成功后不应产生用户修改语义、历史记录或快速保存日志。
        m_pVerisonTable->Clear();
        m_pDerivedPropertyManager->Clear();
        m_pHistory->Clear();
        return;
    }

    const bool _bHasNotificationOnlyEvents = HasNotificationOnlyEvents(_EventRecords);
    if (_ChangeSet.IsEmpty() && !_bHasNotificationOnlyEvents)
    {
        // 批次内操作可能互相抵消，例如添加组件后又删除组件。
        // 净摘要为空时对外没有可见变更，不发布批量事件，也不进入历史或日志。
        return;
    }

    if (!_ChangeSet.IsEmpty())
    {
        ApplyChangeSetEffects(_ChangeSet);
        HandleCommittedOperationBatch(_Batch, _ChangeSet);
    }

    auto _pBatch = std::make_shared<RepositoryEventBatch>();
    _pBatch->pOperationBatch = std::make_shared<COperationBatch>(_Batch);
    _pBatch->Records = std::move(_EventRecords);

    TriggerRepositoryChanging(RepositoryEventArgs::kBatchChanged, {}, {}, {}, {}, {}, {}, _pBatch);
    TriggerRepositoryChanged(RepositoryEventArgs::kBatchChanged, {}, {}, {}, {}, {}, {}, _pBatch);
}

void iCAX::Database::CRepository::CancelOperationBatch()
{
    if (!IsOperationBatchActive())
    {
        return;
    }

    auto _pBuilder = std::move(m_pOperationBatchBuilder);
    m_nOperationBatchKind = EOperationBatchKind::UserCommand;
    m_strOperationBatchName.clear();
    m_OperationBatchEventRecords.clear();

    auto _Batch = _pBuilder->Build();
    if (!_Batch.IsEmpty())
    {
        RollbackOperationBatchSilently(_Batch);
    }
}

void iCAX::Database::CRepository::RecordRepositoryOperation(IN const RepositoryEventArgs& Args_)
{
    if (!m_pOperationBatchBuilder)
    {
        return;
    }
    if (!IsOperationBatchEventType(Args_.nType))
    {
        return;
    }

    m_pOperationBatchBuilder->RecordRepositoryEvent(Args_);
}

void iCAX::Database::CRepository::RecordRepositoryBatchEvent(IN const RepositoryEventArgs& Args_)
{
    m_OperationBatchEventRecords.push_back({
        Args_.nType,
        Args_.EntityID,
        Args_.strClassName,
        Args_.PreviousProperties,
        Args_.NewProperties,
        Args_.pComponent,
        Args_.pEntity
    });
}

void iCAX::Database::CRepository::ApplyChangeSetEffects(IN const CChangeSet& ChangeSet_)
{
    // 版本和派生字段失效只关心最终净结果，不需要保留字段修改顺序。
    // 因此这里使用 ChangeSet，而不是 OperationBatch。
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

    for (const auto& _Change : ChangeSet_.ModifiedComponentStates)
    {
        const auto& _Key = _Change.Key;
        if (!_VersionResets.contains({ _Key.EntityID, _Key.ComponentClass }))
        {
            _VersionBumps.emplace(_Key.EntityID, _Key.ComponentClass);
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

void iCAX::Database::CRepository::ApplyTransactionOperation(IN const CRepositoryOperation& Operation_)
{
    switch (Operation_.Type)
    {
    case RepositoryEventArgs::kAddEntity:
        if (HasEntity(Operation_.EntityID))
        {
            throw std::runtime_error("Transaction create entity failed: entity already exists");
        }
        {
            std::shared_ptr<IEntity> _pCreatedEntity;
            std::string _strError;
            RequireRepositoryWrite(CreateEntity(Operation_.EntityID, _pCreatedEntity, _strError), _strError, "Transaction create entity failed");
        }
        break;
    case RepositoryEventArgs::kDeleteEntity:
        if (!HasEntity(Operation_.EntityID))
        {
            throw std::runtime_error("Transaction dispose entity failed: entity does not exist");
        }
        {
            std::string _strError;
            RequireRepositoryWrite(DeleteEntity(Operation_.EntityID, _strError), _strError, "Transaction dispose entity failed");
        }
        break;
    case RepositoryEventArgs::kAddComponent:
    {
        auto _pEntity = GetEntity(Operation_.EntityID);
        if (!_pEntity)
        {
            throw std::runtime_error("Transaction attach component failed: entity does not exist");
        }
        if (_pEntity->HasComponent(Operation_.ComponentClass))
        {
            throw std::runtime_error("Transaction attach component failed: component already exists");
        }

        std::string _strError;
        std::shared_ptr<CComponentBase> _pComponent;
        RequireRepositoryWrite(_pEntity->AddComponent(Operation_.ComponentClass, _pComponent, _strError), _strError, "Transaction attach component failed");
        if (!Operation_.NewProperties.empty())
        {
            RequireRepositoryWrite(_pComponent->SetProperties(Operation_.NewProperties, _strError), _strError, "Transaction initialize component failed");
        }
        if (_pComponent && Operation_.NewEnabled.has_value())
        {
            ApplyComponentEnabledState(*_pComponent, *Operation_.NewEnabled);
        }
        break;
    }
    case RepositoryEventArgs::kRemoveComponent:
    {
        auto _pEntity = GetEntity(Operation_.EntityID);
        if (!_pEntity)
        {
            throw std::runtime_error("Transaction detach component failed: entity does not exist");
        }
        if (!_pEntity->HasComponent(Operation_.ComponentClass))
        {
            throw std::runtime_error("Transaction detach component failed: component does not exist");
        }

        std::string _strError;
        RequireRepositoryWrite(_pEntity->RemoveComponent(Operation_.ComponentClass, _strError), _strError, "Transaction detach component failed");
        break;
    }
    case RepositoryEventArgs::kModifyComponent:
    {
        auto _pEntity = GetEntity(Operation_.EntityID);
        if (!_pEntity)
        {
            throw std::runtime_error("Transaction modify component failed: entity does not exist");
        }

        auto _pComponent = _pEntity->GetComponent(Operation_.ComponentClass);
        if (!_pComponent)
        {
            throw std::runtime_error("Transaction modify component failed: component does not exist");
        }

        std::string _strError;
        RequireRepositoryWrite(_pComponent->SetProperties(Operation_.NewProperties, _strError), _strError, "Transaction modify component failed");
        break;
    }
    case RepositoryEventArgs::kEnableComponent:
    case RepositoryEventArgs::kDisableComponent:
    {
        auto _pEntity = GetEntity(Operation_.EntityID);
        if (!_pEntity)
        {
            throw std::runtime_error("Transaction component state failed: entity does not exist");
        }

        auto _pComponent = _pEntity->GetComponent(Operation_.ComponentClass);
        if (!_pComponent)
        {
            throw std::runtime_error("Transaction component state failed: component does not exist");
        }

        ApplyComponentEnabledState(*_pComponent, Operation_.NewEnabled.value_or(Operation_.Type == RepositoryEventArgs::kEnableComponent));
        break;
    }
    default:
        throw std::runtime_error("Transaction operation type is not supported");
    }
}

void iCAX::Database::CRepository::ApplyOperationForward(IN const CRepositoryOperation& Operation_)
{
    switch (Operation_.Type)
    {
    case RepositoryEventArgs::kAddEntity:
        {
            if (HasEntity(Operation_.EntityID))
            {
                throw std::runtime_error("Replay create entity failed: entity already exists");
            }
            std::shared_ptr<IEntity> _pCreatedEntity;
            std::string _strError;
            RequireRepositoryWrite(CreateEntity(Operation_.EntityID, _pCreatedEntity, _strError), _strError, "Replay create entity failed");
        }
        break;
    case RepositoryEventArgs::kDeleteEntity:
        {
            if (!HasEntity(Operation_.EntityID))
            {
                throw std::runtime_error("Replay delete entity failed: entity does not exist");
            }
            std::string _strError;
            RequireRepositoryWrite(DeleteEntity(Operation_.EntityID, _strError), _strError, "Replay delete entity failed");
        }
        break;
    case RepositoryEventArgs::kAddComponent:
    {
        auto _pEntity = GetEntity(Operation_.EntityID);
        if (!_pEntity)
        {
            throw std::runtime_error("Replay add component failed: entity does not exist");
        }

        if (_pEntity->HasComponent(Operation_.ComponentClass))
        {
            throw std::runtime_error("Replay add component failed: component already exists");
        }

        std::shared_ptr<CComponentBase> _pComponent;
        {
            std::string _strError;
            RequireRepositoryWrite(_pEntity->AddComponent(Operation_.ComponentClass, _pComponent, _strError), _strError, "Replay add component failed");
        }
        if (_pComponent && !Operation_.NewProperties.empty())
        {
            std::string _strError;
            RequireRepositoryWrite(_pComponent->SetProperties(Operation_.NewProperties, _strError), _strError, "Replay initialize component failed");
        }
        if (_pComponent && Operation_.NewEnabled.has_value())
        {
            ApplyComponentEnabledState(*_pComponent, *Operation_.NewEnabled);
        }
        break;
    }
    case RepositoryEventArgs::kRemoveComponent:
    {
        auto _pEntity = GetEntity(Operation_.EntityID);
        if (!_pEntity)
        {
            throw std::runtime_error("Replay remove component failed: entity does not exist");
        }
        if (!_pEntity->HasComponent(Operation_.ComponentClass))
        {
            throw std::runtime_error("Replay remove component failed: component does not exist");
        }
        {
            std::string _strError;
            RequireRepositoryWrite(_pEntity->RemoveComponent(Operation_.ComponentClass, _strError), _strError, "Replay remove component failed");
        }
        break;
    }
    case RepositoryEventArgs::kModifyComponent:
    {
        auto _pEntity = GetEntity(Operation_.EntityID);
        if (!_pEntity)
        {
            throw std::runtime_error("Replay modify component failed: entity does not exist");
        }

        auto _pComponent = _pEntity->GetComponent(Operation_.ComponentClass);
        if (!_pComponent)
        {
            throw std::runtime_error("Replay modify component failed: component does not exist");
        }

        if (!Operation_.NewProperties.empty())
        {
            std::string _strError;
            RequireRepositoryWrite(_pComponent->SetProperties(Operation_.NewProperties, _strError), _strError, "Replay modify component failed");
        }
        break;
    }
    case RepositoryEventArgs::kEnableComponent:
    case RepositoryEventArgs::kDisableComponent:
    {
        auto _pEntity = GetEntity(Operation_.EntityID);
        if (!_pEntity)
        {
            throw std::runtime_error("Replay component state failed: entity does not exist");
        }

        auto _pComponent = _pEntity->GetComponent(Operation_.ComponentClass);
        if (!_pComponent)
        {
            throw std::runtime_error("Replay component state failed: component does not exist");
        }

        ApplyComponentEnabledState(*_pComponent, Operation_.NewEnabled.value_or(Operation_.Type == RepositoryEventArgs::kEnableComponent));
        break;
    }
    default:
        break;
    }
}

void iCAX::Database::CRepository::ApplyOperationBatchForward(IN const COperationBatch& Batch_)
{
    for (const auto& _Operation : Batch_.Operations)
    {
        ApplyOperationForward(_Operation);
    }
}

void iCAX::Database::CRepository::ApplyOperationBatchBackward(IN const COperationBatch& Batch_)
{
    auto _Inverse = MakeInverseOperationBatch(Batch_);
    ApplyOperationBatchForward(_Inverse);
}

void iCAX::Database::CRepository::ApplyOperationBatchWithReplayEvent(IN const COperationBatch& Batch_)
{
    if (Batch_.IsEmpty())
    {
        return;
    }

    // Replay 失败时需要回滚已经应用的部分，避免留下半条回放结果。
    BeginOperationBatchCore(EOperationBatchKind::Replay, Batch_.Name);
    try
    {
        ApplyOperationBatchForward(Batch_);
        EndOperationBatch();
    }
    catch (...)
    {
        CancelOperationBatch();
        throw;
    }
}

void iCAX::Database::CRepository::RollbackOperationBatchSilently(IN const COperationBatch& Batch_)
{
    CRepositoryEventSuppressor _Suppressor(*this);
    ApplyOperationBatchBackward(Batch_);
}

void iCAX::Database::CRepository::HandleCommittedOperationBatch(IN const COperationBatch& Batch_, IN const CChangeSet& Summary_)
{
    if (Batch_.IsEmpty()
        || Batch_.Kind == EOperationBatchKind::LoadBaseline
        || Batch_.Kind == EOperationBatchKind::Replay
        || m_nHistoryReplayDepth > 0)
    {
        // LoadBaseline 是干净基线；Replay/Undo/Redo 不应再次进入历史；
        // HistoryReplayDepth 用于阻断 Undo/Redo 自身产生嵌套历史。
        return;
    }

    m_pHistory->HandleCommittedOperationBatch(Batch_);
    AppendOperationLog(Batch_);
}

void iCAX::Database::CRepository::AppendOperationLog(IN const COperationBatch& Batch_)
{
    if (!m_pOperationLog || !m_pOperationLog->IsOpen())
    {
        return;
    }

    m_pOperationLog->Append(FilterPersistentOperationBatch(Batch_, *m_pMetaRegistry));
}
