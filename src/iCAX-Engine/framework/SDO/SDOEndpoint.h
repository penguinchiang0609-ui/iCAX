#pragma once

#include "SDOQueue.h"

#include <memory>
#include <string>
#include <vector>

namespace iCAX::Interaction
{
    /*
    * @brief SDO 双向通道的一端。
    * @details Send 写入对端队列，Receive 取出本端已经到达的帧；Endpoint 不拥有队列。
    */
    class _SDO_EXP CSDOEndpoint final
    {
    public:
        CSDOEndpoint() = default;
        CSDOEndpoint(
            IN std::shared_ptr<CSDOQueue> pIncomingQueue_,
            IN std::shared_ptr<CSDOQueue> pOutgoingQueue_) noexcept;

        bool IsValid() const noexcept;
        void Send(IN const CSDOFrame& Frame_) const;
        void Send(IN CSDOFrame&& Frame_) const;
        void SendText(
            IN uint64_t nCallID_,
            IN uint64_t nMethodCode_,
            IN ESDOFrameKind Kind_,
            IN const std::string& strPayloadText_,
            IN EInvocationStatus Status_ = EInvocationStatus::Ok) const;
        std::vector<CSDOFrame> Receive() const;
        void ClearIncoming() const;
        void ClearOutgoing() const;

    private:
        std::shared_ptr<CSDOQueue> RequireIncomingQueue() const;
        std::shared_ptr<CSDOQueue> RequireOutgoingQueue() const;

    private:
        std::weak_ptr<CSDOQueue> m_pIncomingQueue;
        std::weak_ptr<CSDOQueue> m_pOutgoingQueue;
    };
}
