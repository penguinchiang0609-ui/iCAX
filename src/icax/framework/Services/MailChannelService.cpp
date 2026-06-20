#include "pch.h"
#include "MailChannelService.h"

#include <stdexcept>

void iCAX::Services::CMailChannelService::OnLoad()
{
}

void iCAX::Services::CMailChannelService::OnUnload()
{
    ClearChannels();
}

bool iCAX::Services::CMailChannelService::CreateChannel(IN const iCAX::Data::uuid& ChannelID_)
{
    ValidateChannelID(ChannelID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto [_Iter, _Inserted] = m_Channels.emplace(
        ChannelID_,
        std::make_unique<iCAX::Mail::CMailChannel>());
    return _Inserted;
}

bool iCAX::Services::CMailChannelService::HasChannel(IN const iCAX::Data::uuid& ChannelID_) const
{
    ValidateChannelID(ChannelID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Channels.find(ChannelID_) != m_Channels.end();
}

iCAX::Mail::CMailPostOffice iCAX::Services::CMailChannelService::GetFrontendPostOffice(
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

iCAX::Mail::CMailPostOffice iCAX::Services::CMailChannelService::GetBackendPostOffice(
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

bool iCAX::Services::CMailChannelService::RemoveChannel(IN const iCAX::Data::uuid& ChannelID_)
{
    ValidateChannelID(ChannelID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Channels.erase(ChannelID_) > 0;
}

void iCAX::Services::CMailChannelService::ClearChannels()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Channels.clear();
}

void iCAX::Services::CMailChannelService::ValidateChannelID(IN const iCAX::Data::uuid& ChannelID_)
{
    if (ChannelID_.is_nil())
    {
        throw std::invalid_argument("Mail channel id cannot be nil");
    }
}
