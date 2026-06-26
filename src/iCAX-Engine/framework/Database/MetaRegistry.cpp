#include "pch.h"
#include "MetaRegistry.h"
#include "IEntity.h"
#include "IRepository.h"
#include "Data/CommonFunction.h"

//! 注册类型
void iCAX::Database::CMetaRegistry::RegistType(IN const std::string& strComponentClass_, IN const std::string& strParentComponentClass_)
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    uint32_t _nParent = FNV1a32(strParentComponentClass_.c_str());
    if (m_mapType.contains(_nClass))
    {
        if (auto _pExistingMeta = GetMeta(_nClass); _pExistingMeta && _pExistingMeta->strComponentClass != strComponentClass_)
        {
            throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::RegistType {} collides with {}", strComponentClass_, _pExistingMeta->strComponentClass));
        }
        if (m_mapType[_nClass] != _nParent)
        {
            throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::RegistType {} parent conflicts", strComponentClass_));
        }
        return;
    }
    m_mapType[_nClass] = _nParent;

    auto _pMeta = GetMeta(_nClass);
    _pMeta->strComponentClass = strComponentClass_;
}

//! 是否存在继承关系
bool iCAX::Database::CMetaRegistry::IsInheritance(IN const std::string& strComponentClass_, IN const std::string& strParentComponentClass_)
{
    if (strComponentClass_ == strParentComponentClass_)
    {
        return true;
    }
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    uint32_t _nParent = FNV1a32(strParentComponentClass_.c_str());

    return IsInheritance(_nClass, _nParent);
}

//! 是否包含组件类型
bool iCAX::Database::CMetaRegistry::HasTypeByName(IN const std::string& strComponentClass_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    return m_mapType.contains(_nClass);
}

//! 注册构造函数
void iCAX::Database::CMetaRegistry::RegistCreatorByName(IN const std::string& strComponentClass_, IN const std::function<std::shared_ptr<CComponentBase>(IN std::shared_ptr<IEntity>)>& Creator_)
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    auto _pMeta = GetMeta(_nClass);
    if (_pMeta->Creator)
    {
        return;
    }
    _pMeta->Creator = Creator_;
}

//! 构造
std::shared_ptr<iCAX::Database::CComponentBase> iCAX::Database::CMetaRegistry::CreateByName(IN const std::string& strComponentClass_, IN const std::shared_ptr<IEntity>& pEntity_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    auto _pMate = const_cast<CMetaRegistry const*>(this)->GetMeta(_nClass);
    if (_pMate == nullptr || _pMate->Creator == nullptr)
    {
        throw std::runtime_error("Component creator is not registered: " + strComponentClass_);
    }
    return _pMate->Creator(pEntity_);
}

//! 是否包含指定组件的构造
bool iCAX::Database::CMetaRegistry::HasCreatorByName(IN const std::string& strComponentClass_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    auto _pMate = const_cast<CMetaRegistry const*>(this)->GetMeta(_nClass);
    if (_pMate == nullptr || _pMate->Creator == nullptr)
    {
        return false;
    }
    else
    {
        return true;
    }
}

//! 注册属性
void iCAX::Database::CMetaRegistry::RegistPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const std::function<PropertyValue(const void*)>& Getter_, IN const std::function<void(void*, const PropertyValue&)>& Setter_, IN EPropertyPersistence Persistence_, IN EPropertyChangePolicy ChangePolicy_)
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    auto& _mapProperties = GetMeta(_nClass)->mapProperties;
    uint32_t _nProperyName = FNV1a32(strPropertyName_.c_str());
    auto _Ite = _mapProperties.find(_nProperyName);
    if (_Ite == _mapProperties.end())
    {
        _Ite = _mapProperties.insert(std::make_pair(_nProperyName, PropertyMeta())).first;
        GetMeta(_nClass)->vecPropertyOrder.push_back(_nProperyName);
    }
    else
    {
        if (_Ite->second.Name != strPropertyName_)
        {
            throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::RegistPropertyByName {}.{} collides with {}", strComponentClass_, strPropertyName_, _Ite->second.Name));
        }
        if (_Ite->second.Kind != EPropertyKind::Value
            || _Ite->second.Persistence != Persistence_
            || _Ite->second.ChangePolicy != ChangePolicy_)
        {
            throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::RegistPropertyByName {}.{} conflicts", strComponentClass_, strPropertyName_));
        }
        return;
    }
    _Ite->second.Name = strPropertyName_;
    _Ite->second.Kind = EPropertyKind::Value;
    _Ite->second.Persistence = Persistence_;
    _Ite->second.ChangePolicy = ChangePolicy_;
    _Ite->second.Getter = Getter_;
    _Ite->second.Setter = Setter_;
}

//! 注册派生属性
void iCAX::Database::CMetaRegistry::RegistDerivedPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_, IN EPropertyPersistence Persistence_, IN EPropertyChangePolicy ChangePolicy_)
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    auto& _mapProperties = GetMeta(_nClass)->mapProperties;
    uint32_t _nProperyName = FNV1a32(strPropertyName_.c_str());
    auto _Ite = _mapProperties.find(_nProperyName);
    if (_Ite == _mapProperties.end())
    {
        _Ite = _mapProperties.insert(std::make_pair(_nProperyName, PropertyMeta())).first;
        GetMeta(_nClass)->vecPropertyOrder.push_back(_nProperyName);
    }
    else
    {
        if (_Ite->second.Name != strPropertyName_)
        {
            throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::RegistDerivedPropertyByName {}.{} collides with {}", strComponentClass_, strPropertyName_, _Ite->second.Name));
        }
        if (_Ite->second.Kind != EPropertyKind::Derived
            || _Ite->second.Persistence != Persistence_
            || _Ite->second.ChangePolicy != ChangePolicy_)
        {
            throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::RegistDerivedPropertyByName {}.{} conflicts", strComponentClass_, strPropertyName_));
        }
        return;
    }
    _Ite->second.Name = strPropertyName_;
    _Ite->second.Kind = EPropertyKind::Derived;
    _Ite->second.Persistence = Persistence_;
    _Ite->second.ChangePolicy = ChangePolicy_;
    _Ite->second.Evaluator = Evaluator_;
}

//! 是否包含属性
bool iCAX::Database::CMetaRegistry::HasPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    uint32_t _nProperty = FNV1a32(strPropertyName_.c_str());

    return FindPropertyMeta(_nClass, _nProperty) != nullptr;
}

//! 获取属性类型
iCAX::Database::EPropertyKind iCAX::Database::CMetaRegistry::GetPropertyKindByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    uint32_t _nProperty = FNV1a32(strPropertyName_.c_str());

    if (auto _pProperty = FindPropertyMeta(_nClass, _nProperty))
    {
        return _pProperty->Kind;
    }
    throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::GetPropertyKindByName {} has no property {}", strComponentClass_, strPropertyName_));
}

//! 获取属性持久化语义
iCAX::Database::EPropertyPersistence iCAX::Database::CMetaRegistry::GetPropertyPersistenceByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    uint32_t _nProperty = FNV1a32(strPropertyName_.c_str());

    if (auto _pProperty = FindPropertyMeta(_nClass, _nProperty))
    {
        return _pProperty->Persistence;
    }
    throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::GetPropertyPersistenceByName {} has no property {}", strComponentClass_, strPropertyName_));
}

//! 获取属性修改传播策略
iCAX::Database::EPropertyChangePolicy iCAX::Database::CMetaRegistry::GetPropertyChangePolicyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    uint32_t _nProperty = FNV1a32(strPropertyName_.c_str());

    if (auto _pProperty = FindPropertyMeta(_nClass, _nProperty))
    {
        return _pProperty->ChangePolicy;
    }
    throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::GetPropertyChangePolicyByName {} has no property {}", strComponentClass_, strPropertyName_));
}

//! 是否为派生属性
bool iCAX::Database::CMetaRegistry::IsDerivedPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    uint32_t _nProperty = FNV1a32(strPropertyName_.c_str());

    return IsDerivedProperty(_nClass, _nProperty);
}

//! 获取属性名称列表
std::vector<std::string> iCAX::Database::CMetaRegistry::GetPropertyNames(IN const std::string& strComponentClass_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    return GetPropertyNames(_nClass);
}

//! 调用属性获取
PropertyValue iCAX::Database::CMetaRegistry::InvokeGetter(IN const CComponentBase& Component_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    uint32_t _nProperty = FNV1a32(strPropertyName_.c_str());

    return InvokeGetter(Component_, _nClass, _nProperty);
}

//! 调用属性设置
void iCAX::Database::CMetaRegistry::InvokeSetter(IN CComponentBase& Component_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_) const
{
    uint32_t _nClass = FNV1a32(strComponentClass_.c_str());
    uint32_t _nProperty = FNV1a32(strPropertyName_.c_str());

    InvokeSetter(Component_, _nClass, _nProperty, NewValue_);
}

//! 绑定特性
void iCAX::Database::CMetaRegistry::RegistAttributeByName(IN std::shared_ptr<IAttribute> pAttribute_, IN const std::string& strComponentClass_)
{
    auto _pMate = GetMeta(FNV1a32(strComponentClass_.c_str()));
    if (_pMate != nullptr)
    {
        _pMate->vecAttributes.emplace(pAttribute_);
    }
}

//! 获取属性列表
std::unordered_set<std::shared_ptr<iCAX::Database::IAttribute>> iCAX::Database::CMetaRegistry::GetAttributesByName(IN const std::string& strComponentClass_) const
{
    return GetAttributesByName(FNV1a32(strComponentClass_.c_str()));
}

//! 注册约束器
void iCAX::Database::CMetaRegistry::RegistCheckerByName(IN const std::shared_ptr<IChecker>& pChecker_, IN const std::string& strComponentClass_)
{
    auto _pMate = GetMeta(FNV1a32(strComponentClass_.c_str()));
    if (_pMate != nullptr)
    {
        _pMate->vecCheckers.emplace(pChecker_);
    }
}

//! 获取约束列表
std::unordered_set<std::shared_ptr<iCAX::Database::IChecker>> iCAX::Database::CMetaRegistry::GetCheckersByName(IN const std::string& strComponentClass_) const
{
    return GetCheckersByName(FNV1a32(strComponentClass_.c_str()));
}

//! 是否是继承关系
bool iCAX::Database::CMetaRegistry::IsInheritance(IN const uint32_t& nComponentClass_, IN const uint32_t& nParentComponentClass_)
{
    auto _Ite = m_mapType.find(nComponentClass_);
    if (_Ite == m_mapType.end())
    {
        return false;
    }
    if (_Ite->second == nParentComponentClass_)
    {
        return true;
    }
    return IsInheritance(_Ite->second, nParentComponentClass_);
}

//! 获取属性列表
std::vector<std::string> iCAX::Database::CMetaRegistry::GetPropertyNames(IN const uint32_t& nComponentClass_) const
{
    std::vector<std::string> result;
    std::unordered_set<std::string> existed;

    // 先加入自己
    if (auto meta = GetMeta(nComponentClass_))
    {
        for (const auto& _PropertyKey : meta->vecPropertyOrder)
        {
            auto _Ite = meta->mapProperties.find(_PropertyKey);
            if (_Ite == meta->mapProperties.end())
            {
                continue;
            }
            if (existed.insert(_Ite->second.Name).second)
            {
                result.push_back(_Ite->second.Name);
            }
        }
    }

    // 再递归父类（保证顺序）
    auto itType = m_mapType.find(nComponentClass_);
    if (itType != m_mapType.end())
    {
        auto parentProps = GetPropertyNames(itType->second);
        for (auto& name : parentProps)
        {
            if (existed.insert(name).second)
                result.push_back(name);
        }
    }

    return result;
}

//! 获取属性值
PropertyValue iCAX::Database::CMetaRegistry::InvokeGetter(IN const CComponentBase& Component_, IN const uint32_t& nComponentClass_, IN const uint32_t& nPropertyName_) const
{
    // 查当前类
    if (auto meta = GetMeta(nComponentClass_))
    {
        auto it = meta->mapProperties.find(nPropertyName_);
        if (it != meta->mapProperties.end())
        {
            if (it->second.Kind == EPropertyKind::Derived)
            {
                auto _pEntity = Component_.GetEntity();
                if (!_pEntity)
                {
                    throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::InvokeGetter {} has no entity", Component_.GetComponentClass()));
                }

                auto _pRepository = _pEntity->GetRepository();
                if (!_pRepository)
                {
                    throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::InvokeGetter {} has no repository", Component_.GetComponentClass()));
                }

                return _pRepository->EvaluateDerivedProperty(Component_, it->second.Name, it->second.Evaluator);
            }
            return it->second.Getter(&Component_);
        }
    }

    // 查父类
    auto itType = m_mapType.find(nComponentClass_);
    if (itType != m_mapType.end())
    {
        return InvokeGetter(Component_, itType->second, nPropertyName_);
    }

    throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::InvokeGetter {} has no property {}", Component_.GetComponentClass(), nPropertyName_));
}

//! 设置属性值
void iCAX::Database::CMetaRegistry::InvokeSetter(IN CComponentBase& Component_, IN const uint32_t& nComponentClass_, IN const uint32_t& nPropertyName_, IN const PropertyValue& NewValue_) const
{
    // 查当前类
    if (auto meta = GetMeta(nComponentClass_))
    {
        auto it = meta->mapProperties.find(nPropertyName_);
        if (it != meta->mapProperties.end())
        {
            if (it->second.Kind == EPropertyKind::Derived)
            {
                throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::InvokeSetter {}.{} is a derived property", Component_.GetComponentClass(), it->second.Name));
            }
            it->second.Setter(&Component_, NewValue_);
            return;
        }
    }

    // 查父类
    auto itType = m_mapType.find(nComponentClass_);
    if (itType != m_mapType.end())
    {
        return InvokeSetter(Component_, itType->second, nPropertyName_, NewValue_);
    }

    throw std::runtime_error(std::format("iCAX::Database::CMetaRegistry::InvokeSetter {} has no property {}", Component_.GetComponentClass(), nPropertyName_));
}

//! 是否为派生属性
bool iCAX::Database::CMetaRegistry::IsDerivedProperty(IN const uint32_t& nComponentClass_, IN const uint32_t& nPropertyName_) const
{
    if (auto _pProperty = FindPropertyMeta(nComponentClass_, nPropertyName_))
    {
        return _pProperty->Kind == EPropertyKind::Derived;
    }

    return false;
}

//! 查找属性元数据
const iCAX::Database::CMetaRegistry::PropertyMeta* iCAX::Database::CMetaRegistry::FindPropertyMeta(IN const uint32_t& nComponentClass_, IN const uint32_t& nPropertyName_) const
{
    if (auto meta = GetMeta(nComponentClass_))
    {
        auto it = meta->mapProperties.find(nPropertyName_);
        if (it != meta->mapProperties.end())
        {
            return &it->second;
        }
    }

    auto itType = m_mapType.find(nComponentClass_);
    if (itType != m_mapType.end())
    {
        return FindPropertyMeta(itType->second, nPropertyName_);
    }

    return nullptr;
}

//! 获取特性，子类优先
std::unordered_set<std::shared_ptr<iCAX::Database::IAttribute>> iCAX::Database::CMetaRegistry::GetAttributesByName(IN const uint32_t& nComponentClass_) const
{
    std::unordered_set<std::shared_ptr<IAttribute>> result;
    std::unordered_set<std::type_index> existed;

    // 先加入自己（子类优先）
    if (auto meta = GetMeta(nComponentClass_))
    {
        for (const auto& _Item : meta->vecAttributes)
        {
            if (!_Item)
            {
                continue;
            }
            if (existed.insert(std::type_index(typeid(*_Item))).second)
                result.emplace(_Item);
        }
    }

    // 再递归父类
    auto itType = m_mapType.find(nComponentClass_);
    if (itType != m_mapType.end())
    {
        auto parentAttributes = GetAttributesByName(itType->second);
        for (auto _Item : parentAttributes)
        {
            if (!_Item)
            {
                continue;
            }
            if (existed.insert(std::type_index(typeid(*_Item))).second)
                result.emplace(_Item);
        }
    }

    const uint32_t _nGlobalClass = FNV1a32("");
    if (nComponentClass_ != _nGlobalClass)
    {
        if (auto meta = GetMeta(_nGlobalClass))
        {
            for (const auto& _Item : meta->vecAttributes)
            {
                if (!_Item)
                {
                    continue;
                }
                if (existed.insert(std::type_index(typeid(*_Item))).second)
                    result.emplace(_Item);
            }
        }
    }

    return result;
}

//! 获取约束列表
std::unordered_set<std::shared_ptr<iCAX::Database::IChecker>> iCAX::Database::CMetaRegistry::GetCheckersByName(IN const uint32_t& nComponentClass_) const
{
    std::unordered_set<std::shared_ptr<IChecker>> result;
    std::unordered_set<std::type_index> existed;

    // 先加入自己（子类优先）
    if (auto meta = GetMeta(nComponentClass_))
    {
        for (const auto& _Item : meta->vecCheckers)
        {
            if (!_Item)
            {
                continue;
            }
            if (existed.insert(std::type_index(typeid(*_Item))).second)
                result.emplace(_Item);
        }
    }

    // 再递归父类
    auto itType = m_mapType.find(nComponentClass_);
    if (itType != m_mapType.end())
    {
        auto parentCheckers = GetCheckersByName(itType->second);
        for (auto _Item : parentCheckers)
        {
            if (!_Item)
            {
                continue;
            }
            if (existed.insert(std::type_index(typeid(*_Item))).second)
                result.emplace(_Item);
        }
    }

    const uint32_t _nGlobalClass = FNV1a32("");
    if (nComponentClass_ != _nGlobalClass)
    {
        if (auto meta = GetMeta(_nGlobalClass))
        {
            for (const auto& _Item : meta->vecCheckers)
            {
                if (!_Item)
                {
                    continue;
                }
                if (existed.insert(std::type_index(typeid(*_Item))).second)
                    result.emplace(_Item);
            }
        }
    }

    return result;
}

//! 获取元数据
std::shared_ptr<iCAX::Database::CMetaRegistry::ComponentMeta> iCAX::Database::CMetaRegistry::GetMeta(IN const uint32_t& nComponentClass_)
{
    auto _Ite = m_mapMeta.find(nComponentClass_);
    if (_Ite == m_mapMeta.end())
    {
        _Ite = m_mapMeta.insert(std::make_pair(nComponentClass_, std::make_shared<ComponentMeta>())).first;
    }
    return _Ite->second;
  
}

//! 获取元数据
std::shared_ptr<iCAX::Database::CMetaRegistry::ComponentMeta> iCAX::Database::CMetaRegistry::GetMeta(IN const uint32_t& nComponentClass_) const
{
    auto _Ite = m_mapMeta.find(nComponentClass_);
    if (_Ite == m_mapMeta.end())
    {
        return nullptr;
    }
    return _Ite->second;
}
