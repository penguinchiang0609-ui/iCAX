#include "pch.h"
#include "UniverseContext.h"
#include "Timer.h"

//! 构造函数
iCAX::Behaviour::CUniverseContext::CUniverseContext(IN const std::shared_ptr<iCAX::Database::IRepository>& pRepository_,
    IN const std::shared_ptr<iCAX::Data::PropertyBag>& pApplicationSetting_)
    : m_pRepository(pRepository_)
    , m_pApplicationSetting(pApplicationSetting_)
{
    m_pTimer = std::make_shared<CTimer>();
}

//! 析构函数
iCAX::Behaviour::CUniverseContext::~CUniverseContext()
{
    for (auto& [_Key, _pData] : m_mapContext)
    {
        delete[] _pData;
    }
    m_mapContext.clear();
}

//! 获取应用程序设置
iCAX::Data::PropertyBag& iCAX::Behaviour::CUniverseContext::GetApplicationSettings() const
{
    return *m_pApplicationSetting;
}

//! 获取数据仓储
iCAX::Database::IRepository& iCAX::Behaviour::CUniverseContext::GetDatabase() const
{
    return *m_pRepository;
}

//! 获取计时器
iCAX::Behaviour::ITimer& iCAX::Behaviour::CUniverseContext::GetTimer() const
{
    return *m_pTimer;
}

//! 设置数据
void iCAX::Behaviour::CUniverseContext::SetData(IN const uint32_t& nKey_, IN uint8_t* pData_)
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
uint8_t* iCAX::Behaviour::CUniverseContext::GetData(IN const uint32_t& nKey_)
{
    if (m_mapContext.contains(nKey_))
    {
        return  m_mapContext[nKey_];
    }
    return nullptr;
}

//! 移除
void iCAX::Behaviour::CUniverseContext::Remove(IN const uint32_t& nKey_)
{
    if (m_mapContext.contains(nKey_))
    {
        auto _p = m_mapContext[nKey_];
        m_mapContext.erase(nKey_);
        delete[] _p;
    }
}
