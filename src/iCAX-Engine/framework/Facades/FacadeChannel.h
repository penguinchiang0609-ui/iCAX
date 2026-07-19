#pragma once

#include "FacadeEndpoint.h"

#include <memory>

namespace iCAX::Interaction
{
    struct _FACADES_EXP CFacadeChannelCreateInfo final
    {
        CFacadeQueueCreateInfo EndAToEndBQueue;
        CFacadeQueueCreateInfo EndBToEndAQueue;
    };

    class _FACADES_EXP CFacadeChannel final
    {
    public:
        CFacadeChannel();
        explicit CFacadeChannel(IN const CFacadeChannelCreateInfo& CreateInfo_);

        CFacadeChannel(IN const CFacadeChannel&) = delete;
        CFacadeChannel& operator=(IN const CFacadeChannel&) = delete;

        CFacadeEndpoint GetEndAEndpoint() noexcept;
        CFacadeEndpoint GetEndBEndpoint() noexcept;
        void Clear();
        void Reset();
        CFacadeQueue& GetAToBQueue() noexcept;
        CFacadeQueue& GetBToAQueue() noexcept;

    private:
        CFacadeChannelCreateInfo m_CreateInfo;
        std::shared_ptr<CFacadeQueue> m_AToB;
        std::shared_ptr<CFacadeQueue> m_BToA;
    };
}
