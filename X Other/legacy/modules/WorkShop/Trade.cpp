#include "pch.h"
#include "Trade.h"
#include <memory>
#include "Worker.h"

//!< 初始化
bool iCAX::WorkShop::Trade::Initialize(IN const iCAX::Data::uuid& UID_, IN const int nCount_, IN std::function<std::shared_ptr<CWorkerBase>()> pfunCreator_)
{
    m_ID = UID_;
    for (int i = 0; i < nCount_; i++)
    {
        auto _pWorker = pfunCreator_();
        if (!_pWorker->Initialize())
        {
            return false;
        }

        if (!ReturnWorker(_pWorker))
        {
            return false;
        }
    }
}

//!< 释放
void iCAX::WorkShop::Trade::Dispose()
{
}

//!< 尝试获取工人
bool iCAX::WorkShop::Trade::TryFetchWorker(OUT std::shared_ptr<CWorkerBase>& pWorker_)
{
    if (!m_queueWorkerIdle.empty())
    {
        pWorker_ = m_queueWorkerIdle.front();
        return true;
    }
    return false;

}

//!< 归还工人
bool iCAX::WorkShop::Trade::ReturnWorker(IN std::shared_ptr<CWorkerBase>& pWorker_)
{
    if (pWorker_->GetTradeID() != m_ID)
    {
        return false;
    }

    m_queueWorkerIdle.push(pWorker_);
    return true;
}

//!< 获取ID
iCAX::Data::uuid iCAX::WorkShop::Trade::GetID() const
{
    return m_ID;
}
