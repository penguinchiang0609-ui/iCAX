#pragma once

#include "FacadesExport.h"
#include "FacadeMethod.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX::Interaction
{
    enum class EFacadeCallStatus : int32_t
    {
        Ok = 0,
        FacadeNotFound = 1,
        MethodNotFound = 2,
        InvalidCall = 3,
    };

    /*
    * @brief 对 Facade 方法的一次调用。
    * @details 调用可以由 Mail 承载，也可以由测试或进程内代码直接发起。
    */
    struct _FACADES_EXP CFacadeCall final
    {
        uint64_t nCallID = 0;
        uint64_t nOriginID = 0;
        CFacadeMethod Method;
        std::vector<uint8_t> Payload;
    };

    struct _FACADES_EXP CFacadeResult final
    {
        uint64_t nCallID = 0;
        uint64_t nOriginID = 0;
        CFacadeMethod Method;
        EFacadeCallStatus nStatus = EFacadeCallStatus::Ok;
        std::string strError;
        std::vector<uint8_t> Payload;

        bool IsOK() const noexcept;
    };
}
