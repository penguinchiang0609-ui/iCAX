#include "pch.h"
#include "MailPayload.h"

#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

void iCAX::Mail::ReleaseMailPayload(IN OUT Mail& Mail_) noexcept
{
    if (Mail_.Payload.pLease)
    {
        Mail_.Payload.pLease->ReleaseMailPayloadLease(
            Mail_.Payload.nLeaseIndex,
            Mail_.Payload.nLeaseGeneration);
        Mail_.Payload.pLease.reset();
        Mail_.Payload.nLeaseIndex = (std::numeric_limits<size_t>::max)();
        Mail_.Payload.nLeaseGeneration = 0;
        Mail_.Payload.pData = nullptr;
        Mail_.Payload.nSize = 0;
        return;
    }

    delete[] Mail_.Payload.pData;
    Mail_.Payload.pData = nullptr;
    Mail_.Payload.nSize = 0;
    Mail_.Payload.nLeaseIndex = (std::numeric_limits<size_t>::max)();
    Mail_.Payload.nLeaseGeneration = 0;
}

void iCAX::Mail::SetMailPayloadText(IN OUT Mail& Mail_, IN const std::string& strText_)
{
    ReleaseMailPayload(Mail_);
    if (strText_.empty())
    {
        return;
    }

    auto _Payload = std::make_unique<uint8_t[]>(strText_.size());
    std::memcpy(_Payload.get(), strText_.data(), strText_.size());

    Mail_.Payload.nSize = strText_.size();
    Mail_.Payload.pData = _Payload.release();
}

std::string iCAX::Mail::GetMailPayloadText(IN const Mail& Mail_)
{
    if (Mail_.Payload.nSize == 0)
    {
        return {};
    }
    if (!Mail_.Payload.pData)
    {
        throw std::invalid_argument("mail payload data is null");
    }
    return std::string(
        reinterpret_cast<const char*>(Mail_.Payload.pData),
        Mail_.Payload.nSize);
}

iCAX::Mail::Mail iCAX::Mail::CreateTextMail(IN const MailHeader& Header_, IN const std::string& strText_)
{
    Mail _Mail;
    _Mail.Header = Header_;
    SetMailPayloadText(_Mail, strText_);
    return _Mail;
}
