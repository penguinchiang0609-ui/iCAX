#include "pch.h"
#include "PropertyBag.h"

//!< 构造函数
iCAX::Data::PropertyBag::PropertyBag()
    : m_Properties()
{
}

//!< 构造函数
iCAX::Data::PropertyBag::PropertyBag(IN const PropertySet& Properties_)
    : m_Properties(Properties_)
{
}


//!< 析构函数
iCAX::Data::PropertyBag::~PropertyBag()
{
}

//!< 设置
void iCAX::Data::PropertyBag::Set(const std::string& strkey_, const iCAX::Data::Variant& Varient_)
{
    m_Properties[strkey_] = Varient_;
}

//!< 获取设置项/值
iCAX::Data::Variant iCAX::Data::PropertyBag::Get(const std::string& strkey_, const iCAX::Data::Variant& Default_) const
{
    auto _Ite = m_Properties.find(strkey_);
    if (_Ite != m_Properties.end())
    {
        return _Ite->second;
    }

    return  Default_;
}

//!< 获取设置项/值
iCAX::Data::Variant iCAX::Data::PropertyBag::Get(const std::string& strkey_, const std::string& strPath_, const iCAX::Data::Variant& Default_) const
{
    auto _Ite = m_Properties.find(strkey_);
    if (_Ite != m_Properties.end())
    {
        auto _Vue = _Ite->second.GetByPath(strPath_);
        if (_Vue.has_value())
        {
            return *_Vue;
        }
    }

    return  Default_;
}

//!< 设置
void iCAX::Data::PropertyBag::Set(const std::string& strkey_, const std::string& strPath_, const iCAX::Data::Variant& Varient_)
{
    auto _Ite = m_Properties.find(strkey_);
    if (_Ite == m_Properties.end())
    {
        _Ite = m_Properties.insert(std::make_pair(strkey_, iCAX::Data::Variant())).first;
    }
    _Ite->second.SetByPath(strPath_, Varient_);
}

//!< 获取属性集
const iCAX::Data::PropertySet& iCAX::Data::PropertyBag::GetProperties() const
{
    return m_Properties;
}
