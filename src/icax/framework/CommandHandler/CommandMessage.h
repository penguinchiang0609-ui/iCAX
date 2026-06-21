#pragma once

#include "CommandHandlerExport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 命令执行状态。
        * @details Dispatcher 会把 handler 抛出的异常转换为这些状态，调用方再映射为邮件 Stamp。
        */
        enum class ECommandStatusCode : int32_t
        {
            Ok = 0,
            NoHandler = 1,
            InvalidRequest = 2,
            ExecutionError = 3,
        };

        /*
        * @brief 命令请求。
        * @details
        *   CommandHandler 不关心请求来自 mailbox、测试代码还是其他入口。
        *   nTypeCode 用于查找 handler，Payload 由具体命令自行解释。
        */
        struct _COMMAND_HANDLER_EXP CCommandRequest final
        {
            uint64_t nCommandID = 0; //!< 请求 ID，通常来自邮件 ID。
            uint64_t nOriginID = 0;  //!< 来源 ID，通常用于响应链路。
            uint64_t nTypeCode = 0;  //!< 命令类型码。
            std::vector<uint8_t> Payload; //!< 命令负载。
        };

        /*
        * @brief 命令响应。
        */
        struct _COMMAND_HANDLER_EXP CCommandResponse final
        {
            uint64_t nCommandID = 0; //!< 对应请求 ID。
            uint64_t nOriginID = 0;  //!< 对应请求来源 ID。
            uint64_t nTypeCode = 0;  //!< 对应命令类型码。
            ECommandStatusCode nStatus = ECommandStatusCode::Ok; //!< 执行状态。
            std::string strError; //!< 失败原因；成功时通常为空。
            std::vector<uint8_t> Payload; //!< 响应负载。

            /*
            * @brief 判断响应是否成功。
            * @return true 表示 nStatus 为 Ok。
            */
            bool IsOK() const;
        };
    }
}
