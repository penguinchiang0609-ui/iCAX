#pragma once

#include "SDOExport.h"
#include "SDOMethod.h"
#include "SDOPayload.h"

#include <cstdint>
#include <functional>
#include <string>

namespace iCAX::Interaction
{
    enum class EInvocationStatus : int32_t
    {
        Ok = 0,
        SDONotFound = 1,
        MethodNotFound = 2,
        InvalidInvocation = 3,
    };

    /*
    * @brief 对 SDO 方法的一次调用。
    * @details 调用可以由 SDOFrame 承载，也可以由进程内代码直接发起。
    */
    struct _SDO_EXP CInvocation final
    {
        using ReportHandler = std::function<void(const SDOPayload&)>;

        uint64_t nCallID = 0;
        CSDOMethod Method;
        SDOPayload Payload;

        /*
        * @brief 当前 SDO 方法执行期间向调用方汇报进度或状态。
        * @details
        *   Report 只产生中间汇报，不结束调用。SDO 方法仍然同步返回 CInvocationResult，
        *   由 SDO 调用运行环境把汇报送到调用方。进程内直接调用且没有绑定
        *   ReportHandler 时，本方法安全地忽略汇报。
        */
        void Report(IN const SDOPayload& Payload_) const;

        bool CanReport() const noexcept;

        /*
        * @brief 由调用承载适配器绑定汇报出口，普通 SDO 实现不需要调用。
        */
        void SetReportHandler(IN ReportHandler Handler_);

    private:
        ReportHandler m_ReportHandler;
    };

    struct _SDO_EXP CInvocationResult final
    {
        uint64_t nCallID = 0;
        CSDOMethod Method;
        EInvocationStatus nStatus = EInvocationStatus::Ok;
        std::string strError;
        SDOPayload Payload;

        bool IsOK() const noexcept;
    };
}
