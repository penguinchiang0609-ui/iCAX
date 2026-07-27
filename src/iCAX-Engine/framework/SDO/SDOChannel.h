#pragma once

#include "SDOEndpoint.h"

#include <memory>

namespace iCAX::Interaction
{
    struct _SDO_EXP CSDOChannelCreateInfo final
    {
        CSDOQueueCreateInfo EndAToEndBQueue;
        CSDOQueueCreateInfo EndBToEndAQueue;
    };

    class _SDO_EXP CSDOChannel final
    {
    public:
        CSDOChannel();
        explicit CSDOChannel(IN const CSDOChannelCreateInfo& CreateInfo_);

        CSDOChannel(IN const CSDOChannel&) = delete;
        CSDOChannel& operator=(IN const CSDOChannel&) = delete;

        CSDOEndpoint GetEndAEndpoint() noexcept;
        CSDOEndpoint GetEndBEndpoint() noexcept;
        void Clear();
        void Reset();
        CSDOQueue& GetAToBQueue() noexcept;
        CSDOQueue& GetBToAQueue() noexcept;

    private:
        CSDOChannelCreateInfo m_CreateInfo;
        std::shared_ptr<CSDOQueue> m_AToB;
        std::shared_ptr<CSDOQueue> m_BToA;
    };
}
