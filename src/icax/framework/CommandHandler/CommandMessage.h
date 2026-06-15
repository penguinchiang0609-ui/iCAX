#pragma once

#include "CommandHandlerExport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Command
    {
        enum class ECommandStatusCode : int32_t
        {
            Ok = 0,
            NoHandler = 1,
            InvalidRequest = 2,
            ExecutionError = 3,
        };

        struct _COMMAND_HANDLER_EXP CCommandRequest final
        {
            uint64_t nCommandID = 0;
            uint64_t nOriginID = 0;
            uint64_t nTypeCode = 0;
            std::vector<uint8_t> Payload;
        };

        struct _COMMAND_HANDLER_EXP CCommandResponse final
        {
            uint64_t nCommandID = 0;
            uint64_t nOriginID = 0;
            uint64_t nTypeCode = 0;
            ECommandStatusCode nStatus = ECommandStatusCode::Ok;
            std::string strError;
            std::vector<uint8_t> Payload;

            bool IsOK() const;
        };
    }
}
