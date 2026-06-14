#include "pch.h"
#include "TradeRegistry.h"

//!< 构造函数
iCAX::WorkShop::CTradeRegistry::CTradeRegistry()
    : m_mapTrade()
{
    m_MainThreadID = std::this_thread::get_id();
}

//!< 析构函数
iCAX::WorkShop::CTradeRegistry::~CTradeRegistry()
{
}

//!< 注册工种
bool iCAX::WorkShop::CTradeRegistry::Regist(IN const iCAX::Data::uuid& UID_, IN const int nCount_, IN std::function<std::shared_ptr<CWorkerBase>()> pfunCreator_)
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }
    auto _Ite = m_mapTrade.find(UID_);
    if (_Ite != m_mapTrade.end())
    {
        return false;//!< 不可重复注册
    }

    auto _pTrade = std::make_shared<Trade>();
    if (!_pTrade->Initialize(UID_, nCount_, pfunCreator_))
    {
        return false;
    }
    m_mapTrade.insert({ UID_ , _pTrade });
    return true;
}

//!< 注销
bool iCAX::WorkShop::CTradeRegistry::Deregist(IN const iCAX::Data::uuid& UID_)
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }

    auto _Ite = m_mapTrade.find(UID_);
    if (_Ite == m_mapTrade.end())
    {
        return false;//!< 不可重复注销
    }

    try
    {
        _Ite->second->Dispose();
    }
    catch (...)
    {
    }
    m_mapTrade.erase(_Ite);
}

//!< 是否包含指定工种
bool iCAX::WorkShop::CTradeRegistry::Has(IN const iCAX::Data::uuid& UID_) const
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }
    return m_mapTrade.find(UID_) != m_mapTrade.end();
}


//!< 获取工人
bool iCAX::WorkShop::CTradeRegistry::TryFetchWorker(IN const iCAX::Data::uuid& UID_, OUT std::shared_ptr<CWorkerBase>& pWorker_) const
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }
    auto _Ite = m_mapTrade.find(UID_);
    if (_Ite == m_mapTrade.end())
    {
        return false;
    }
    return _Ite->second->TryFetchWorker(pWorker_);
}

//!< 归还工人
bool iCAX::WorkShop::CTradeRegistry::ReturnWorker(IN std::shared_ptr<CWorkerBase>& pWorker_) const
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }
    auto _Ite = m_mapTrade.find(pWorker_->GetTradeID());
    if (_Ite == m_mapTrade.end())
    {
        return false;
    }

    return _Ite->second->ReturnWorker(pWorker_);
}

//!< 获取工种索引列表
std::vector<iCAX::Data::uuid> iCAX::WorkShop::CTradeRegistry::GetTradeIndexes() const
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }
    std::vector<iCAX::Data::uuid> _Indexes;
    for (auto& [_nIndex, _Trade] : m_mapTrade)
    {
        _Indexes.push_back(_nIndex);
    }

    return _Indexes;
}

