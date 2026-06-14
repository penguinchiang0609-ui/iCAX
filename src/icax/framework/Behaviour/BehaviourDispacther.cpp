#include "pch.h"
#include "BehaviourDispacther.h"
#include "World.h"
#include <algorithm>
#include "../Database/IDomain.h"
#include "../Database/ComponentBase.h"
#include <algorithm>

//!< 构造函数
iCAX::Behaviour::CBehaviourDispatcher::CBehaviourDispatcher()
    : m_setBehaviourIndex()
    , m_BehavioursMap()
    , m_OrderedList()
{
}

//!< 注册Behaviour
bool iCAX::Behaviour::CBehaviourDispatcher::Pushback(IN const std::type_index& nType_)
{
    if (!m_setBehaviourIndex.contains(nType_))
    {
        m_setBehaviourIndex.emplace(nType_);

        std::shared_ptr<CBehaviourBase> _pBehaviour = GetGlobalBehaviourRegistry()->GetBehaviourByType(nType_);
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
        _Res.push_back(GetGlobalBehaviourRegistry()->GetBehaviourByType(_nType));
    }

    return _Res;
}

//! 获取所有被绑定的行为
std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CBehaviourDispatcher::GetALL() const
{
    std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> _Res;
    for (auto& _nType : m_setBehaviourIndex)
    {
        _Res.push_back(GetGlobalBehaviourRegistry()->GetBehaviourByType(_nType));
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
    std::shared_ptr<CBehaviourBase> _pBehaviour = GetGlobalBehaviourRegistry()->GetBehaviourByType(nType_);
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
void iCAX::Behaviour::CBehaviourDispatcher::Tick(IN const IUniverseContext& Context_, IN class IWorld& World_, IN const double& nDeltaTime_, IN const double& nTotalTime_) const
{
    auto _pUniverse = World_.GetUniverse();
    if (!_pUniverse)
    {
        return;
    }
    auto _pDomain = _pUniverse->GetContext().GetDatabase().GetDomain(World_.GetID());
    if (!_pDomain)
    {
        return;
    }

    //!< OnStart
    for (const auto& _pBehaviour : m_OrderedList) 
    {
        if (IsPaused(typeid(*_pBehaviour)))//! 被暂停的则跳过
        {
            continue;
        }
        auto _pCache = _pDomain->GetView().GetEntities(_pBehaviour->GetComponentClass());
        auto _p2ndCache = _pDomain->GetView().GetPreEntities(_pBehaviour->GetComponentClass());

        for (auto& _pComponent : _pCache)
        {
            if (std::find_if(_p2ndCache.begin(), _p2ndCache.end(), [&](const std::shared_ptr<iCAX::Database::CComponentBase>& p) 
                {
                    return  p.get() == _pComponent.get();
                }) == _p2ndCache.end())
            {
                _pBehaviour->Start(World_, *_pComponent, Context_);
            }
        }
    }
    _pDomain->GetView().RefreshPreCache();

    //!< OnPreUpdate
    for (const auto& _pBehaviour : m_OrderedList)
    {
        if (IsPaused(typeid(*_pBehaviour)))
        {
            continue;
        }
        auto _pCache = _pDomain->GetView().GetEntities(_pBehaviour->GetComponentClass());
        for (auto& _pComponent : _pCache)
        {
            _pBehaviour->PreUpdate(World_, *_pComponent, nDeltaTime_, nTotalTime_, Context_);
        }
    }

    //!< OnUpdate
    for (const auto& _pBehaviour : m_OrderedList)
    {
        if (IsPaused(typeid(*_pBehaviour)))
        {
            continue;
        }
        auto _pCache = _pDomain->GetView().GetEntities(_pBehaviour->GetComponentClass());
        for (auto& _pComponent : _pCache)
        {
            _pBehaviour->Update(World_, *_pComponent, nDeltaTime_, nTotalTime_, Context_);
        }
    }

    //!< OnPostUpdate
    for (const auto& _pBehaviour : m_OrderedList)
    {
        if (IsPaused(typeid(*_pBehaviour)))
        {
            continue;
        }
        auto _pCache = _pDomain->GetView().GetEntities(_pBehaviour->GetComponentClass());
        for (auto& _pComponent : _pCache)
        {
            _pBehaviour->PostUpdate(World_, *_pComponent, nDeltaTime_, nTotalTime_, Context_);
        }
    }
}

//!< 通知
void iCAX::Behaviour::CBehaviourDispatcher::OnNotify(IN const IUniverseContext& Context_, IN IWorld& World_, IN NotifyType nType_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& Properties_) const
{
    auto _Ite = m_BehavioursMap.find(Component_.GetComponentClass());
    if (_Ite == m_BehavioursMap.end())
    {
        return;
    }
    switch (nType_)
    {
    case kAddComponent:
        _Ite->second->Awake(World_, Component_, Context_);
        break;
    case kEnableComponent:
        _Ite->second->Enable(World_, Component_, Context_);
        break;
    case kDisableComponent:
        _Ite->second->Disable(World_, Component_, Context_);
        break;
    case kDestroyComponent:
        _Ite->second->Destory(World_, Component_, Context_);
        break;
    case kModifingComponent:
        _Ite->second->Modifing(World_, Component_, Properties_, Context_);
        break;
    case kModifiedComponent:
        _Ite->second->Modified(World_, Component_, Properties_, Context_);
        break;
    default:
        break;
    }
   
}
