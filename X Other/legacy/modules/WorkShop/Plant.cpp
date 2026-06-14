#include "pch.h"
#include "Plant.h"

//!< 构造函数
iCAX::WorkShop::CPlant::CPlant()
{
    m_pTradeRegistry = std::make_shared<CTradeRegistry>();
    m_pGraphRegistry = std::make_shared<CWorkFlowRegistry>();
    m_MainThreadID = std::this_thread::get_id();
}

//!< 析构函数
iCAX::WorkShop::CPlant::~CPlant()
{
}

//!< 获取工种登记表
std::shared_ptr<iCAX::WorkShop::CTradeRegistry> iCAX::WorkShop::CPlant::GetTradeRegistry()
{
    return m_pTradeRegistry;
}

//!< 获取图纸登记表
std::shared_ptr<iCAX::WorkShop::CWorkFlowRegistry> iCAX::WorkShop::CPlant::GetGraphRegistry()
{
    return m_pGraphRegistry;
}

//!< 调度工单
void iCAX::WorkShop::CPlant::DispatchJobTicket(IN std::shared_ptr<CJobTicket> pTicket_, std::function<void(std::shared_ptr<CJobTicket>)> funOnSuccess_, std::function<void(std::shared_ptr<CJobTicket>)> funOnFault_)
{
    if (std::this_thread::get_id() != m_MainThreadID)
    {
        throw "can be call by other thread, just main thread";
    }
    auto _Tracer = std::make_shared<CJobTicketTracker>(m_pTradeRegistry, m_pGraphRegistry, pTicket_, funOnSuccess_, funOnFault_);
    m_mapTracker.insert({ pTicket_ ->GetID(), _Tracer});
}

//!< 循环检测
void iCAX::WorkShop::CPlant::LoopUpdate()
{
    for (auto _Ite = m_mapTracker.begin(); _Ite != m_mapTracker.end();)
    {
        _Ite->second->LoopUpdate();
        if (_Ite->second->GetTicket()->GetStatics()->IsCompleted())//!< 如果已经完成了，则此处移除跟单员
        {
            _Ite = m_mapTracker.erase(_Ite);
        }
        else
        {
            _Ite++;
        }
    }
}
