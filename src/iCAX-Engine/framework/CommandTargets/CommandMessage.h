#pragma once

#include "CommandTargetsExport.h"
#include "CommandRoute.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 命令执行状态。
        * @details
        *   这里只表达命令路由层可以正常判断的状态。
        *   handler 内部异常不在这里编码，默认直接向外抛出，便于错误停在第一现场。
        */
        enum class ECommandStatusCode : int32_t
        {
            Ok = 0,
            NoHandler = 1,
            InvalidRequest = 2,
        };

        /*
        * @brief 命令请求。
        * @details
        *   CommandTargets 不关心请求来自 mailbox、测试代码还是其他入口。
        *   Route 用于主/子命令分发，Payload 由具体命令自行解释。
        */
        struct _COMMAND_TARGETS_EXP CCommandRequest final
        {
            uint64_t nCommandID = 0; //!< 请求 ID，通常来自邮件 ID。
            uint64_t nOriginID = 0;  //!< 来源 ID，通常用于响应链路。
            CCommandRoute Route;      //!< 主/子命令路由。
            std::vector<uint8_t> Payload; //!< 命令负载。
        };

        /*
        * @brief 命令响应。
        */
        struct _COMMAND_TARGETS_EXP CCommandResponse final
        {
            uint64_t nCommandID = 0; //!< 对应请求 ID。
            uint64_t nOriginID = 0;  //!< 对应请求来源 ID。
            CCommandRoute Route;      //!< 对应命令路由。
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
