#include "pch.h"
#include "MailChannel.h"

//! 构造指定容量的双向邮件通道
iCAX::Mail::CMailChannel::CMailChannel(IN const CMailChannelCreateInfo& CreateInfo_)
    : m_CreateInfo(CreateInfo_)
    , m_AToB(std::make_shared<CMailQueue>(m_CreateInfo.EndAToEndBQueue))
    , m_BToA(std::make_shared<CMailQueue>(m_CreateInfo.EndBToEndAQueue))
{
}

//! 获取指定端点视角的邮局
iCAX::Mail::CMailPostOffice iCAX::Mail::CMailChannel::GetPostOffice(IN MailChannelEnd End_) noexcept
{
    if (End_ == kMailEndA)
    {
        return GetEndAPostOffice();
    }
    return GetEndBPostOffice();
}

//! 获取 EndA 视角的邮局
iCAX::Mail::CMailPostOffice iCAX::Mail::CMailChannel::GetEndAPostOffice() noexcept
{
    return CMailPostOffice(m_BToA, m_AToB);
}

//! 获取 EndB 视角的邮局
iCAX::Mail::CMailPostOffice iCAX::Mail::CMailChannel::GetEndBPostOffice() noexcept
{
    return CMailPostOffice(m_AToB, m_BToA);
}

//! 清空两个方向上的待处理邮件
void iCAX::Mail::CMailChannel::Clear()
{
    m_AToB->Clear();
    m_BToA->Clear();
}

//! 重置通道
void iCAX::Mail::CMailChannel::Reset()
{
    m_AToB->Clear();
    m_BToA->Clear();
    m_AToB = std::make_shared<CMailQueue>(m_CreateInfo.EndAToEndBQueue);
    m_BToA = std::make_shared<CMailQueue>(m_CreateInfo.EndBToEndAQueue);
}

//! 获取 EndA 到 EndB 的底层队列
iCAX::Mail::CMailQueue& iCAX::Mail::CMailChannel::GetAToBQueue() noexcept
{
    return *m_AToB;
}

//! 获取 EndB 到 EndA 的底层队列
iCAX::Mail::CMailQueue& iCAX::Mail::CMailChannel::GetBToAQueue() noexcept
{
    return *m_BToA;
}
