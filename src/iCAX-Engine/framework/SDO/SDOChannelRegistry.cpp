#include "pch.h"
#include "SDOChannelRegistry.h"

bool iCAX::Interaction::CSDOChannelRegistry::CreateChannel(IN const iCAX::Data::uuid& ChannelID_)
{
    return CreateChannel(ChannelID_, CSDOChannelCreateInfo{});
}

bool iCAX::Interaction::CSDOChannelRegistry::CreateChannel(
    IN const iCAX::Data::uuid& ChannelID_,
    IN const CSDOChannelCreateInfo& CreateInfo_)
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto [_Iter, _Inserted] = m_Channels.emplace(
        ChannelID_,
        std::make_unique<CSDOChannel>(CreateInfo_));
    return _Inserted;
}

bool iCAX::Interaction::CSDOChannelRegistry::HasChannel(IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Channels.find(ChannelID_) != m_Channels.end();
}

iCAX::Interaction::CSDOEndpoint iCAX::Interaction::CSDOChannelRegistry::GetFrontendEndpoint(
    IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Channels.find(ChannelID_);
    if (_Iter == m_Channels.end())
    {
        throw std::logic_error("SDO channel does not exist");
    }
    return _Iter->second->GetEndAEndpoint();
}

iCAX::Interaction::CSDOEndpoint iCAX::Interaction::CSDOChannelRegistry::GetBackendEndpoint(
    IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Channels.find(ChannelID_);
    if (_Iter == m_Channels.end())
    {
        throw std::logic_error("SDO channel does not exist");
    }
    return _Iter->second->GetEndBEndpoint();
}

bool iCAX::Interaction::CSDOChannelRegistry::RemoveChannel(IN const iCAX::Data::uuid& ChannelID_)
{
    ValidateChannelID(ChannelID_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Channels.erase(ChannelID_) > 0;
}

void iCAX::Interaction::CSDOChannelRegistry::ClearChannels()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Channels.clear();
}

void iCAX::Interaction::CSDOChannelRegistry::ValidateChannelID(IN const iCAX::Data::uuid& ChannelID_)
{
    if (ChannelID_.is_nil())
    {
        throw std::invalid_argument("SDO channel id cannot be nil");
    }
}
