#pragma once

#include "IService.h"
#include "Data/uuid.h"
#include "Mailbox/MailPostOffice.h"

namespace iCAX
{
    namespace Services
    {
        class _SERVICE_EXP IMailChannelService : public IService
        {
        public:
            ~IMailChannelService() override = default;

        public:
            virtual bool CreateChannel(IN const iCAX::Data::uuid& ChannelID_) = 0;
            virtual bool HasChannel(IN const iCAX::Data::uuid& ChannelID_) const = 0;
            virtual iCAX::Mail::CMailPostOffice GetFrontendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const = 0;
            virtual iCAX::Mail::CMailPostOffice GetBackendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const = 0;
            virtual bool RemoveChannel(IN const iCAX::Data::uuid& ChannelID_) = 0;
            virtual void ClearChannels() = 0;
        };
    }
}
