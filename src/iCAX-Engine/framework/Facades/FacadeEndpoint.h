#pragma once

#include "FacadeQueue.h"

#include <memory>
#include <string>
#include <vector>

namespace iCAX::Interaction
{
    /*
    * @brief Facade 双向通道的一端。
    * @details Send 写入对端队列，Receive 取出本端已经到达的帧；Endpoint 不拥有队列。
    */
    class _FACADES_EXP CFacadeEndpoint final
    {
    public:
        CFacadeEndpoint() = default;
        CFacadeEndpoint(
            IN std::shared_ptr<CFacadeQueue> pIncomingQueue_,
            IN std::shared_ptr<CFacadeQueue> pOutgoingQueue_) noexcept;

        bool IsValid() const noexcept;
        void Send(IN const CFacadeFrame& Frame_) const;
        void Send(IN CFacadeFrame&& Frame_) const;
        void SendText(
            IN uint64_t nCallID_,
            IN uint64_t nMethodCode_,
            IN EFacadeFrameKind Kind_,
            IN const std::string& strPayloadText_,
            IN EInvocationStatus Status_ = EInvocationStatus::Ok) const;
        std::vector<CFacadeFrame> Receive() const;
        void ClearIncoming() const;
        void ClearOutgoing() const;

    private:
        std::shared_ptr<CFacadeQueue> RequireIncomingQueue() const;
        std::shared_ptr<CFacadeQueue> RequireOutgoingQueue() const;

    private:
        std::weak_ptr<CFacadeQueue> m_pIncomingQueue;
        std::weak_ptr<CFacadeQueue> m_pOutgoingQueue;
    };
}
