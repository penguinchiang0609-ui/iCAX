#include "pch.h"
#include "BehaviourDispatcher.h"
#include "IBehaviourRegistry.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "../Database/ComponentBase.h"

namespace
{
    constexpr size_t kUnboundBehaviourRank = (std::numeric_limits<size_t>::max)();
    constexpr size_t kMaxDestroyFlushNotificationCount = 100000;

    iCAX::Behaviour::CComponentDestroyInfo MakeDestroyInfo(
        IN const iCAX::Database::RepositoryEventArgs& Args_)
    {
        return { Args_.EntityID, Args_.strClassName, Args_.PreviousProperties };
    }

    struct ComponentKey final
    {
        iCAX::Data::uuid EntityID;
        std::string ComponentClass;

        bool operator<(IN const ComponentKey& Other_) const
        {
            if (EntityID < Other_.EntityID)
            {
                return true;
            }
            if (Other_.EntityID < EntityID)
            {
                return false;
            }
            return ComponentClass < Other_.ComponentClass;
        }
    };

    iCAX::Behaviour::CComponentDestroyInfo MakeDestroyInfo(
        IN const ComponentKey& Key_,
        IN const iCAX::Data::PropertySet& PreviousProperties_)
    {
        return { Key_.EntityID, Key_.ComponentClass, PreviousProperties_ };
    }

    ComponentKey MakeComponentKey(
        IN const iCAX::Database::RepositoryEventRecord& Record_)
    {
        return { Record_.EntityID, Record_.strClassName };
    }

    std::shared_ptr<iCAX::Database::CComponentBase> FindCurrentComponent(
        IN iCAX::Project::ISceneContext& SceneContext_,
        IN const ComponentKey& Key_)
    {
        auto _pEntity = SceneContext_.Database().GetEntity(Key_.EntityID);
        if (!_pEntity)
        {
            return nullptr;
        }
        return _pEntity->GetComponent(Key_.ComponentClass);
    }

    void MergeProperties(
        IN OUT iCAX::Data::PropertySet& Target_,
        IN const iCAX::Data::PropertySet& Source_)
    {
        for (const auto& _Pair : Source_)
        {
            Target_[_Pair.first] = _Pair.second;
        }
    }

    struct ComponentBatchState final
    {
        bool HasStructuralEvent = false;
        iCAX::Database::RepositoryEventArgs::EventType FirstStructuralEvent = iCAX::Database::RepositoryEventArgs::kAddComponent;
        iCAX::Database::RepositoryEventArgs::EventType LastStructuralEvent = iCAX::Database::RepositoryEventArgs::kAddComponent;
        size_t LastAddSequence = 0;
        size_t LastRemoveSequence = 0;
        iCAX::Data::PropertySet LastAddProperties;
        iCAX::Data::PropertySet LastRemoveProperties;
        std::shared_ptr<iCAX::Database::CComponentBase> pLastAddComponent;
        std::shared_ptr<iCAX::Database::CComponentBase> pLastRemoveComponent;

        bool HasModifyEvent = false;
        size_t FirstModifySequence = 0;
        iCAX::Data::PropertySet ModifiedProperties;
        std::shared_ptr<iCAX::Database::CComponentBase> pModifiedComponent;

        bool HasEnableStateEvent = false;
        iCAX::Database::RepositoryEventArgs::EventType FirstEnableStateEvent = iCAX::Database::RepositoryEventArgs::kEnableComponent;
        iCAX::Database::RepositoryEventArgs::EventType LastEnableStateEvent = iCAX::Database::RepositoryEventArgs::kEnableComponent;
        size_t LastEnableStateSequence = 0;
        iCAX::Data::PropertySet LastEnableStateProperties;
        std::shared_ptr<iCAX::Database::CComponentBase> pLastEnableStateComponent;
    };
}

//!< 构造函数
iCAX::Behaviour::CBehaviourDispatcher::CBehaviourDispatcher(
    IN std::shared_ptr<IBehaviourRegistry> pRegistry_,
    IN std::shared_ptr<iCAX::Coroutines::CCoroutineRuntime> pCoroutineRuntime_)
    : m_setBehaviourIndex()
    , m_BehavioursMap()
    , m_OrderedList()
    , m_ExecutionList()
    , m_ExecutionRankByComponentClass()
    , m_FrameUpdatePausedBehaviourTypes()
    , m_PendingDestroyNotifications()
    , m_nNextPendingDestroySequence(0)
    , m_pRegistry(std::move(pRegistry_))
    , m_pCoroutineRuntime(std::move(pCoroutineRuntime_))
{
    if (!m_pRegistry)
    {
        throw std::invalid_argument("BehaviourDispatcher registry cannot be null");
    }
    if (!m_pCoroutineRuntime)
    {
        throw std::invalid_argument("BehaviourDispatcher coroutine runtime cannot be null");
    }
}

//!< 注册Behaviour
bool iCAX::Behaviour::CBehaviourDispatcher::BindBehaviour(IN const std::type_index& nType_)
{
    if (m_setBehaviourIndex.contains(nType_))
    {
        return false;
    }

    std::shared_ptr<CBehaviourBase> _pBehaviour = m_pRegistry->CreateBehaviourByType(nType_);
    if (!_pBehaviour)
    {
        throw std::runtime_error("Behaviour is not registered");
    }
    const auto _strComponentClass = _pBehaviour->GetComponentClass();
    if (_strComponentClass.empty())
    {
        throw std::runtime_error("Behaviour component class cannot be empty");
    }
    if (m_BehavioursMap.contains(_strComponentClass))
    {
        throw std::runtime_error("Component already has a bound behaviour: " + _strComponentClass);
    }

    _pBehaviour->BindCoroutineRuntime(m_pCoroutineRuntime);

    m_setBehaviourIndex.emplace(nType_);
    m_OrderedList.push_back(_pBehaviour);
    m_BehavioursMap.emplace(_strComponentClass, _pBehaviour);
    try
    {
        RebuildExecutionPlan();
    }
    catch (...)
    {
        _pBehaviour->UnbindCoroutineRuntime();
        m_BehavioursMap.erase(_strComponentClass);
        m_OrderedList.pop_back();
        m_setBehaviourIndex.erase(nType_);
        throw;
    }
    return true;
}

//! 暂停帧更新
void iCAX::Behaviour::CBehaviourDispatcher::PauseFrameUpdate(IN const std::type_index& nType_)
{
    m_FrameUpdatePausedBehaviourTypes.emplace(nType_);
}

//! 是否已暂停帧更新
bool iCAX::Behaviour::CBehaviourDispatcher::IsFrameUpdatePaused(IN const std::type_index& nType_) const
{
    return m_FrameUpdatePausedBehaviourTypes.contains(nType_);
}

//! 恢复指定类型的行为帧更新
void iCAX::Behaviour::CBehaviourDispatcher::ResumeFrameUpdate(IN const std::type_index& nType_)
{
    m_FrameUpdatePausedBehaviourTypes.erase(nType_);
}

//! 获取所有被暂停帧更新的行为
std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CBehaviourDispatcher::GetFrameUpdatePaused() const
{
    std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> _Res;
    for (const auto& _pBehaviour : m_ExecutionList)
    {
        if (_pBehaviour && IsFrameUpdatePaused(std::type_index(typeid(*_pBehaviour))))
        {
            _Res.push_back(_pBehaviour);
        }
    }

    return _Res;
}

//! 获取所有被绑定的行为
std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CBehaviourDispatcher::GetAll() const
{
    std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> _Res;
    for (const auto& _pBehaviour : m_ExecutionList)
    {
        if (_pBehaviour)
        {
            _Res.push_back(_pBehaviour);
        }
    }

    return _Res;
}

//! 是否已包含
bool iCAX::Behaviour::CBehaviourDispatcher::HasBehaviour(IN const std::type_index& nType_) const
{
    return  m_setBehaviourIndex.contains(nType_);
}

//! 注销行为
void iCAX::Behaviour::CBehaviourDispatcher::UnbindBehaviour(IN const std::type_index& nType_)
{
    auto _Ite = std::find_if(
        m_OrderedList.begin(),
        m_OrderedList.end(),
        [&nType_](const std::shared_ptr<CBehaviourBase>& pBehaviour_)
        {
            return pBehaviour_ && std::type_index(typeid(*pBehaviour_)) == nType_;
        });
    if (_Ite == m_OrderedList.end())
    {
        return;
    }

    auto _pBehaviour = *_Ite;
    _pBehaviour->CancelAllTrackedCoroutines();
    m_setBehaviourIndex.erase(nType_);
    m_FrameUpdatePausedBehaviourTypes.erase(nType_);
    m_BehavioursMap.erase(_pBehaviour->GetComponentClass());
    _pBehaviour->Detach();
    _pBehaviour->UnbindCoroutineRuntime();
    m_OrderedList.erase(_Ite);
    RebuildExecutionPlan();
}

void iCAX::Behaviour::CBehaviourDispatcher::Shutdown()
{
    m_pCoroutineRuntime->CancelAll();
    for (const auto& _pBehaviour : m_OrderedList)
    {
        if (_pBehaviour)
        {
            _pBehaviour->Detach();
            _pBehaviour->ClearTrackedCoroutines();
            _pBehaviour->UnbindCoroutineRuntime();
        }
    }
    m_setBehaviourIndex.clear();
    m_FrameUpdatePausedBehaviourTypes.clear();
    m_BehavioursMap.clear();
    m_OrderedList.clear();
    m_ExecutionList.clear();
    m_ExecutionRankByComponentClass.clear();
    m_PendingDestroyNotifications.clear();
    m_nNextPendingDestroySequence = 0;
}

void iCAX::Behaviour::CBehaviourDispatcher::RebuildExecutionPlan()
{
    const auto _nCount = m_OrderedList.size();
    std::vector<CBehaviourSchedule> _Schedules(_nCount);
    std::unordered_map<std::string, size_t> _BehaviourIndexByClass;
    std::vector<std::vector<size_t>> _Edges(_nCount);
    std::vector<size_t> _InDegree(_nCount, 0);
    std::set<std::pair<size_t, size_t>> _UniqueEdges;

    for (size_t _Index = 0; _Index < _nCount; ++_Index)
    {
        const auto& _pBehaviour = m_OrderedList[_Index];
        if (!_pBehaviour)
        {
            throw std::runtime_error("Behaviour execution plan contains null behaviour");
        }

        const auto _strBehaviourClass = _pBehaviour->GetBehaviourClass();
        if (_strBehaviourClass.empty())
        {
            throw std::runtime_error("Behaviour class cannot be empty");
        }
        if (!_BehaviourIndexByClass.emplace(_strBehaviourClass, _Index).second)
        {
            throw std::runtime_error("Duplicated behaviour class in execution plan: " + _strBehaviourClass);
        }

        _Schedules[_Index] = _pBehaviour->GetSchedule();
    }

    auto _AddEdge = [&](IN const size_t nFrom_, IN const size_t nTo_)
    {
        if (nFrom_ == nTo_)
        {
            return;
        }
        if (_UniqueEdges.emplace(nFrom_, nTo_).second)
        {
            _Edges[nFrom_].push_back(nTo_);
            ++_InDegree[nTo_];
        }
    };

    for (size_t _Index = 0; _Index < _nCount; ++_Index)
    {
        for (const auto& _strAfter : _Schedules[_Index].RunAfter)
        {
            if (auto _Ite = _BehaviourIndexByClass.find(_strAfter); _Ite != _BehaviourIndexByClass.end())
            {
                _AddEdge(_Ite->second, _Index);
            }
        }

        for (const auto& _strBefore : _Schedules[_Index].RunBefore)
        {
            if (auto _Ite = _BehaviourIndexByClass.find(_strBefore); _Ite != _BehaviourIndexByClass.end())
            {
                _AddEdge(_Index, _Ite->second);
            }
        }
    }

    std::vector<bool> _Emitted(_nCount, false);
    std::vector<std::shared_ptr<CBehaviourBase>> _ExecutionList;
    _ExecutionList.reserve(_nCount);

    auto _IsEarlier = [&](IN const size_t nLeft_, IN const size_t nRight_)
    {
        const auto& _Left = _Schedules[nLeft_];
        const auto& _Right = _Schedules[nRight_];
        if (_Left.ExecutionOrder != _Right.ExecutionOrder)
        {
            return _Left.ExecutionOrder < _Right.ExecutionOrder;
        }
        return nLeft_ < nRight_;
    };

    while (_ExecutionList.size() < _nCount)
    {
        size_t _nCandidate = kUnboundBehaviourRank;
        for (size_t _Index = 0; _Index < _nCount; ++_Index)
        {
            if (_Emitted[_Index] || _InDegree[_Index] != 0)
            {
                continue;
            }
            if (_nCandidate == kUnboundBehaviourRank || _IsEarlier(_Index, _nCandidate))
            {
                _nCandidate = _Index;
            }
        }

        if (_nCandidate == kUnboundBehaviourRank)
        {
            std::string _strCycle;
            for (size_t _Index = 0; _Index < _nCount; ++_Index)
            {
                if (!_Emitted[_Index])
                {
                    if (!_strCycle.empty())
                    {
                        _strCycle += " -> ";
                    }
                    _strCycle += m_OrderedList[_Index]->GetBehaviourClass();
                }
            }
            throw std::runtime_error("Behaviour schedule cycle: " + _strCycle);
        }

        _Emitted[_nCandidate] = true;
        _ExecutionList.push_back(m_OrderedList[_nCandidate]);
        for (const auto _nNext : _Edges[_nCandidate])
        {
            --_InDegree[_nNext];
        }
    }

    m_ExecutionList = std::move(_ExecutionList);
    m_ExecutionRankByComponentClass.clear();
    for (size_t _Index = 0; _Index < m_ExecutionList.size(); ++_Index)
    {
        m_ExecutionRankByComponentClass[m_ExecutionList[_Index]->GetComponentClass()] = _Index;
    }
}

size_t iCAX::Behaviour::CBehaviourDispatcher::GetExecutionRank(IN const std::string& strComponentClass_) const
{
    auto _Ite = m_ExecutionRankByComponentClass.find(strComponentClass_);
    return _Ite != m_ExecutionRankByComponentClass.end() ? _Ite->second : kUnboundBehaviourRank;
}

void iCAX::Behaviour::CBehaviourDispatcher::QueueDestroyNotification(
    IN const CComponentDestroyInfo& DestroyInfo_) const
{
    if (DestroyInfo_.ComponentClass.empty())
    {
        return;
    }

    NotifyRequest _Request;
    _Request.Type = kDestroyComponent;
    _Request.Properties = DestroyInfo_.PreviousProperties;
    _Request.DestroyInfo = DestroyInfo_;
    _Request.Sequence = m_nNextPendingDestroySequence++;
    m_PendingDestroyNotifications.push_back(std::move(_Request));
}

void iCAX::Behaviour::CBehaviourDispatcher::DispatchDestroyImmediateAndQueue(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
    IN const CComponentDestroyInfo& DestroyInfo_) const
{
    if (DestroyInfo_.ComponentClass.empty())
    {
        return;
    }

    auto _Ite = m_BehavioursMap.find(DestroyInfo_.ComponentClass);
    if (_Ite == m_BehavioursMap.end())
    {
        return;
    }

    if (pComponent_)
    {
        _Ite->second->DetachComponentCoroutines(*pComponent_);
        try
        {
            _Ite->second->DestroyImmediate(*pComponent_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
        }
        catch (...)
        {
            _Ite->second->CompleteComponentCoroutineDetach(*pComponent_);
            throw;
        }
        _Ite->second->CompleteComponentCoroutineDetach(*pComponent_);
    }
    QueueDestroyNotification(DestroyInfo_);
}

void iCAX::Behaviour::CBehaviourDispatcher::FlushPendingDestroyNotifications(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) const
{
    size_t _nProcessedNotificationCount = 0;

    // OnDestroy 允许触发新的组件删除。销毁阶段必须一直处理到队列为空，
    // 否则级联删除会被拆到多帧，生命周期语义会变得不可预测。
    while (!m_PendingDestroyNotifications.empty())
    {
        auto _Notifications = std::move(m_PendingDestroyNotifications);
        m_PendingDestroyNotifications.clear();

        std::vector<size_t> _OrderedIndices;
        _OrderedIndices.reserve(_Notifications.size());
        for (size_t _Index = 0; _Index < _Notifications.size(); ++_Index)
        {
            const auto& _Notification = _Notifications[_Index];
            if (_Notification.DestroyInfo.ComponentClass.empty())
            {
                continue;
            }
            if (GetExecutionRank(_Notification.DestroyInfo.ComponentClass) == kUnboundBehaviourRank)
            {
                continue;
            }
            _OrderedIndices.push_back(_Index);
        }

        std::stable_sort(
            _OrderedIndices.begin(),
            _OrderedIndices.end(),
            [&](IN const size_t nLeft_, IN const size_t nRight_)
            {
                const auto& _Left = _Notifications[nLeft_];
                const auto& _Right = _Notifications[nRight_];
                const auto _nLeftRank = GetExecutionRank(_Left.DestroyInfo.ComponentClass);
                const auto _nRightRank = GetExecutionRank(_Right.DestroyInfo.ComponentClass);
                if (_nLeftRank != _nRightRank)
                {
                    return _nLeftRank < _nRightRank;
                }
                return _Left.Sequence < _Right.Sequence;
            });

        for (const auto _Index : _OrderedIndices)
        {
            if (++_nProcessedNotificationCount > kMaxDestroyFlushNotificationCount)
            {
                throw std::runtime_error("Behaviour destroy notification flush exceeded the safety limit");
            }

            const auto& _Notification = _Notifications[_Index];
            auto _Ite = m_BehavioursMap.find(_Notification.DestroyInfo.ComponentClass);
            if (_Ite == m_BehavioursMap.end())
            {
                continue;
            }

            _Ite->second->Destroy(
                _Notification.DestroyInfo,
                ApplicationContext_,
                ProductContext_,
                ProjectContext_,
                SceneContext_);
        }
    }
}

//!< tick
void iCAX::Behaviour::CBehaviourDispatcher::Tick(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_,
    IN std::function<void()> CoroutinePhase_) const
{
    auto& _View = SceneContext_.Database().GetView();
    FlushPendingDestroyNotifications(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);

    // Start 只针对本帧首次出现在当前组件缓存中的组件调用。
    // GetPreEntities 保存上一帧快照，RefreshPreCache 在 Start 阶段结束后推进。
    for (const auto& _pBehaviour : m_ExecutionList) 
    {
        if (!_pBehaviour)
        {
            continue;
        }
        _View.BuildCache(_pBehaviour->GetComponentClass(), false);
        auto _pCache = _View.GetEntities(_pBehaviour->GetComponentClass());
        auto _p2ndCache = _View.GetPreEntities(_pBehaviour->GetComponentClass());

        for (auto& _pComponent : _pCache)
        {
            if (std::find_if(_p2ndCache.begin(), _p2ndCache.end(), [&](const std::shared_ptr<iCAX::Database::CComponentBase>& p) 
                {
                    return  p.get() == _pComponent.get();
                }) == _p2ndCache.end())
            {
                _pBehaviour->Start(*_pComponent, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
            }
        }
    }
    _View.RefreshPreCache();

    // PreUpdate、Update、PostUpdate 都按行为调度计划执行；每个行为内部遍历它关注的组件缓存。
    for (const auto& _pBehaviour : m_ExecutionList)
    {
        if (!_pBehaviour)
        {
            continue;
        }
        if (IsFrameUpdatePaused(typeid(*_pBehaviour)))
        {
            continue;
        }
        auto _pCache = _View.GetEntities(_pBehaviour->GetComponentClass());
        for (auto& _pComponent : _pCache)
        {
            if (!_pComponent || !_pComponent->IsEnable())
            {
                continue;
            }
            _pBehaviour->PreUpdate(*_pComponent, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, nDeltaTime_, nTotalTime_);
        }
    }

    //!< OnUpdate
    for (const auto& _pBehaviour : m_ExecutionList)
    {
        if (!_pBehaviour)
        {
            continue;
        }
        if (IsFrameUpdatePaused(typeid(*_pBehaviour)))
        {
            continue;
        }
        auto _pCache = _View.GetEntities(_pBehaviour->GetComponentClass());
        for (auto& _pComponent : _pCache)
        {
            if (!_pComponent || !_pComponent->IsEnable())
            {
                continue;
            }
            _pBehaviour->Update(*_pComponent, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, nDeltaTime_, nTotalTime_);
        }
    }

    //!< Component Coroutine：固定在 Universe 线程，位于 Update 与 PostUpdate 之间。
    if (CoroutinePhase_)
    {
        CoroutinePhase_();
    }

    for (const auto& _pBehaviour : m_ExecutionList)
    {
        if (!_pBehaviour)
        {
            continue;
        }
        if (IsFrameUpdatePaused(typeid(*_pBehaviour)))
        {
            continue;
        }
        auto _pCache = _View.GetEntities(_pBehaviour->GetComponentClass());
        for (auto& _pComponent : _pCache)
        {
            if (!_pComponent || !_pComponent->IsEnable())
            {
                continue;
            }
            _pBehaviour->PostUpdate(*_pComponent, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, nDeltaTime_, nTotalTime_);
        }
    }
}

void iCAX::Behaviour::CBehaviourDispatcher::OnRepositoryChanging(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const iCAX::Database::RepositoryEventArgs& Args_) const
{
    if (!Args_.pComponent)
    {
        return;
    }

    if (Args_.nType == iCAX::Database::RepositoryEventArgs::kRemoveComponent)
    {
        // 删除链路中只能同步触发 DestroyImmediate，并把 delayed Destroy 的快照入队。
        // OnDestroy 本身必须等到 Tick 开头的统一销毁阶段再执行。
        DispatchDestroyImmediateAndQueue(
            ApplicationContext_,
            ProductContext_,
            ProjectContext_,
            SceneContext_,
            Args_.pComponent,
            MakeDestroyInfo(Args_));
    }
    else if (Args_.nType == iCAX::Database::RepositoryEventArgs::kModifyComponent)
    {
        OnNotify(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, kModifyingComponent, *Args_.pComponent, Args_.NewProperties);
    }
}

void iCAX::Behaviour::CBehaviourDispatcher::OnRepositoryChanged(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const iCAX::Database::RepositoryEventArgs& Args_) const
{
    if (Args_.nType == iCAX::Database::RepositoryEventArgs::kBatchChanged)
    {
        if (!Args_.pBatch)
        {
            return;
        }

        std::vector<NotifyRequest> _Notifications;

        auto _QueueNotify = [&](
            IN const NotifyType nType_,
            IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
            IN const iCAX::Data::PropertySet& Properties_,
            IN const size_t nSequence_)
        {
            if (!pComponent_)
            {
                return;
            }

            NotifyRequest _Request;
            _Request.Type = nType_;
            _Request.pComponent = pComponent_;
            _Request.Properties = Properties_;
            _Request.Sequence = nSequence_;
            _Notifications.push_back(std::move(_Request));
        };

        auto _QueueDestroy = [&](
            IN const ComponentKey& Key_,
            IN const iCAX::Data::PropertySet& Properties_,
            IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
            IN const size_t nSequence_)
        {
            NotifyRequest _Request;
            _Request.Type = kDestroyComponent;
            _Request.pComponent = pComponent_;
            _Request.Properties = Properties_;
            _Request.DestroyInfo = MakeDestroyInfo(Key_, Properties_);
            _Request.Sequence = nSequence_;
            _Notifications.push_back(std::move(_Request));
        };

        std::map<ComponentKey, ComponentBatchState> _ComponentStates;
        for (size_t _Index = 0; _Index < Args_.pBatch->Records.size(); ++_Index)
        {
            const auto& _Record = Args_.pBatch->Records[_Index];
            if (_Record.strClassName.empty())
            {
                continue;
            }

            const auto _Key = MakeComponentKey(_Record);
            auto& _State = _ComponentStates[_Key];

            switch (_Record.nType)
            {
            case iCAX::Database::RepositoryEventArgs::kAddComponent:
                if (!_State.HasStructuralEvent)
                {
                    _State.HasStructuralEvent = true;
                    _State.FirstStructuralEvent = _Record.nType;
                }
                _State.LastStructuralEvent = _Record.nType;
                _State.LastAddSequence = _Index;
                _State.LastAddProperties = _Record.NewProperties;
                _State.pLastAddComponent = _Record.pComponent;
                break;
            case iCAX::Database::RepositoryEventArgs::kRemoveComponent:
                if (!_State.HasStructuralEvent)
                {
                    _State.HasStructuralEvent = true;
                    _State.FirstStructuralEvent = _Record.nType;
                }
                _State.LastStructuralEvent = _Record.nType;
                _State.LastRemoveSequence = _Index;
                _State.LastRemoveProperties = _Record.PreviousProperties;
                _State.pLastRemoveComponent = _Record.pComponent;
                break;
            case iCAX::Database::RepositoryEventArgs::kModifyComponent:
                if (_State.HasStructuralEvent && _State.LastStructuralEvent == iCAX::Database::RepositoryEventArgs::kAddComponent)
                {
                    MergeProperties(_State.LastAddProperties, _Record.NewProperties);
                    _State.pLastAddComponent = _Record.pComponent ? _Record.pComponent : _State.pLastAddComponent;
                }
                else if (!_State.HasStructuralEvent)
                {
                    if (!_State.HasModifyEvent)
                    {
                        _State.FirstModifySequence = _Index;
                        _State.pModifiedComponent = _Record.pComponent;
                    }
                    _State.HasModifyEvent = true;
                    MergeProperties(_State.ModifiedProperties, _Record.NewProperties);
                    if (_Record.pComponent)
                    {
                        _State.pModifiedComponent = _Record.pComponent;
                    }
                }
                break;
            case iCAX::Database::RepositoryEventArgs::kEnableComponent:
            case iCAX::Database::RepositoryEventArgs::kDisableComponent:
                if (!_State.HasEnableStateEvent)
                {
                    _State.HasEnableStateEvent = true;
                    _State.FirstEnableStateEvent = _Record.nType;
                }
                _State.LastEnableStateEvent = _Record.nType;
                _State.LastEnableStateSequence = _Index;
                _State.LastEnableStateProperties = _Record.NewProperties;
                _State.pLastEnableStateComponent = _Record.pComponent;
                break;
            default:
                break;
            }
        }

        for (const auto& [_Key, _State] : _ComponentStates)
        {
            const bool _bExistedBeforeBatch = !_State.HasStructuralEvent
                || _State.FirstStructuralEvent != iCAX::Database::RepositoryEventArgs::kAddComponent;
            const bool _bExistsAfterBatch = !_State.HasStructuralEvent
                || _State.LastStructuralEvent == iCAX::Database::RepositoryEventArgs::kAddComponent;

            if (_State.HasStructuralEvent && _bExistedBeforeBatch)
            {
                _QueueDestroy(_Key, _State.LastRemoveProperties, _State.pLastRemoveComponent, _State.LastRemoveSequence);
            }
            if (_State.HasStructuralEvent && _bExistsAfterBatch && _State.LastStructuralEvent == iCAX::Database::RepositoryEventArgs::kAddComponent)
            {
                auto _pComponent = _State.pLastAddComponent ? _State.pLastAddComponent : FindCurrentComponent(SceneContext_, _Key);
                _QueueNotify(kAddComponent, _pComponent, _State.LastAddProperties, _State.LastAddSequence);
            }
            if (!_State.HasStructuralEvent && _State.HasModifyEvent)
            {
                auto _pComponent = _State.pModifiedComponent ? _State.pModifiedComponent : FindCurrentComponent(SceneContext_, _Key);
                _QueueNotify(kModifiedComponent, _pComponent, _State.ModifiedProperties, _State.FirstModifySequence);
            }
            if (_State.HasEnableStateEvent && _bExistedBeforeBatch && _bExistsAfterBatch)
            {
                const bool _bInitiallyEnabled = _State.FirstEnableStateEvent == iCAX::Database::RepositoryEventArgs::kDisableComponent;
                const bool _bFinallyEnabled = _State.LastEnableStateEvent == iCAX::Database::RepositoryEventArgs::kEnableComponent;
                if (_bInitiallyEnabled != _bFinallyEnabled)
                {
                    auto _pComponent = _State.pLastEnableStateComponent ? _State.pLastEnableStateComponent : FindCurrentComponent(SceneContext_, _Key);
                    _QueueNotify(
                        _bFinallyEnabled ? kEnableComponent : kDisableComponent,
                        _pComponent,
                        _State.LastEnableStateProperties,
                        _State.LastEnableStateSequence);
                }
            }
        }

        OnNotifyBatch(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, _Notifications);
        return;
    }

    if (!Args_.pComponent)
    {
        return;
    }

    switch (Args_.nType)
    {
    case iCAX::Database::RepositoryEventArgs::kAddComponent:
        OnNotify(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, kAddComponent, *Args_.pComponent, Args_.NewProperties);
        break;
    case iCAX::Database::RepositoryEventArgs::kEnableComponent:
        OnNotify(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, kEnableComponent, *Args_.pComponent, Args_.NewProperties);
        break;
    case iCAX::Database::RepositoryEventArgs::kDisableComponent:
        OnNotify(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, kDisableComponent, *Args_.pComponent, Args_.NewProperties);
        break;
    case iCAX::Database::RepositoryEventArgs::kModifyComponent:
        OnNotify(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, kModifiedComponent, *Args_.pComponent, Args_.NewProperties);
        break;
    default:
        break;
    }
}

//!< 通知
void iCAX::Behaviour::CBehaviourDispatcher::OnNotify(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN NotifyType nType_,
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Data::PropertySet& Properties_) const
{
    auto _Ite = m_BehavioursMap.find(Component_.GetComponentClass());
    if (_Ite == m_BehavioursMap.end())
    {
        return;
    }
    // Repository 只发布数据事件；Dispatcher 在这里把数据事件翻译成 Behaviour 生命周期回调。
    switch (nType_)
    {
    case kAddComponent:
        _Ite->second->AttachComponentCoroutines(Component_);
        _Ite->second->Awake(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
        break;
    case kEnableComponent:
        _Ite->second->SetComponentCoroutinesEnabled(Component_, true);
        _Ite->second->Enable(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
        break;
    case kDisableComponent:
        _Ite->second->SetComponentCoroutinesEnabled(Component_, false);
        _Ite->second->Disable(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
        break;
    case kDestroyComponent:
        throw std::logic_error("Destroy notifications must use the destroy queue");
    case kModifyingComponent:
        _Ite->second->Modifying(Component_, Properties_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
        break;
    case kModifiedComponent:
        _Ite->second->Modified(Component_, Properties_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
        break;
    default:
        break;
    }
   
}

void iCAX::Behaviour::CBehaviourDispatcher::OnNotifyBatch(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const std::vector<NotifyRequest>& Notifications_) const
{
    auto _GetNotifyComponentClass = [](IN const NotifyRequest& Notification_)
    {
        if (Notification_.Type == kDestroyComponent && !Notification_.DestroyInfo.ComponentClass.empty())
        {
            return Notification_.DestroyInfo.ComponentClass;
        }

        return Notification_.pComponent ? Notification_.pComponent->GetComponentClass() : std::string();
    };

    std::vector<size_t> _OrderedIndices;
    _OrderedIndices.reserve(Notifications_.size());
    for (size_t _Index = 0; _Index < Notifications_.size(); ++_Index)
    {
        const auto& _Notification = Notifications_[_Index];
        const auto _strComponentClass = _GetNotifyComponentClass(_Notification);
        if (_strComponentClass.empty())
        {
            continue;
        }
        if (GetExecutionRank(_strComponentClass) == kUnboundBehaviourRank)
        {
            continue;
        }
        _OrderedIndices.push_back(_Index);
    }

    std::stable_sort(
        _OrderedIndices.begin(),
        _OrderedIndices.end(),
        [&](IN const size_t nLeft_, IN const size_t nRight_)
        {
            const auto& _Left = Notifications_[nLeft_];
            const auto& _Right = Notifications_[nRight_];
            const auto _nLeftRank = GetExecutionRank(_GetNotifyComponentClass(_Left));
            const auto _nRightRank = GetExecutionRank(_GetNotifyComponentClass(_Right));
            if (_nLeftRank != _nRightRank)
            {
                return _nLeftRank < _nRightRank;
            }
            return _Left.Sequence < _Right.Sequence;
        });

    for (const auto _Index : _OrderedIndices)
    {
        const auto& _Notification = Notifications_[_Index];
        if (_Notification.Type == kDestroyComponent)
        {
            DispatchDestroyImmediateAndQueue(
                ApplicationContext_,
                ProductContext_,
                ProjectContext_,
                SceneContext_,
                _Notification.pComponent,
                _Notification.DestroyInfo);
        }
        else
        {
            if (!_Notification.pComponent)
            {
                continue;
            }
            OnNotify(
                ApplicationContext_,
                ProductContext_,
                ProjectContext_,
                SceneContext_,
                _Notification.Type,
                *_Notification.pComponent,
                _Notification.Properties);
        }
    }
}
