#include "pch.h"
#include "CMailCommandHandler.h"

#include <Mailbox/MailPayload.h>

#include <stdexcept>
#include <vector>

namespace
{
    class CMailPayloadScope final
    {
    public:
        explicit CMailPayloadScope(IN iCAX::Mail::Mail& Mail_) noexcept
            : m_Mail(Mail_)
        {
        }

        ~CMailPayloadScope() noexcept
        {
            iCAX::Mail::ReleaseMailPayload(m_Mail);
        }

        CMailPayloadScope(IN const CMailPayloadScope&) = delete;
        CMailPayloadScope& operator=(IN const CMailPayloadScope&) = delete;

    private:
        iCAX::Mail::Mail& m_Mail;
    };
}

iCAX::Command::CCommandRequest iCAX::MailHandler::CMailCommandHandler::ToCommandRequest(
    IN const iCAX::Mail::Mail& Mail_) const
{
    iCAX::Command::CCommandRequest _Request;
    _Request.nCommandID = Mail_.Header.nMailId;
    _Request.nOriginID = Mail_.Header.nOriginId;
    _Request.Route = iCAX::Command::MakeCommandRoute(Mail_.Header.nTypeCode);

    if (Mail_.Payload.nSize > 0)
    {
        if (Mail_.Payload.pData == nullptr)
        {
            throw std::invalid_argument("mail payload data is null");
        }
        _Request.Payload.assign(Mail_.Payload.pData, Mail_.Payload.pData + Mail_.Payload.nSize);
    }
    return _Request;
}

iCAX::Mail::StampCode iCAX::MailHandler::CMailCommandHandler::ToMailStamp(
    IN iCAX::Command::ECommandStatusCode Status_) const noexcept
{
    switch (Status_)
    {
    case iCAX::Command::ECommandStatusCode::Ok:
        return iCAX::Mail::kMailOk;
    case iCAX::Command::ECommandStatusCode::NoHandler:
        return iCAX::Mail::kMailNoHandler;
    case iCAX::Command::ECommandStatusCode::InvalidRequest:
    default:
        return iCAX::Mail::kMailInvalidPayload;
    }
}

size_t iCAX::MailHandler::CMailCommandHandler::DispatchAvailableMails(
    IN const iCAX::Mail::CMailPostOffice& PostOffice_,
    IN const iCAX::Command::CCommandDispatcher& Dispatcher_,
    IN iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN const MailIDAllocator& AllocateResponseMailID_) const
{
    if (!PostOffice_.IsValid())
    {
        return 0;
    }
    if (!AllocateResponseMailID_)
    {
        throw std::invalid_argument("response mail id allocator cannot be empty");
    }

    auto _Mails = PostOffice_.Receive();
    size_t _HandledCount = 0;
    for (auto& _Mail : _Mails)
    {
        CMailPayloadScope _RequestPayloadScope(_Mail);

        auto _Request = ToCommandRequest(_Mail);
        auto _Response = Dispatcher_.Dispatch(
            _Request,
            ApplicationContext_,
            pProductContext_,
            pProjectContext_);

        SendCommandResponse(
            PostOffice_,
            _Mail,
            _Response,
            AllocateResponseMailID_());
        ++_HandledCount;
    }
    return _HandledCount;
}

void iCAX::MailHandler::CMailCommandHandler::SendCommandResponse(
    IN const iCAX::Mail::CMailPostOffice& PostOffice_,
    IN const iCAX::Mail::Mail& RequestMail_,
    IN const iCAX::Command::CCommandResponse& Response_,
    IN uint64_t nResponseMailID_) const
{
    iCAX::Mail::MailHeader _Header;
    _Header.nMailId = nResponseMailID_;
    _Header.nOriginId = RequestMail_.Header.nMailId;
    _Header.nTypeCode = Response_.Route.GetRouteCode();
    _Header.nStamp = ToMailStamp(Response_.nStatus);

    const auto* _pPayload = &Response_.Payload;
    std::vector<uint8_t> _ErrorPayload;
    if (_pPayload->empty() && !Response_.strError.empty())
    {
        _ErrorPayload.assign(Response_.strError.begin(), Response_.strError.end());
        _pPayload = &_ErrorPayload;
    }

    PostOffice_.SendPayload(
        _Header,
        _pPayload->empty() ? nullptr : _pPayload->data(),
        _pPayload->size());
}

