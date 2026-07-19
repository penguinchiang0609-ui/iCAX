#include "pch.h"
#include "Invocation.h"


void iCAX::Interaction::CInvocation::Report(IN const std::vector<uint8_t>& Payload_) const
{
    if (m_ReportHandler)
    {
        m_ReportHandler(Payload_);
    }
}

bool iCAX::Interaction::CInvocation::CanReport() const noexcept
{
    return static_cast<bool>(m_ReportHandler);
}

void iCAX::Interaction::CInvocation::SetReportHandler(IN ReportHandler Handler_)
{
    m_ReportHandler = std::move(Handler_);
}

bool iCAX::Interaction::CInvocationResult::IsOK() const noexcept
{
    return nStatus == EInvocationStatus::Ok;
}
