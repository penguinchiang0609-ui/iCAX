#pragma once

#include <cstdint>
#include <vector>

namespace iCAX::Interaction
{
    /*
    * @brief SDO/Mailbox 的服务数据对象（SDO）负载。
    * @details
    *   SDO 拥有完整的消息数据。业务协议优先把该字节序列编码为普通
    *   Google FlatBuffer；队列和路由层不解析具体 schema。
    */
    using SDOPayload = std::vector<uint8_t>;
}
