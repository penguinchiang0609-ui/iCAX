#include "pch.h"
#include "Worker.h"

//!< 构造函数
iCAX::WorkShop::CWorkerBase::CWorkerBase(IN const iCAX::Data::uuid& ID_)
{
    m_ID = ID_;
}


//!< 析构函数
iCAX::WorkShop::CWorkerBase::~CWorkerBase()
{
}

//!< 获取工种ID
iCAX::Data::uuid iCAX::WorkShop::CWorkerBase::GetTradeID() const
{
    return m_ID;
}

//!< 初始化
bool iCAX::WorkShop::CWorkerBase::Initialize()
{
    return OnInitialize();
}

//!< 分配任务
void iCAX::WorkShop::CWorkerBase::Assign(IN std::shared_ptr<CJobTicket> pJob_)
{
    m_pJobTicket = pJob_;
    OnAssign();
}

//!< 移动下一帧
int iCAX::WorkShop::CWorkerBase::MoveNext()
{
    return OnMoveNext();
}

//!< 清理
void iCAX::WorkShop::CWorkerBase::Clear()
{
    OnClear();
    m_pJobTicket = nullptr;
}

//!< 释放
bool iCAX::WorkShop::CWorkerBase::Dispose()
{
    return OnDispose();
}

//!< 获取工单号
unsigned long long iCAX::WorkShop::CWorkerBase::GetJobIndex() const
{
    return m_pJobTicket == nullptr ? -1 : m_pJobTicket->GetID();
}
