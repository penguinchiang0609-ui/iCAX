#include "pch.h"
#include "FacadeChannelRegistry.h"

bool iCAX::Interaction::CFacadeChannelRegistry::CreateChannel(IN const iCAX::Data::uuid& ChannelID_)
{
    return CreateChannel(ChannelID_, CFacadeChannelCreateInfo{});
}

bool iCAX::Interaction::CFacadeChannelRegistry::CreateChannel(
    IN const iCAX::Data::uuid& ChannelID_,
    IN const CFacadeChannelCreateInfo& CreateInfo_)
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto [_Iter, _Inserted] = m_Channels.emplace(
        ChannelID_,
        std::make_unique<CFacadeChannel>(CreateInfo_));
    return _Inserted;
}

bool iCAX::Interaction::CFacadeChannelRegistry::HasChannel(IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Channels.find(ChannelID_) != m_Channels.end();
}

iCAX::Interaction::CFacadeEndpoint iCAX::Interaction::CFacadeChannelRegistry::GetFrontendEndpoint(
    IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Channels.find(ChannelID_);
    if (_Iter == m_Channels.end())
    {
        throw std::logic_error("Facade channel does not exist");
    }
    return _Iter->second->GetEndAEndpoint();
}

iCAX::Interaction::CFacadeEndpoint iCAX::Interaction::CFacadeChannelRegistry::GetBackendEndpoint(
    IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Channels.find(ChannelID_);
    if (_Iter == m_Channels.end())
    {
        throw std::logic_error("Facade channel does not exist");
    }
    return _Iter->second->GetEndBEndpoint();
}

bool iCAX::Interaction::CFacadeChannelRegistry::RemoveChannel(IN const iCAX::Data::uuid& ChannelID_)
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Channels.erase(ChannelID_) > 0;
}

void iCAX::Interaction::CFacadeChannelRegistry::ClearChannels()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Channels.clear();
}

void iCAX::Interaction::CFacadeChannelRegistry::ValidateChannelID(IN const iCAX::Data::uuid& ChannelID_)
{
    if (ChannelID_.is_nil())
    {
        throw std::invalid_argument("Facade channel id cannot be nil");
    }
}
