#pragma once

#include "Data/uuid.h"
#include "FacadeChannel.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace iCAX::Interaction
{
    /*
    * @brief 按运行范围管理 Facade 双向通道。
    */
    class _FACADES_EXP CFacadeChannelRegistry final
    {
    public:
        CFacadeChannelRegistry() = default;
        ~CFacadeChannelRegistry() = default;

        CFacadeChannelRegistry(IN const CFacadeChannelRegistry&) = delete;
        CFacadeChannelRegistry& operator=(IN const CFacadeChannelRegistry&) = delete;

        bool CreateChannel(IN const iCAX::Data::uuid& ChannelID_);
        bool CreateChannel(
            IN const iCAX::Data::uuid& ChannelID_,
            IN const CFacadeChannelCreateInfo& CreateInfo_);
        bool HasChannel(IN const iCAX::Data::uuid& ChannelID_) const;
        CFacadeEndpoint GetFrontendEndpoint(IN const iCAX::Data::uuid& ChannelID_) const;
        CFacadeEndpoint GetBackendEndpoint(IN const iCAX::Data::uuid& ChannelID_) const;
        bool RemoveChannel(IN const iCAX::Data::uuid& ChannelID_);
        void ClearChannels();

    private:
        static void ValidateChannelID(IN const iCAX::Data::uuid& ChannelID_);

    private:
        mutable std::mutex m_Mutex;
        std::unordered_map<iCAX::Data::uuid, std::unique_ptr<CFacadeChannel>> m_Channels;
    };
}
