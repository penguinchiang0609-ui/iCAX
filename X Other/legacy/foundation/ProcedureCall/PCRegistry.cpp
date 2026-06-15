#include "pch.h"
#include "PCRegistry.h"
#include <stdexcept>
#include <format>


//! 构造函数
iCAX::PC::CPCRegistry::CPCRegistry()
    : m_mapEntries()
{

}

//! 析构函数
iCAX::PC::CPCRegistry::~CPCRegistry()
{
    m_mapEntries.clear();
}

//! 注册
bool iCAX::PC::CPCRegistry::Regist(IN const PCID& PCID_, IN PCFunc Fn_)
{
    if (m_mapEntries.contains(PCID_))
    {
        return false;
    }
    m_mapEntries[PCID_] = Fn_;
    return true;
}


//! 查询过程调用入口
iCAX::PC::PCFunc iCAX::PC::CPCRegistry::Find(IN const PCID& PCID_) const
{
    auto _Ite = m_mapEntries.find(PCID_);
    if (_Ite == m_mapEntries.end())
    {
        throw std::out_of_range(std::format("PCEntry not found {}", PCID_));
    }

    return _Ite->second;
}
