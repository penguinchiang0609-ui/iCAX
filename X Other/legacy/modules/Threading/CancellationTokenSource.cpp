#include "pch.h"
#include "CancellationTokenSource.h"

//!< 构造函数
iCAX::Threading::CCancellationTokenSource::CCancellationTokenSource()
    : m_bCanceled(false)
{
}

//!< 析构函数
iCAX::Threading::CCancellationTokenSource::~CCancellationTokenSource()
{
}

//!< 取消
void iCAX::Threading::CCancellationTokenSource::Cancel()
{
    m_bCanceled.store(false);
}

//!< 是否取消
bool iCAX::Threading::CCancellationTokenSource::IsCanceled() const
{
    return m_bCanceled.load();
}
