#pragma once

#include "Data/uuid.h"
#include "SDOChannel.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace iCAX::Interaction
{
    /*
    * @brief 按运行范围管理 SDO 双向通道。
    */
    class _SDO_EXP CSDOChannelRegistry final
    {
    public:
        CSDOChannelRegistry() = default;
        ~CSDOChannelRegistry() = default;

        CSDOChannelRegistry(IN const CSDOChannelRegistry&) = delete;
        CSDOChannelRegistry& operator=(IN const CSDOChannelRegistry&) = delete;

        bool CreateChannel(IN const iCAX::Data::uuid& ChannelID_);
        bool CreateChannel(
            IN const iCAX::Data::uuid& ChannelID_,
            IN const CSDOChannelCreateInfo& CreateInfo_);
        bool HasChannel(IN const iCAX::Data::uuid& ChannelID_) const;
        CSDOEndpoint GetFrontendEndpoint(IN const iCAX::Data::uuid& ChannelID_) const;
        CSDOEndpoint GetBackendEndpoint(IN const iCAX::Data::uuid& ChannelID_) const;
        bool RemoveChannel(IN const iCAX::Data::uuid& ChannelID_);
        void ClearChannels();

    private:
        static void ValidateChannelID(IN const iCAX::Data::uuid& ChannelID_);

    private:
        mutable std::mutex m_Mutex;
        std::unordered_map<iCAX::Data::uuid, std::unique_ptr<CSDOChannel>> m_Channels;
    };
}
