#include "pch.h"
#include "WorldContext.h"

//! 构造函数
iCAX::Behaviour::CWorldContext::CWorldContext()
    : m_mapContext()
{

}

//! 析构函数
iCAX::Behaviour::CWorldContext::~CWorldContext()
{
    for (auto _Ite = m_mapContext.begin(); _Ite != m_mapContext.end(); _Ite++)
    {
        delete [] _Ite->second;
    }
}

//! 设置数据
void iCAX::Behaviour::CWorldContext::SetData(IN const uint32_t& nKey_, IN uint8_t* pData_)
{
    if (m_mapContext.contains(nKey_))
    {
        if (m_mapContext[nKey_] != pData_)
        {
            Remove(nKey_);
        }
    }
    m_mapContext[nKey_] = pData_;
}

//! 获取数据
uint8_t* iCAX::Behaviour::CWorldContext::GetData(IN const uint32_t& nKey_)
{
    if (m_mapContext.contains(nKey_))
    {
        return  m_mapContext[nKey_];
    }
    return nullptr;
}

//! 移除
void iCAX::Behaviour::CWorldContext::Remove(IN const uint32_t& nKey_)
{
    if (m_mapContext.contains(nKey_))
    {
        auto _p = m_mapContext[nKey_];
        m_mapContext.erase(nKey_);
        delete[] _p;
    }
}




