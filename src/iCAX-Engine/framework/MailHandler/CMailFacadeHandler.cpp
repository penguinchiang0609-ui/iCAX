#include "pch.h"
#include "CMailFacadeHandler.h"

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

iCAX::Interaction::CFacadeCall iCAX::MailHandler::CMailFacadeHandler::ToFacadeCall(
    IN const iCAX::Mail::Mail& Mail_) const
{
    iCAX::Interaction::CFacadeCall _Call;
    _Call.nCallID = Mail_.Header.nMailId;
    _Call.nOriginID = Mail_.Header.nOriginId;
    _Call.Method = iCAX::Interaction::MakeFacadeMethod(Mail_.Header.nTypeCode);

    if (Mail_.Payload.nSize > 0)
    {
        if (Mail_.Payload.pData == nullptr)
        {
            throw std::invalid_argument("mail payload data is null");
        }
        _Call.Payload.assign(Mail_.Payload.pData, Mail_.Payload.pData + Mail_.Payload.nSize);
    }
    return _Call;
}

iCAX::Mail::StampCode iCAX::MailHandler::CMailFacadeHandler::ToMailStamp(
    IN iCAX::Interaction::EFacadeCallStatus Status_) const noexcept
{
    switch (Status_)
    {
    case iCAX::Interaction::EFacadeCallStatus::Ok:
        return iCAX::Mail::kMailOk;
    case iCAX::Interaction::EFacadeCallStatus::FacadeNotFound:
    case iCAX::Interaction::EFacadeCallStatus::MethodNotFound:
        return iCAX::Mail::kMailNotFound;
    case iCAX::Interaction::EFacadeCallStatus::InvalidCall:
    default:
        return iCAX::Mail::kMailInvalidPayload;
    }
}

size_t iCAX::MailHandler::CMailFacadeHandler::HandleAvailableMails(
    IN const iCAX::Mail::CMailPostOffice& PostOffice_,
    IN const iCAX::Interaction::CFacadeInvoker& Invoker_,
    IN iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_,
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

        iCAX::Interaction::CFacadeResult _Result;
        try
        {
            auto _Call = ToFacadeCall(_Mail);
            _Result = Invoker_.Invoke(
                _Call,
                ApplicationContext_,
                pProductContext_,
                pProjectContext_,
                pSceneContext_);
        }
        catch (const std::exception& Error_)
        {
            _Result.Method = iCAX::Interaction::MakeFacadeMethod(_Mail.Header.nTypeCode);
            _Result.nStatus = iCAX::Interaction::EFacadeCallStatus::InvalidCall;
            _Result.strError = Error_.what();
        }
        catch (...)
        {
            _Result.Method = iCAX::Interaction::MakeFacadeMethod(_Mail.Header.nTypeCode);
            _Result.nStatus = iCAX::Interaction::EFacadeCallStatus::InvalidCall;
            _Result.strError = "Facade method caught a non-standard exception";
        }

        try
        {
            SendFacadeResult(
                PostOffice_,
                _Mail,
                _Result,
                AllocateResponseMailID_());
        }
        catch (const std::exception& Error_)
        {
            iCAX::Interaction::CFacadeResult _FailureResult;
            _FailureResult.Method = iCAX::Interaction::MakeFacadeMethod(_Mail.Header.nTypeCode);
            _FailureResult.nStatus = iCAX::Interaction::EFacadeCallStatus::InvalidCall;
            _FailureResult.strError = Error_.what();
            SendFacadeResult(
                PostOffice_,
                _Mail,
                _FailureResult,
                AllocateResponseMailID_());
        }
        ++_HandledCount;
    }
    return _HandledCount;
}

void iCAX::MailHandler::CMailFacadeHandler::SendFacadeResult(
    IN const iCAX::Mail::CMailPostOffice& PostOffice_,
    IN const iCAX::Mail::Mail& RequestMail_,
    IN const iCAX::Interaction::CFacadeResult& Result_,
    IN uint64_t nResponseMailID_) const
{
    iCAX::Mail::MailHeader _Header;
    _Header.nMailId = nResponseMailID_;
    _Header.nOriginId = RequestMail_.Header.nMailId;
    _Header.nTypeCode = Result_.Method.GetCode();
    _Header.nStamp = ToMailStamp(Result_.nStatus);

    const auto* _pPayload = &Result_.Payload;
    std::vector<uint8_t> _ErrorPayload;
    if (_pPayload->empty() && !Result_.strError.empty())
    {
        _ErrorPayload.assign(Result_.strError.begin(), Result_.strError.end());
        _pPayload = &_ErrorPayload;
    }

    PostOffice_.SendPayload(
        _Header,
        _pPayload->empty() ? nullptr : _pPayload->data(),
        _pPayload->size());
}
