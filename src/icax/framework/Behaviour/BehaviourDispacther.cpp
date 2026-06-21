#include "pch.h"
#include "BehaviourDispacther.h"
#include "IBehaviourRegistry.h"
#include <algorithm>
#include "../Database/ComponentBase.h"
#include <algorithm>
#include <utility>

//!< 构造函数
iCAX::Behaviour::CBehaviourDispatcher::CBehaviourDispatcher(IN std::shared_ptr<IBehaviourRegistry> pRegistry_)
    : m_setBehaviourIndex()
    , m_BehavioursMap()
    , m_OrderedList()
    , m_pRegistry(pRegistry_ ? std::move(pRegistry_) : GetGlobalBehaviourRegistry())
{
}

//!< 注册Behaviour
bool iCAX::Behaviour::CBehaviourDispatcher::Pushback(IN const std::type_index& nType_)
{
    if (!m_setBehaviourIndex.contains(nType_))
    {
        m_setBehaviourIndex.emplace(nType_);

        std::shared_ptr<CBehaviourBase> _pBehaviour = m_pRegistry->GetBehaviourByType(nType_);
        m_OrderedList.push_back(_pBehaviour);
        m_BehavioursMap[_pBehaviour->GetComponentClass()] = _pBehaviour;

        return true;
    }

    return false;
}

//! 暂停
void iCAX::Behaviour::CBehaviourDispatcher::Pause(IN const std::type_index& nType_)
{
    m_setPaused.emplace(nType_);
}

//! 是否暂停
bool iCAX::Behaviour::CBehaviourDispatcher::IsPaused(IN const std::type_index& nType_) const
{
    return m_setPaused.contains(nType_);
}

//! 恢复指定类型的行为执行
void iCAX::Behaviour::CBehaviourDispatcher::Resume(IN const std::type_index& nType_)
{
    m_setPaused.erase(nType_);
}

//! 获取所有被暂停的行为
std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CBehaviourDispatcher::GetPaused() const
{
    std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> _Res;
    for (auto& _nType : m_setPaused)
    {
        _Res.push_back(m_pRegistry->GetBehaviourByType(_nType));
    }

    return _Res;
}

//! 获取所有被绑定的行为
std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CBehaviourDispatcher::GetALL() const
{
    std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> _Res;
    for (auto& _nType : m_setBehaviourIndex)
    {
        _Res.push_back(m_pRegistry->GetBehaviourByType(_nType));
    }

    return _Res;
}

//! 是否已包含
bool iCAX::Behaviour::CBehaviourDispatcher::HasBehaviour(IN const std::type_index& nType_) const
{
    return  m_setBehaviourIndex.contains(nType_);
}

//! 注销行为
void iCAX::Behaviour::CBehaviourDispatcher::UnregisterBehaviour(IN const std::type_index& nType_)
{
    std::shared_ptr<CBehaviourBase> _pBehaviour = m_pRegistry->GetBehaviourByType(nType_);
    if (!_pBehaviour)
    {
        return;
    }

    m_setBehaviourIndex.erase(nType_);
    m_BehavioursMap.erase(_pBehaviour->GetComponentClass());
    m_OrderedList.erase
    (
        std::remove(m_OrderedList.begin(), m_OrderedList.end(), _pBehaviour),
        m_OrderedList.end()
    );
}

//!< tick
void iCAX::Behaviour::CBehaviourDispatcher::Tick(IN const IUniverseContext& Context_, IN const double& nDeltaTime_, IN const double& nTotalTime_) const
{
    auto& _View = Context_.GetDatabase().GetView();

    // Start 只针对本帧首次出现在当前组件缓存中的组件调用。
    // GetPreEntities 保存上一帧快照，RefreshPreCache 在 Start 阶段结束后推进。
    for (const auto& _pBehaviour : m_OrderedList) 
    {
        if (IsPaused(typeid(*_pBehaviour)))//! 被暂停的则跳过
        {
            continue;
        }
        auto _pCache = _View.GetEntities(_pBehaviour->GetComponentClass());
        auto _p2ndCache = _View.GetPreEntities(_pBehaviour->GetComponentClass());

        for (auto& _pComponent : _pCache)
        {
            if (std::find_if(_p2ndCache.begin(), _p2ndCache.end(), [&](const std::shared_ptr<iCAX::Database::CComponentBase>& p) 
                {
                    return  p.get() == _pComponent.get();
                }) == _p2ndCache.end())
            {
                _pBehaviour->Start(*_pComponent, Context_);
            }
        }
    }
    _View.RefreshPreCache();

    // PreUpdate、Update、PostUpdate 都按行为绑定顺序执行；每个行为内部遍历它关注的组件缓存。
    for (const auto& _pBehaviour : m_OrderedList)
    {
        if (IsPaused(typeid(*_pBehaviour)))
        {
            continue;
        }
        auto _pCache = _View.GetEntities(_pBehaviour->GetComponentClass());
        for (auto& _pComponent : _pCache)
        {
            _pBehaviour->PreUpdate(*_pComponent, nDeltaTime_, nTotalTime_, Context_);
        }
    }

    //!< OnUpdate
    for (const auto& _pBehaviour : m_OrderedList)
    {
        if (IsPaused(typeid(*_pBehaviour)))
        {
            continue;
        }
        auto _pCache = _View.GetEntities(_pBehaviour->GetComponentClass());
        for (auto& _pComponent : _pCache)
        {
            _pBehaviour->Update(*_pComponent, nDeltaTime_, nTotalTime_, Context_);
        }
    }

    //!< OnPostUpdate
    for (const auto& _pBehaviour : m_OrderedList)
    {
        if (IsPaused(typeid(*_pBehaviour)))
        {
            continue;
        }
        auto _pCache = _View.GetEntities(_pBehaviour->GetComponentClass());
        for (auto& _pComponent : _pCache)
        {
            _pBehaviour->PostUpdate(*_pComponent, nDeltaTime_, nTotalTime_, Context_);
        }
    }
}

//!< 通知
void iCAX::Behaviour::CBehaviourDispatcher::OnNotify(IN const IUniverseContext& Context_, IN NotifyType nType_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& Properties_) const
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
        _Ite->second->Awake(Component_, Context_);
        break;
    case kEnableComponent:
        _Ite->second->Enable(Component_, Context_);
        break;
    case kDisableComponent:
        _Ite->second->Disable(Component_, Context_);
        break;
    case kDestroyComponent:
        _Ite->second->Destory(Component_, Context_);
        break;
    case kModifingComponent:
        _Ite->second->Modifing(Component_, Properties_, Context_);
        break;
    case kModifiedComponent:
        _Ite->second->Modified(Component_, Properties_, Context_);
        break;
    default:
        break;
    }
   
}
