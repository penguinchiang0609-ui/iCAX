#include "pch.h"
#include "BehaviourRegistry.h"

//! 构造函数
iCAX::Behaviour::CBehaviourRegistry::CBehaviourRegistry()
{
}

//! 析构函数
iCAX::Behaviour::CBehaviourRegistry::~CBehaviourRegistry()
{
}

//!< 注册Behaviour
void iCAX::Behaviour::CBehaviourRegistry::RegisterBehaviourByInstance(IN const std::shared_ptr<CBehaviourBase>& pBehaviour_)
{
    if (!pBehaviour_)
    {
        throw std::invalid_argument("Behaviour cannot be null");
    }

    std::type_index _nType = std::type_index(typeid(*pBehaviour_));
    auto _NameIter = m_BehavioursByName.find(pBehaviour_->GetBehaviourClass());
    auto _TypeIter = m_Behaviours.find(_nType);
    if (_NameIter != m_BehavioursByName.end() || _TypeIter != m_Behaviours.end())
    {
        if (_NameIter != m_BehavioursByName.end()
            && _TypeIter != m_Behaviours.end()
            && _NameIter->second == _TypeIter->second)
        {
            return;
        }
        throw std::runtime_error(std::format("CBehaviourRegistry::RegisterBehaviourByInstance {} conflicts", pBehaviour_->GetBehaviourClass()));
    }


    m_Behaviours[_nType] = pBehaviour_;
    m_BehavioursByName[pBehaviour_->GetBehaviourClass()] = pBehaviour_;
}

//! 是否已包含
bool iCAX::Behaviour::CBehaviourRegistry::HasBehaviourByType(IN const std::type_index& nType_) const
{
    return  m_Behaviours.find(nType_) != m_Behaviours.end();
}

//! 是否已包含
bool iCAX::Behaviour::CBehaviourRegistry::HasBehaviourByType(IN const std::string& strType_) const
{
    return  m_BehavioursByName.find(strType_) != m_BehavioursByName.end();
}

//! 获取行为
std::shared_ptr<iCAX::Behaviour::CBehaviourBase> iCAX::Behaviour::CBehaviourRegistry::GetBehaviourByType(IN const std::type_index& nType_) const
{
    auto _Ite = m_Behaviours.find(nType_);
    if (_Ite != m_Behaviours.end())
    {
        return _Ite->second;
    }
    return nullptr;
}

//! 获取行为
std::shared_ptr<iCAX::Behaviour::CBehaviourBase> iCAX::Behaviour::CBehaviourRegistry::GetBehaviourByType(IN const std::string& strType_) const
{
    auto _Ite = m_BehavioursByName.find(strType_);
    if (_Ite != m_BehavioursByName.end())
    {
        return _Ite->second;
    }
    return nullptr;
}

std::vector<std::type_index> iCAX::Behaviour::CBehaviourRegistry::GetTypeIndexByComponentType(IN const std::string& strComponent_) const
{
    std::vector<std::type_index> _vec;
    for (auto& [_Key, _Value] : m_Behaviours)
    {
        if (_Value->GetComponentClass() == strComponent_)
        {
            _vec.push_back(_Key);
        }
    }
    return _vec;
}
