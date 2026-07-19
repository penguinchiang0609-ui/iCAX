#include "pch.h"
#include "FacadeChannel.h"

iCAX::Interaction::CFacadeChannel::CFacadeChannel()
    : CFacadeChannel(CFacadeChannelCreateInfo{})
{
}

iCAX::Interaction::CFacadeChannel::CFacadeChannel(IN const CFacadeChannelCreateInfo& CreateInfo_)
    : m_CreateInfo(CreateInfo_)
    , m_AToB(std::make_shared<CFacadeQueue>(m_CreateInfo.EndAToEndBQueue))
    , m_BToA(std::make_shared<CFacadeQueue>(m_CreateInfo.EndBToEndAQueue))
{
}

iCAX::Interaction::CFacadeEndpoint iCAX::Interaction::CFacadeChannel::GetEndAEndpoint() noexcept
{
    return CFacadeEndpoint(m_BToA, m_AToB);
}

iCAX::Interaction::CFacadeEndpoint iCAX::Interaction::CFacadeChannel::GetEndBEndpoint() noexcept
{
    return CFacadeEndpoint(m_AToB, m_BToA);
}

void iCAX::Interaction::CFacadeChannel::Clear()
{
    m_AToB->Clear();
    m_BToA->Clear();
}

void iCAX::Interaction::CFacadeChannel::Reset()
{
    m_AToB->Clear();
    m_BToA->Clear();
    m_AToB = std::make_shared<CFacadeQueue>(m_CreateInfo.EndAToEndBQueue);
    m_BToA = std::make_shared<CFacadeQueue>(m_CreateInfo.EndBToEndAQueue);
}

iCAX::Interaction::CFacadeQueue& iCAX::Interaction::CFacadeChannel::GetAToBQueue() noexcept
{
    return *m_AToB;
}

iCAX::Interaction::CFacadeQueue& iCAX::Interaction::CFacadeChannel::GetBToAQueue() noexcept
{
    return *m_BToA;
}
