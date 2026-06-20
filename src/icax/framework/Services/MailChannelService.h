#pragma once

#include "IMailChannelService.h"
#include "Mailbox/MailChannel.h"
#include "ServicesHelper.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace iCAX
{
    namespace Services
    {
        class _SERVICE_EXP CMailChannelService final : public IMailChannelService
        {
            AUTO_REGIST_SERVICE(IMailChannelService, CMailChannelService)

        public:
            CMailChannelService() = default;
            ~CMailChannelService() override = default;

            CMailChannelService(IN const CMailChannelService&) = delete;
            CMailChannelService& operator=(IN const CMailChannelService&) = delete;

        public:
            void OnLoad() override;
            void OnUnload() override;

            bool CreateChannel(IN const iCAX::Data::uuid& ChannelID_) override;
            bool HasChannel(IN const iCAX::Data::uuid& ChannelID_) const override;
            iCAX::Mail::CMailPostOffice GetFrontendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const override;
            iCAX::Mail::CMailPostOffice GetBackendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const override;
            bool RemoveChannel(IN const iCAX::Data::uuid& ChannelID_) override;
            void ClearChannels() override;

        private:
            static void ValidateChannelID(IN const iCAX::Data::uuid& ChannelID_);

        private:
            mutable std::mutex m_Mutex;
            std::unordered_map<iCAX::Data::uuid, std::unique_ptr<iCAX::Mail::CMailChannel>> m_Channels;
        };
    }
}
