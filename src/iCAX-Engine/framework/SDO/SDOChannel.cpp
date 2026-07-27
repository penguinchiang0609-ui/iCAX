#include "pch.h"
#include "SDOChannel.h"

iCAX::Interaction::CSDOChannel::CSDOChannel()
    : CSDOChannel(CSDOChannelCreateInfo{})
{
}

iCAX::Interaction::CSDOChannel::CSDOChannel(IN const CSDOChannelCreateInfo& CreateInfo_)
    : m_CreateInfo(CreateInfo_)
    , m_AToB(std::make_shared<CSDOQueue>(m_CreateInfo.EndAToEndBQueue))
    , m_BToA(std::make_shared<CSDOQueue>(m_CreateInfo.EndBToEndAQueue))
{
}

iCAX::Interaction::CSDOEndpoint iCAX::Interaction::CSDOChannel::GetEndAEndpoint() noexcept
{
    return CSDOEndpoint(m_BToA, m_AToB);
}

iCAX::Interaction::CSDOEndpoint iCAX::Interaction::CSDOChannel::GetEndBEndpoint() noexcept
{
    return CSDOEndpoint(m_AToB, m_BToA);
}

void iCAX::Interaction::CSDOChannel::Clear()
{
    m_AToB->Clear();
    m_BToA->Clear();
}

void iCAX::Interaction::CSDOChannel::Reset()
{
    m_AToB->Clear();
    m_BToA->Clear();
    m_AToB = std::make_shared<CSDOQueue>(m_CreateInfo.EndAToEndBQueue);
    m_BToA = std::make_shared<CSDOQueue>(m_CreateInfo.EndBToEndAQueue);
}

iCAX::Interaction::CSDOQueue& iCAX::Interaction::CSDOChannel::GetAToBQueue() noexcept
{
    return *m_AToB;
}

iCAX::Interaction::CSDOQueue& iCAX::Interaction::CSDOChannel::GetBToAQueue() noexcept
{
    return *m_BToA;
}
