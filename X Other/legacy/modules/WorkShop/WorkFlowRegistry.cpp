#include "pch.h"
#include "WorkFlowRegistry.h"

//!< 构造函数
iCAX::WorkShop::CWorkFlowRegistry::CWorkFlowRegistry()
{
    m_MainThreadID = std::this_thread::get_id();
}

//!< 析构函数
iCAX::WorkShop::CWorkFlowRegistry::~CWorkFlowRegistry()
{
}

//!< 注册工序图
bool iCAX::WorkShop::CWorkFlowRegistry::Regist(IN std::shared_ptr<CWorkFlowGraph>& pGraph_)
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }
    if (m_mapRoutePtr.find(pGraph_->GetID())!= m_mapRoutePtr.end())
    {
        return false;//!< 不可重复注册
    }
    m_mapRoutePtr.insert({ pGraph_->GetID() , pGraph_ });
    return true;
}

//!< 注销工序图
bool iCAX::WorkShop::CWorkFlowRegistry::Deregist(IN std::shared_ptr<CWorkFlowGraph>& pGraph_)
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }
    auto _Ite = m_mapRoutePtr.find(pGraph_->GetID());
    if (_Ite == m_mapRoutePtr.end())
    {
        return false;//!< 已经擦除
    }
    m_mapRoutePtr.erase(_Ite);
    return true;
}

//!< 获取工序图
std::shared_ptr<iCAX::WorkShop::CWorkFlowGraph> iCAX::WorkShop::CWorkFlowRegistry::GetGraph(IN const iCAX::Data::uuid& UID_) const
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }
    auto _Ite = m_mapRoutePtr.find(UID_);
    if (_Ite == m_mapRoutePtr.end())
    {
        return nullptr;
    }

    return _Ite->second;
}
