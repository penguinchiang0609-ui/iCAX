#include "pch.h"
#include "MailChannelRegistry.h"

#include <stdexcept>

bool iCAX::Mail::CMailChannelRegistry::CreateChannel(IN const iCAX::Data::uuid& ChannelID_)
{
    return CreateChannel(ChannelID_, CMailChannelCreateInfo{});
}

bool iCAX::Mail::CMailChannelRegistry::CreateChannel(
    IN const iCAX::Data::uuid& ChannelID_,
    IN const CMailChannelCreateInfo& CreateInfo_)
{
    ValidateChannelID(ChannelID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto [_Iter, _Inserted] = m_Channels.emplace(
        ChannelID_,
        std::make_unique<CMailChannel>(CreateInfo_));
    return _Inserted;
}

bool iCAX::Mail::CMailChannelRegistry::HasChannel(IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Channels.find(ChannelID_) != m_Channels.end();
}

iCAX::Mail::CMailPostOffice iCAX::Mail::CMailChannelRegistry::GetFrontendPostOffice(
    IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Channels.find(ChannelID_);
    if (_Iter == m_Channels.end())
    {
        throw std::logic_error("Mail channel does not exist");
    }
    return _Iter->second->GetEndAPostOffice();
}

iCAX::Mail::CMailPostOffice iCAX::Mail::CMailChannelRegistry::GetBackendPostOffice(
    IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Channels.find(ChannelID_);
    if (_Iter == m_Channels.end())
    {
        throw std::logic_error("Mail channel does not exist");
    }
    return _Iter->second->GetEndBPostOffice();
}

bool iCAX::Mail::CMailChannelRegistry::RemoveChannel(IN const iCAX::Data::uuid& ChannelID_)
{
    ValidateChannelID(ChannelID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Channels.erase(ChannelID_) > 0;
}

void iCAX::Mail::CMailChannelRegistry::ClearChannels()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Channels.clear();
}

void iCAX::Mail::CMailChannelRegistry::ValidateChannelID(IN const iCAX::Data::uuid& ChannelID_)
{
    if (ChannelID_.is_nil())
    {
        throw std::invalid_argument("Mail channel id cannot be nil");
    }
}
