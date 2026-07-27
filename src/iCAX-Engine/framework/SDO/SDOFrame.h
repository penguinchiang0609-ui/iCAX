#pragma once

#include "Invocation.h"

#include <cstdint>

namespace iCAX::Interaction
{
    /*
    * @brief SDO 调用在两个运行端之间传输时的阶段。
    * @details Request 发起调用；Report 汇报中间状态；Response 唯一结束调用；Event 主动通知。
    */
    enum class ESDOFrameKind : uint16_t
    {
        Request = 0,
        Report = 1,
        Response = 2,
        Event = 3,
    };

    /*
    * @brief SDO 内部传输帧。
    * @details
    *   同一次 Request、Report 和 Response 共享同一个 nCallID，不再使用另一组消息关联 ID。
    *   nMethodCode 承载 SDOName.MethodName 的稳定编码；Payload 由帧自身拥有。
    */
    struct _SDO_EXP CSDOFrame final
    {
        uint64_t nCallID = 0;
        uint64_t nMethodCode = 0;
        ESDOFrameKind nKind = ESDOFrameKind::Request;
        EInvocationStatus nStatus = EInvocationStatus::Ok;
        SDOPayload Payload;
    };
}
