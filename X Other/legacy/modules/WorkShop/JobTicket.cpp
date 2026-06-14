#include "pch.h"
#include "JobTicket.h"

using namespace iCAX::WorkShop;


std::atomic<unsigned long long> g_nIndex = 0;


//!< 构造函数
iCAX::WorkShop::CJobTicket::CJobTicket(IN const CWorkFlowGraph& Route_, IN std::shared_ptr<WorkPiece> pPicece_)
{
    m_ID = g_nIndex.fetch_add(1, std::memory_order_acq_rel);
    m_pWorkPiece = pPicece_;
    m_pStatics = std::make_shared<CProcessingStatics>();
    m_pStatics->Initialize(Route_);
}

//!< 析构函数
iCAX::WorkShop::CJobTicket::~CJobTicket()
{
}

unsigned long long iCAX::WorkShop::CJobTicket::GetID() const
{
    return 0;
}


//!< 获取工件
std::shared_ptr<WorkPiece>iCAX::WorkShop::CJobTicket::GetWorkPiece() const
{
    return m_pWorkPiece;
}

//!< 获取加工统计
std::shared_ptr<CProcessingStatics> iCAX::WorkShop::CJobTicket::GetStatics() const
{
    return m_pStatics;
}

