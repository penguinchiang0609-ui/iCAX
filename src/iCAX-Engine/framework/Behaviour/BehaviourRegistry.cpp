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
void iCAX::Behaviour::CBehaviourRegistry::RegisterBehaviourFactory(IN const CBehaviourDescriptor& Descriptor_)
{
    if (!Descriptor_.Factory)
    {
        throw std::invalid_argument("Behaviour factory cannot be empty");
    }
    if (Descriptor_.Type == std::type_index(typeid(void)))
    {
        throw std::invalid_argument("Behaviour type cannot be void");
    }
    if (Descriptor_.BehaviourClass.empty())
    {
        throw std::invalid_argument("Behaviour class cannot be empty");
    }
    if (Descriptor_.ComponentClass.empty())
    {
        throw std::invalid_argument("Behaviour component class cannot be empty");
    }

    auto _NameIter = m_BehaviourTypesByName.find(Descriptor_.BehaviourClass);
    auto _TypeIter = m_Behaviours.find(Descriptor_.Type);
    auto _ComponentIter = m_BehaviourTypesByComponent.find(Descriptor_.ComponentClass);
    if (_NameIter != m_BehaviourTypesByName.end()
        || _TypeIter != m_Behaviours.end()
        || _ComponentIter != m_BehaviourTypesByComponent.end())
    {
        if (_NameIter != m_BehaviourTypesByName.end()
            && _TypeIter != m_Behaviours.end()
            && _ComponentIter != m_BehaviourTypesByComponent.end()
            && _NameIter->second == Descriptor_.Type
            && _ComponentIter->second == Descriptor_.Type
            && _TypeIter->second.BehaviourClass == Descriptor_.BehaviourClass
            && _TypeIter->second.ComponentClass == Descriptor_.ComponentClass)
        {
            return;
        }
        throw std::runtime_error(std::format("CBehaviourRegistry::RegisterBehaviourFactory {} conflicts", Descriptor_.BehaviourClass));
    }

    m_Behaviours.emplace(Descriptor_.Type, Descriptor_);
    m_BehaviourTypesByName.emplace(Descriptor_.BehaviourClass, Descriptor_.Type);
    m_BehaviourTypesByComponent.emplace(Descriptor_.ComponentClass, Descriptor_.Type);
    m_BehaviourRegistrationOrder.push_back(Descriptor_.Type);
}

//! 是否已包含
bool iCAX::Behaviour::CBehaviourRegistry::HasBehaviourByType(IN const std::type_index& nType_) const
{
    return  m_Behaviours.find(nType_) != m_Behaviours.end();
}

//! 是否已包含
bool iCAX::Behaviour::CBehaviourRegistry::HasBehaviourByType(IN const std::string& strType_) const
{
    return  m_BehaviourTypesByName.find(strType_) != m_BehaviourTypesByName.end();
}

//! 获取行为
std::shared_ptr<iCAX::Behaviour::CBehaviourBase> iCAX::Behaviour::CBehaviourRegistry::CreateBehaviourByType(IN const std::type_index& nType_) const
{
    auto _Ite = m_Behaviours.find(nType_);
    if (_Ite != m_Behaviours.end())
    {
        return _Ite->second.Factory();
    }
    return nullptr;
}

//! 获取行为
std::shared_ptr<iCAX::Behaviour::CBehaviourBase> iCAX::Behaviour::CBehaviourRegistry::CreateBehaviourByType(IN const std::string& strType_) const
{
    auto _Ite = m_BehaviourTypesByName.find(strType_);
    if (_Ite != m_BehaviourTypesByName.end())
    {
        return CreateBehaviourByType(_Ite->second);
    }
    return nullptr;
}

const iCAX::Behaviour::CBehaviourDescriptor* iCAX::Behaviour::CBehaviourRegistry::GetDescriptorByType(IN const std::type_index& nType_) const
{
    auto _Ite = m_Behaviours.find(nType_);
    if (_Ite != m_Behaviours.end())
    {
        return &_Ite->second;
    }
    return nullptr;
}

const iCAX::Behaviour::CBehaviourDescriptor* iCAX::Behaviour::CBehaviourRegistry::GetDescriptorByType(IN const std::string& strType_) const
{
    auto _Ite = m_BehaviourTypesByName.find(strType_);
    if (_Ite != m_BehaviourTypesByName.end())
    {
        return GetDescriptorByType(_Ite->second);
    }
    return nullptr;
}

std::vector<std::type_index> iCAX::Behaviour::CBehaviourRegistry::GetTypeIndexByComponentType(IN const std::string& strComponent_) const
{
    std::vector<std::type_index> _vec;
    if (auto _Ite = m_BehaviourTypesByComponent.find(strComponent_); _Ite != m_BehaviourTypesByComponent.end())
    {
        _vec.push_back(_Ite->second);
    }
    return _vec;
}

std::vector<std::type_index> iCAX::Behaviour::CBehaviourRegistry::ListBehaviourTypes() const
{
    return m_BehaviourRegistrationOrder;
}
