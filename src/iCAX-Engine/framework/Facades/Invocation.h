#pragma once

#include "FacadesExport.h"
#include "FacadeMethod.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace iCAX::Interaction
{
    enum class EInvocationStatus : int32_t
    {
        Ok = 0,
        FacadeNotFound = 1,
        MethodNotFound = 2,
        InvalidInvocation = 3,
    };

    /*
    * @brief 对 Facade 方法的一次调用。
    * @details 调用可以由 FacadeFrame 承载，也可以由进程内代码直接发起。
    */
    struct _FACADES_EXP CInvocation final
    {
        using ReportHandler = std::function<void(const std::vector<uint8_t>&)>;

        uint64_t nCallID = 0;
        CFacadeMethod Method;
        std::vector<uint8_t> Payload;

        /*
        * @brief 当前 Facade 方法执行期间向调用方汇报进度或状态。
        * @details
        *   Report 只产生中间汇报，不结束调用。Facade 方法仍然同步返回 CInvocationResult，
        *   由 Facade 调用运行环境把汇报送到调用方。进程内直接调用且没有绑定
        *   ReportHandler 时，本方法安全地忽略汇报。
        */
        void Report(IN const std::vector<uint8_t>& Payload_) const;

        bool CanReport() const noexcept;

        /*
        * @brief 由调用承载适配器绑定汇报出口，普通 Facade 实现不需要调用。
        */
        void SetReportHandler(IN ReportHandler Handler_);

    private:
        ReportHandler m_ReportHandler;
    };

    struct _FACADES_EXP CInvocationResult final
    {
        uint64_t nCallID = 0;
        CFacadeMethod Method;
        EInvocationStatus nStatus = EInvocationStatus::Ok;
        std::string strError;
        std::vector<uint8_t> Payload;

        bool IsOK() const noexcept;
    };
}
