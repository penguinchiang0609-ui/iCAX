#include "pch.h"
#include "ComponentMask.h"
#include "MaskRegistry.h"

//!< 设置
void iCAX::Database::CComponentMask::Set(IN const std::string& strClassName_)
{
    m_Bits.set(CMaskRegistry::GetComponentIndex(strClassName_));
}

//!< 取消
void iCAX::Database::CComponentMask::Reset(IN const std::string& strClassName_)
{
    m_Bits.reset(CMaskRegistry::GetComponentIndex(strClassName_));
}

//!< 测试
bool iCAX::Database::CComponentMask::Test(IN const std::string& strClassName_) const
{
    return m_Bits.test(CMaskRegistry::GetComponentIndex(strClassName_));
}

//!< 匹配
bool iCAX::Database::CComponentMask::Matches(IN const CComponentMask& Required_) const
{
    return (m_Bits & Required_.m_Bits) == Required_.m_Bits;
}

//!< 获取名称列表
std::vector<std::string> iCAX::Database::CComponentMask::GetClassNames() const
{
    std::vector<std::string> names;
    for (size_t i = 0; i < m_Bits.size(); ++i) {
        if (m_Bits.test(i)) 
        {
            names.push_back(CMaskRegistry::GetComponentClass(i));
        }
    }
    return names;
}

//!< ==运算符
bool iCAX::Database::CComponentMask::operator==(const CComponentMask& Right_) const
{
    return m_Bits == Right_.m_Bits;
}

//!< !=运算符
bool iCAX::Database::CComponentMask::operator!=(const CComponentMask& Right_) const
{
    return !(*this == Right_);
}

//!< <运算符
bool iCAX::Database::CComponentMask::operator<(const CComponentMask& Right_) const
{
    for (size_t i = m_Bits.size(); i-- > 0;) {
        bool lhs = m_Bits.test(i);
        bool rhs = Right_.m_Bits.test(i);
        if (lhs != rhs)
            return lhs < rhs;
    }
    return false; // equal
}
