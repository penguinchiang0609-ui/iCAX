#include "pch.h"
#include "CancellationToken.h"

//!< 构造函数
iCAX::Threading::CCancellationToken::CCancellationToken(IN std::shared_ptr<CCancellationTokenSource> pSource_)
    : m_pSource(pSource_)
{}

//!< 析构函数
iCAX::Threading::CCancellationToken::~CCancellationToken()
{
}

//!< 是否取消
bool iCAX::Threading::CCancellationToken::IsCanceled() const
{
    return m_pSource->IsCanceled();
}

//!< 获取空令牌
iCAX::Threading::CCancellationToken iCAX::Threading::CCancellationToken::None()
{
    return CCancellationToken(std::make_shared<CCancellationTokenSource>());
}
