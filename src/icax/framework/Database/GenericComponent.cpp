#include "pch.h"
#include "GenericComponent.h"

//!< 构造函数
iCAX::Database::CGenericComponent::CGenericComponent(IN std::shared_ptr<class IEntity> pEntity_, IN const std::string& strClassName_)
    : CComponentBase(pEntity_)
    , m_strClassName(strClassName_)
    , m_Properties()
{
}

//!< 析构函数
iCAX::Database::CGenericComponent::~CGenericComponent()
{
}

//!< 获取类型
std::string iCAX::Database::CGenericComponent::GetComponentClass() const
{
    return m_strClassName;
}

//!< 获取属性名称列表
std::vector<std::string> iCAX::Database::CGenericComponent::GetPropertyNameArray() const
{
    std::vector<std::string> _vecNames;
    for (auto & [_strName, _] : m_Properties)
    {
        _vecNames.push_back(_strName);
    }
    return _vecNames;
}

//!< 获取属性值
iCAX::Data::PropertyValue iCAX::Database::CGenericComponent::GetProperty(IN const std::string& strPropertyName_) const
{
    auto _Ite = m_Properties.find(strPropertyName_);
    return _Ite != m_Properties.end() ? _Ite->second : iCAX::Data::PropertyValue::Nil;
}

//!< 设置属性
void iCAX::Database::CGenericComponent::OnSetProperty(IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_)
{
    m_Properties[strPropertyName_] = NewValue_;
}

